//#pragma src "/sys/src/libsexp"
//#pragma lib "libsexp.a"

/*
 * S-expressions following Rivest's Network Working Group Internet Draft, 4 May 1997
 * (as used in SDSI/SPKI). The draft specifies the S-expressions, not the interface,
 * which is modelled on Inferno's, but unique to Plan 9, and similar in style to String(2).
 */

typedef struct Sexp Sexp;

enum{
	Sstring,
	Sbinary,
	Slist,
};

struct Sexp {
	Lock;
	int	inuse;
	int	tag;
	union{
		struct{	/* atom (Sstring or Sbinary) */
			String*	s;	/* Sstring */
			String*	hint;
		};
		struct{	/* list element */
			Sexp*	hd;	/* datum, or nil for empty list */
			Sexp*	tl;	/* further Slist, or nil */
		};
	};
};

String*	b_new(void*, uint);
String*	b_copy(String*);
String*	b_unique(String*);

Sexp*	se_string(String*);
Sexp*	se_str(char*);
Sexp*	se_data(uchar*, uint);
Sexp*	se_binary(String*);
Sexp*	se_cons(Sexp*, Sexp*);
Sexp*	se_list(Sexp*, ...);	/* bad idea? */
Sexp*	se_form(char*, Sexp*, ...);
Sexp*	se_parse(char*, char**);
Sexp*	se_unpack(char*, uint, char**);
String*	se_text(Sexp*);
uint	se_packedsize(Sexp*);
uint	se_pack(uchar*, uint, Sexp*);
String*	se_b64text(Sexp*);
Sexp*	se_incref(Sexp*);
Sexp*	se_unique(Sexp*);
void	se_free(Sexp*);

Sexp*	se_hd(Sexp*);	/* returns head */
Sexp*	se_tl(Sexp*);	/* returns tail */

int	se_islist(Sexp*);
int	se_len(Sexp*);	/* list length */
Sexp*	se_els(Sexp*);	/* list of elements */
char*	se_op(Sexp*);		/* string value of head of list, if string */
Sexp*	se_args(Sexp*);	/* list of elements following op */
int	se_eq(Sexp*, Sexp*);	/* recursive comparison */
Sexp*	se_copy(Sexp*);	/* recursive copy */
String*	se_asdata(Sexp*);
String*	se_astext(Sexp*);

#ifdef BGETC
Sexp*	se_read(Biobuf*);
#endif
