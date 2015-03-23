libsexp - S-expression library for Plan 9

*Charles.Forsyth@gmail.com (www.terzarima.net)*

* An S-expression library for Plan 9, based on my older Inferno module.
  The manual pages are in the **man** directory: *sexp(2)* defines the interface; *sexprs(6)* defines the S-expression syntax used.
  It is based on the variant defined by Rivest in Internet Draft **draft-rivest-sexp-00.txt** (4 May 1997) for use by SPKI.
* Version 20150323

### Installation ###

* Put the source in some suitable place, perhaps $home/src/libsexp.
* **cd $home/src/libsexp; mk** 
  will make **libsexp.a$O**
* **mk install**
  will make and copy **libsexp.a$O** to **$LIBDIR**, which is . by default.
  To select a different location, change **mkfile** to set LIBDIR, or use **LIBDIR=***/my/libdir* **mk install**
* **mk clean**
  will remove intermediate object files
* **mk nuke**
  will remove all output files

As usual for Plan 9 source, simply set objtype=... to select a target library architecture different from $cputype.

Copy **sexp.h** to some suitable include directory.

### After installation ###

* **mk tests**
  will make a test program for $cputype and run it. There should be no mismatches noted by **cmp**.
* Report problems through the issue system at [https://bitbucket.org/forsyth/libsexp](https://bitbucket.org/forsyth/libsexp)
