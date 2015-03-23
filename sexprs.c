#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>
#include "sexp.h"

/*
 * full SDSI/SPKI S-expression reader
 *
 * Copyright © 2008 Vita Nuova Holdings Limited
 * based on Limbo version Copyright © 2003-2004 Vita Nuova Holdings Limited
 */

enum{
	Maxtoken=	1024*1024,	/* should be more than enough */
	RIVEST=	0,		/* don't enforce Rivest's s-expr requirement that tokens can't start with digits */

	Here=	-1,
};

#define	waserror()	(rd->nerrlab++, setjmp(rd->errlab[rd->nerrlab-1]))
#define	nexterror()	longjmp(rd->errlab[--rd->nerrlab], 1);
#define	poperror()	rd->nerrlab--

typedef struct Rd Rd;
struct Rd {
	Biobuf*	t;
	uchar*	base;
	uchar*	p;
	uchar*	end;
	int	nerrlab;
	jmp_buf	errlab[50];	/* to do: depth */
	char*	diag;
	vlong	pos;
};

#define	rdgetb(rd)	((rd)->p!=nil? ((rd)->p == (rd)->end? -1: *(rd)->p++): Bgetc((rd)->t))

static Sexp*	decodesform(Rd*, int (*)(uchar*, int, char*, int), int, String*);
static Sexp*	parseitem(Rd*);
static Sexp*	simplestring(Rd*, int, String*);
static Sexp* sform(Rd*, uchar*, uint, String*);
static String*	_b_new(void*, uint);
static String*	quote(String*);
static String*	toclosing(Rd*, int);
static String*	unquote(Rd*);
static int	ws(Rd*);
static int istextual(uchar*, uint);
static int istoken(String*);
static int istokenc(int c);
static uchar*	basedec(Rd*, int (*)(uchar*, int, char*, int), String*, uint*);
static void*	ck(Rd*, void*);

static void
rdaopen(Rd* rd, uchar* buf, uint buflen)
{
	rd->t = nil;
	rd->base = buf;
	rd->p = rd->base;
	rd->end = buf+buflen;
	rd->diag = nil;
	rd->pos = 0;
	rd->nerrlab = 0;
}

static vlong
rdoffset(Rd* rd)
{
	if(rd->p != nil)
		return rd->p - rd->base;
	return Boffset(rd->t);
}

static void
rdungetb(Rd* rd)
{
	if(rd->p != nil){
		if(rd->p != rd->base)
			rd->p--;
	}else
		Bungetc(rd->t);
}

static void
synerr(Rd* rd, char* diag, vlong pos)
{
	if(rd->diag == nil){	/* record first one */
		rd->diag = diag;
		rd->pos = pos;
	}
	nexterror();
}

static Sexp*
se_new(Rd *rd, int tag)
{
	Sexp *s;

	s = ck(rd, mallocz(sizeof(*s), 1));
	s->inuse = 1;
	s->tag = tag;
	return s;
}

Sexp*
se_string(String *s)
{
	Sexp *e;

	e = se_new(nil, Sstring);
	e->s = s;
	return e;
}

Sexp*
se_str(char *s)
{
	return se_string(s_copy(s));
}

Sexp*
se_binary(String *b)
{
	Sexp *e;

	e = se_new(nil, Sbinary);
	e->s = b;
	return e;
}

Sexp*
se_data(uchar *a, uint alen)
{
	return se_binary(b_new(a, alen));
}

Sexp*
se_cons(Sexp *a, Sexp *b)
{
	Sexp *e;

	e = se_new(nil, Slist);
	e->hd = a;
	e->tl = b;
	return e;
}

Sexp*
se_list(Sexp *e0, ...)
{
	Sexp *e, *l;
	va_list ap;

	e = se_cons(e0, nil);
	if(e0 == nil)
		return e;
	va_start(ap, e0);
	for(l = e; (e0 = va_arg(ap, Sexp*)) != nil; l = l->tl)
		l->tl = se_cons(e0, nil);
	va_end(ap);
	return e;
}

Sexp*
se_form(char *a, Sexp *e0, ...)
{
	Sexp *e, *l;
	va_list ap;

	e  = se_cons(se_string(s_copy(a)), nil);
	if(e0 == nil)
		return e;
	e->tl = se_cons(e0, nil);
	va_start(ap, e0);
	for(l = e->tl; (e0 = va_arg(ap, Sexp*)) != nil; l = l->tl)
		l->tl = se_cons(e0, nil);
	va_end(ap);
	return e;
}

