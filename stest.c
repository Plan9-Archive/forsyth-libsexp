#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>
#include "sexprs.h"

Biobuf	bin;

void
main(int argc, char **argv)
{
	char *s, *es;
	Sexp *e, *e64;
	String *b64;

	ARGBEGIN{
	}ARGEND
	quotefmtinstall();

	Binit(&bin, 0, OREAD);
	while((s = Brdstr(&bin, '\n', 1)) != nil){
		e = se_parse(s, &es);
		if(e == nil){
			print("err: %r\n");
			continue;
		}
		b64 = se_b64text(e);
		e64 = se_parse(s_to_c(b64), nil);
		if(e64 == nil)
			print("b64 err: %r\n");
		print("%s :: %q		%q\n	-> %s\n", s_to_c(se_text(e)), es, s_to_c(b64), s_to_c(se_text(e64)));
		if(se_eq(e, e64))
			print("	equal\n");
		else
			print("	not equal\n");
		s_free(b64);
		se_free(e64);
		se_free(e);
	}
	e = se_form("a", se_form("b", se_form("c", nil), se_str("239329"), nil), se_list(nil), nil);
	print("-> %s\n", s_to_c(se_text(e)));
	exits(nil);
}
