</$objtype/mkfile

LIB=libsexp.a$O
CLEANFILES=$O.stest
OFILES=\
	sexprs.$O\

HFILES=\
	sexprs.h\

</sys/src/cmd/mklib

$O.stest:	stest.$O $LIB
	$LD -o $target $prereq

tests:V:	$O.stest
	$O.stest <Tests | cmp /fd/0 Test-out