Sexp*
se_incref(Sexp *s)
{
	if(s != nil){
		lock(s);
		s->inuse++;
		unlock(s);
	}
	return s;
}

void
se_free(Sexp *e)
{
	if(e == nil)
		return;
	lock(e);
	if(--e->inuse != 0){
		unlock(e);
		return;
	}
	unlock(e);
	switch(e->tag){
	case Sstring:
	case Sbinary:
		if(e->s != nil)
			s_free(e->s);
		if(e->hint != nil)
			s_free(e->hint);
		break;
	case Slist:
		se_free(e->hd);
		se_free(e->tl);
		break;
	}
	free(e);
}

Sexp*
se_read(Biobuf *b, char *err, uint errlen)
{
	Rd rdb, *rd = &rdb;
	Sexp *e;

	rd->t = b;
	rd->base = nil;
	rd->p = nil;
	rd->diag = nil;
	rd->pos = 0;
	if(waserror()){
		if(rd->pos < 0)
			rd->pos += Boffset(b);
		werrstr("%s at offset %lld", rd->diag, rd->pos);
		if(err != nil)
			rerrstr(err, errlen);
		return nil;
	}
	if(err != nil)
		*err = 0;
	e = parseitem(rd);
	poperror();
	return e;
}

Sexp*
se_parse(char *s, char **ep)
{
	return se_unpack(s, strlen(s), ep);
}

Sexp*
se_unpack(char* buf, uint buflen, char **ep)
{
	Rd rdb, *rd = &rdb;
	Sexp *e;

	rdaopen(rd, (uchar*)buf, buflen);
	if(waserror()){
		if(rd->pos < 0)
			rd->pos += rdoffset(rd);
		werrstr("%s at offset %lld", rd->diag, rd->pos);
		*ep = buf;		/* perhaps */
		return nil;
	}
	e = parseitem(rd);
	poperror();
	if(ep != nil)
		*ep = (char*)rd->p;
	return e;
}

static Sexp*
parseitem(Rd *rd)
{
	vlong p0;
	int c;
	Rd nrb, *nrd = &nrb;
	Sexp *e, *le, *l;
	String *a;
	uchar *b;
	uint blen;

	p0 = rdoffset(rd);
	c = ws(rd);
	if(c < 0)
		return nil;
	switch(c){
	case '{':
		a = toclosing(rd, '}');
		if(waserror()){
			s_free(a);
			nexterror();
		}
		b = basedec(rd, dec64, a, &blen);
		poperror();
		s_free(a);
		if(waserror()){
			free(b);
			nexterror();
		}
		rdaopen(nrd, b, blen);
		e = parseitem(nrd);
		poperror();
		free(b);
		return e;
	case '(':
		e = se_new(rd, Slist);
		if(waserror()){
			se_free(e);
			nexterror();
		}
		l = e;
		while((c = ws(rd)) != ')'){
			if(c < 0)
				synerr(rd, "unclosed '('", p0);
			rdungetb(rd);
			le = parseitem(rd);	/* we'll catch missing ) at top of loop */
			if(waserror()){
				se_free(le);
				nexterror();
			}
			if(l->hd != nil)
				l = l->tl = se_new(rd, Slist);
			l->hd = le;
			poperror();
		}
		poperror();
		return e;
	case '[':
		/* display hint */
		e = simplestring(rd, rdgetb(rd), nil);
		if(waserror()){
			se_free(e);
			nexterror();
		}
		c = ws(rd);
		if(c != ']'){
			if(c >= 0)
				rdungetb(rd);
			synerr(rd, "missing ] in display hint", p0);
		}
		if(e->tag != Sstring)
			synerr(rd, "illegal display hint", Here);
		a = e->s;
		e->s = nil;
		poperror();
		se_free(e);
		if(waserror()){
			s_free(a);
			nexterror();
		}
		e = simplestring(rd, ws(rd), a);
		poperror();
		return e;
	default:
		/* token */
		return simplestring(rd, c, nil);
	}
}

static int
isspace(int c)
{
	return c == ' ' || c == '\r' || c == '\t' || c == '\n';
}

