</$objtype/mkfile

LIB=libsexp.a$O
OFILES=\
	sexprs.$O\

HFILES=\
	sexprs.h\

</sys/src/cmd/mklib

$O.stest:	stest.$O $LIB
	$LD -o $target $prereq