/* skip white space */
static int
ws(Rd* rd)
{
	int c;

	while(isspace(c = rdgetb(rd)))
		{}
	return c;
}

static Sexp*
simplestring(Rd* rd, int c, String* hint)
{
	int i, dec;
	String *decs, *text, *s;
	uchar *a;
	Sexp *e;

	dec = -1;
	decs = nil;
	if(c >= '0' && c <= '9'){
		decs = s_new();
		for(dec = 0; c >= '0' && c <= '9'; c = rdgetb(rd)){
			dec = dec*10 + c-'0';
			s_putc(decs, c);
		}
		if(dec < 0 || dec > Maxtoken){
			s_free(decs);
			synerr(rd, "implausible token length", Here);
		}
		s_terminate(decs);
	}
	switch(c){
	case '"':
		text = unquote(rd);
		e = se_new(rd, Sstring);
		e->s = text;
		e->hint = hint;
		return e;
	case '|':
		return decodesform(rd, dec64, c, hint);
	case '#':
		return decodesform(rd, dec16, c, hint);
	default:
		if(c == ':' && dec >= 0){	/* byte count of raw bytes */
			a = ck(rd, malloc(dec+1));
			for(i = 0; i < dec; i++){
				c = rdgetb(rd);
				if(c < 0)
					synerr(rd, "missing bytes in raw token", Here);
				a[i] = c;
			}
			a[i] = 0;
			return sform(rd, a, dec, hint);
		}
		if(RIVEST){
			if(decs != nil){
				s_free(decs);
				synerr(rd, "token can't start with a digit", Here);
			}
		}else
			s = decs;	/* not valid according to Rivest's s-expressions, but more convenient for users */
		/* <token> by definition is always printable; never utf-8 */
		while(istokenc(c)){
			if(s == nil)
				s = s_new();
			s_putc(s, c);
			c = rdgetb(rd);
		}
		if(s == nil)
			synerr(rd, "missing token", Here);	/* consume c to ensure progress on error */
		s_terminate(s);
		if(c >= 0)
			rdungetb(rd);
		e = se_new(rd, Sstring);
		e->s = s;
		e->hint = hint;
		return e;
	}
}

static Sexp*
decodesform(Rd *rd, int (*decode)(uchar*, int, char*, int), int c, String *hint)
{
	String *s;
	uchar *a;
	uint alen;

	s = toclosing(rd, c);
	if(waserror()){
		s_free(s);
		nexterror();
	}
	a = basedec(rd, decode, s, &alen);
	poperror();
	s_free(s);
	return sform(rd, a, alen, hint);
}

static Sexp*
sform(Rd *rd, uchar* a, uint alen, String* hint)
{
	Sexp *e;

	if(istextual(a, alen)){
		e = se_new(rd, Sstring);
		e->s = s_copy((char*)a);
		e->hint = hint;
		free(a);
		return e;
	}
	e = se_new(rd, Sbinary);
	e->s = _b_new((char*)a, alen);
	if(e->s == nil)
		synerr(rd, "out of memory", Here);
	return e;
}

static String*
toclosing(Rd* rd, int end)
{
	String *s;
	vlong p0;
	int c;

	s = s_new();
	p0 = rdoffset(rd);
	while((c = rdgetb(rd)) != end){
		if(c < 0){
			s_free(s);
			synerr(rd, "missing closing delimiter", p0);
		}
		s_putc(s, c);
	}
	s_terminate(s);
	return s;
}

static int
hex(int c)
{
	if(c >= '0' && c <= '9')
		return c-'0';
	if(c >= 'a' && c <= 'f')
		return 10+(c-'a');
	if(c >= 'A' && c <= 'F')
		return 10+(c-'A');
	return -1;
}

static String*
unquote(Rd* rd)
{
	String *os;
	vlong e0, p0;
	int i, c, c0, c1, oct;

	p0 = rdoffset(rd);
	os = s_new();
	while((c = rdgetb(rd)) != '"'){
		if(c < 0){
			s_free(os);
			synerr(rd, "unclosed quoted string", p0);
		}
		if(c == '\\'){
			e0 = rdoffset(rd);
			c = rdgetb(rd);
			if(c < 0)
				break;
			switch(c){
			case '\r':
				c = rdgetb(rd);
				if(c != '\n')
					rdungetb(rd);
				continue;
			case '\n':
				c = rdgetb(rd);
				if(c != '\r')
					rdungetb(rd);
				continue;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case 'v':
				c = '\v';
				break;
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				oct = 0;
				for(i = 0;;){
					if(!(c >= '0' && c <= '7')){
						s_free(os);
						synerr(rd, "illegal octal escape", e0);
					}
					oct = (oct<<3) | (c-'0');
					if(++i == 3)
						break;
					c = rdgetb(rd);
				}
				c = oct & 0xFF;
				break;
			case 'x':
				c0 = hex(rdgetb(rd));
				c1 = hex(rdgetb(rd));
				if(c0 < 0 || c1 < 0){
					s_free(os);
					synerr(rd, "illegal hex escape", e0);
				}
				c = (c0<<4) | c1;
				break;
			}
		}
		s_putc(os, c);
	}
	s_terminate(os);
	return os;
}

static int
hintlen(String* s)
{
	char buf[30];
	int n;

	if(s == nil)
		return 0;
	n = s_len(s);
	return n + snprint(buf, sizeof(buf), "[%d:]", n);
}

uint
se_packedsize(Sexp *e)
{
	int n;
	char buf[30];

	if(e == nil)
		return 0;
	switch(e->tag){
	case Sstring:
	case Sbinary:
		n = s_len(e->s);
		return hintlen(e->hint) + snprint(buf, sizeof(buf), "%d:", n) + n;
	case Slist:
		n = 1;	/* '(' */
		do{
			n += se_packedsize(e->hd);
		}while((e = e->tl) != nil);
		return n+1;	/* + ')' */
	default:
		return 0;
	}
}

static uchar*
packbytes(uchar *a, uchar *b, uint n)
{
	char buf[10];
	int nl;

	nl = snprint(buf, sizeof buf, "%d:", n);
	memmove(a, buf, nl);
	a += nl;
	memmove(a, b, n);
	return a+n;
}

static uchar*
packhint(uchar *a, String *s)
{
	if(s == nil || s_len(s) == 0)
		return a;
	*a++ = '[';
	a = packbytes(a, (uchar*)s_to_c(s), s_len(s));
	*a++ = ']';
	return a;
}

static uchar*
pack(uchar* a, Sexp *e)
{
	if(e == nil)
		return a;
	switch(e->tag){
	case Sstring:
	case Sbinary:
		if(e->hint != nil)
			a = packhint(a, e->hint);
		return packbytes(a, (uchar*)s_to_c(e->s), s_len(e->s));
	case Slist:
		*a++ = '(';
		do{
			a = pack(a, e->hd);
		}while((e = e->tl) != nil);
		*a++ = ')';
		return a;
	default:
		return a;
	}
}

uint
se_pack(uchar* buf, uint buflen, Sexp *e)
{
	uint nb;

	nb = se_packedsize(e);
	if(nb > buflen)
		return 0;
	pack(buf, e);
	return nb;
}

String*
se_b64text(Sexp *e)
{
	String *s;
	uchar *b;
	char *a;
	int n, np;

	np = se_packedsize(e);
	b = malloc(np);
	if(b == nil)
		return nil;
	if(se_pack(b, np, e) != np)
		sysfatal("se_b64text: botch");
	n = (np/3+1)*4 + 1;
	a = malloc(n+2+1);
	a[0] = '{';
	n = enc64(a+1, n, b, np);
	a[1+n] = '}';
	a[1+n+1] = 0;
	s = s_copy(a);
	free(a);
	free(b);
	return s;
}

static String*
cat(String *a, String *b)
{
	s_append(a, s_to_c(b));
	s_free(b);
	return a;
}

String*
se_text(Sexp *e)
{
	String *s;
	char *buf;
	int n;

	if(e == nil)
		return s_copy("");
	switch(e->tag){
	case Sstring:
		if(e->hint != nil){
			s = s_copy("[");
			cat(s, quote(e->hint));
			s_putc(s, ']');
			cat(s, quote(e->s));
			return s;
		}
		//s_terminate(e->s);
		return quote(e->s);
	case Sbinary:
		/* TO DO: e->hint */
		n = s_len(e->s);
		if(n <= 4){
			buf = malloc(2+n*2+1);	/* delimiters, encoded data, null byte */
			if(buf == nil)
				return nil;
			buf[0] = '#';
			n = enc16(buf+1, n*2+1, (uchar*)e->s->base, n);
			buf[1+n] = '#';
		}else{
			buf = malloc(2+(n/3+1)*4+1);
			if(buf == nil)
				return nil;
			buf[0] = '|';
			n = enc64(buf+1, n*4/3+1, (uchar*)e->s->base, n);
			buf[1+n] = '|';
		}
		buf[n+2] = 0;
		s = s_copy(buf);
		free(buf);
		return s;
	case Slist:
		s = s_copy("(");
		for(;;){
			cat(s, se_text(e->hd));
			if((e = e->tl) == nil)
				break;
			s_putc(s, ' ');
		}
		s_putc(s, ')');
		s_terminate(s);
		return s;
	default:
		return s_copy("???");
	}
}

/*
 *An octet string that meets the following conditions may be given
 *directly as a "token".
 *
 *	-- it does not begin with a digit
 *
 *	-- it contains only characters that are
 *		-- alphabetic (upper or lower case),
 *		-- numeric, or
 *		-- one of the eight "pseudo-alphabetic" punctuation marks:
 *			-   .   /   _   :  *  +  =  
 *	(Note: upper and lower case are not equivalent.)
 *	(Note: A token may begin with punctuation, including ":").
 */
static int
istokenc(int c)
{
	return c >= '0' && c <= '9' ||
		c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' ||
		c == '-' || c == '.' || c == '/' || c == '_' || c == ':' || c == '*' || c == '+' || c == '=';
}

static int
istoken(String* s)
{
	char *p;
	int i;

	if(s == nil || s_len(s) == 0)
		return 0;
	p = s_to_c(s);
	if(*p >= '0' && *p <= '9')
		return 0;	/* Rivest's s-expressions don't allow tokens to start with digits */
	for(i = 0; i < s_len(s); i++, p++){
		if(!(*p >= 'a' && *p <= 'z' || *p >= 'A' && *p <= 'Z' || *p >= '0' && *p <= '9' ||
		      *p == '-' || *p == '.' || *p == '/' || *p == '_' || *p == ':' || *p == '*' || *p == '+' || *p == '='))
			return 0;
	}
	return 1;
}

/*
 * should the data qualify as binary or text?
 *  the if(0) version accepts valid Unicode sequences
 * could use [display] to control character set?
 */
static int
istextual(uchar* a, uint alen)
{
	int i, c;

	for(i = 0; i < alen;){
//		if(0){
//			(c, n, ok) := sys->byte2char(a, i);
//			if(!ok || c < ' ' && !isspace(c) || c >= 0x7F)
//				return 0;
//			i += n;
//		}else{
			c = a[i++];
			if(c < ' ' && !isspace(c) || c >= 0x7F)
				return 0;
//		}
	}
	return 1;
}

static String*
quote(String *s)
{
	int c;
	String *os;
	char* p, buf[8];

	if(istoken(s))
		return s_incref(s);
	os = s_newalloc(s_len(s)+2);
	s_putc(os, '"');
	for(p = s_to_c(s); (c = *p) != 0; p++){
		switch(c){
		case '"':	s_append(os, "\\\""); break;
		case '\\':	s_append(os, "\\\\"); break;
		case '\b':	s_append(os, "\\b"); break;
		case '\f':	s_append(os, "\\f"); break;
		case '\n':	s_append(os, "\\n"); break;
		case '\t':	s_append(os, "\\t"); break;
		case '\r':	s_append(os, "\\r"); break;
		case '\v':	s_append(os, "\\v"); break;
		default:
			if(c < ' ' || c >= 0x7F){
				snprint(buf, sizeof(buf), "\\x%.2ux", c & 0xFF);
				s_append(os, buf);
			}else
				s_putc(os, *p);
		}
	}
	s_putc(os, '"');
	s_terminate(os);
	return os;
}

/*
 * miscellaneous S expression operations
 */

int
se_islist(Sexp *e)
{
	return e != nil && e->tag == Slist;
}

Sexp*
se_hd(Sexp *e)
{
	if(e == nil || e->tag != Slist)
		return nil;
	return e->hd;
}

Sexp*
se_tl(Sexp *e)
{
	if(e == nil || e->tag != Slist)
		return nil;
	return e->tl;
}

int
se_len(Sexp *e)
{
	int n;

	if(e == nil || e->tag != Slist || e->hd == nil)
		return 0;
	for(n = 0; e != nil; e = e->tl)
		n++;
	return n;
}

Sexp*
se_els(Sexp *e)
{
	if(e == nil || e->tag != Slist || e->hd == nil)
		return nil;
	return e;
}

char*
se_op(Sexp *e)
{
	if(e == nil)
		return nil;
	if(e->tag == Slist){
		if(e->hd == nil)
			return nil;
		e = e->hd;
	}
	if(e->tag != Sstring)
		return nil;
	return s_to_c(e->s);
}

Sexp*
se_args(Sexp *e)
{
	if(e == nil || e->tag != Slist)
		return nil;
	return e->tl;
}

String*
se_asdata(Sexp *e)
{
	if(e == nil || e->tag != Sbinary && e->tag != Sstring)
		return nil;
	return e->s;
}

String*
se_astext(Sexp *e)
{
	if(e == nil || e->tag != Sstring && e->tag != Sbinary || e->tag == Sbinary && e->s->fixed)
		return nil;
	s_terminate(e->s);
	return e->s;
}

static int
seq(String* s1, String* s2)
{
	if(s1 == s2)
		return 1;
	if(s1 == nil)
		return 0;
	return strcmp(s_to_c(s1), s_to_c(s2)) == 0;
}

int
se_eq(Sexp *e1, Sexp *e2)
{
	if(e1 == e2)
		return 1;
	if(e1 == nil || e2 == nil || e1->tag != e2->tag)
		return 0;
	switch(e1->tag){
	case Slist:
		do{
			if(e2 == nil || !se_eq(e1->hd, e2->hd))
				return 0;
			e2 = e2->tl;
		}while((e1 = e1->tl) != nil);
		return e1 == e2;
	case Sstring:
		return seq(e1->s, e2->s) && seq(e1->hint, e2->hint);
	case Sbinary:
		if(s_len(e1->s) != s_len(e2->s) || !seq(e1->hint, e2->hint))
			return 0;
		return memcmp(e1->s->base, e2->s->base, s_len(e1->s)) == 0;
	}
	return 0;
}

Sexp*
se_copy(Sexp *e)
{
	Sexp *o;

	if(e == nil)
		return nil;
	switch(e->tag){
	case Slist:
		o = se_new(nil, Slist);
		o->hd = se_copy(e->hd);
		o->tl = se_copy(e->tl);
		return o;
	case Sstring:
		o = se_new(nil, e->tag);
		o->s = s_clone(e->s);
		if(e->hint != nil)
			o->hint = s_clone(e->hint);
		return o;
	case Sbinary:
		o = se_new(nil, e->tag);
		o->s = b_copy(e->s);
		if(e->hint != nil)
			o->hint = s_clone(e->hint);
		return o;
	}
	return nil;
}

/*
 * binary data
 */

static String*
_b_new(void *a, uint alen)
{
	String *b;

	/* doesn't use s_new because that imposes a minimum length */
	b = mallocz(sizeof(*b), 1);
	if(b == nil)
		return nil;
	b->ref = 1;
	b->base = a;
	b->end = b->ptr = b->base + alen;
	return b;
}

String*
b_new(void *a, uint alen)
{
	String *b;
	void *p;

	p = malloc(alen);
	if(p == nil)
		return nil;
	memmove(p, a, alen);
	b = _b_new(p, alen);
	if(b == nil){
		free(p);
		return nil;
	}
	return b;
}

String*
b_copy(String *b)
{
	return b_new(b->base, s_len(b));
}

String*
b_unique(String *b)
{
	String *n;

	if(b->ref == 1)
		return b;
	n = b_new(b->base, s_len(b));
	if(n == nil)
		return nil;
	s_free(b);
	return n;
}

static uchar*
basedec(Rd *rd, int (*f)(uchar*, int, char*, int), String *s, uint *length)
{
	uchar *b;
	int lim;

	lim = s_len(s)*3/4+1;
	b = ck(rd, malloc(lim));
	lim = f(b, lim-1, s_to_c(s), s_len(s));
	if(lim < 0){
		free(b);
		synerr(rd, "corrupt encoded data", Here);
	}
	b[lim] = 0;
	*length = lim;
	return b;
}

static void*
ck(Rd *rd, void *a)
{
	if(a == nil && rd != nil)
		synerr(rd, "out of memory", Here);
	return a;
}
