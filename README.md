libsexp - S-expression library for Plan 9
*Charles.Forsyth@gmail.com (www.terzarima.net)*

* Quick summary
* Version 20150323

### Installation ###

* Put the source in some suitable place, perhaps $home/src/libsexp.
* **cd $home/src/libsexp; mk** 
  will make **libsexp.a$O**
* **mk install**
  will make and copy libsexp.a$O to $LIBDIR, which is . by default.
  To select a different location, change mkfile to set LIBDIR, or use LIBDIR=/my/libdir mk install

As usual for Plan 9 source, simply set objtype=... to select a target library architecture different from $cputype.

### After installation ###

* **mk tests**
  will make a test program for $cputype and run it. There should be no mismatches noted by **cmp**.
* Report problems through the issue system at [https://bitbucket.org/forsyth/libsexp](https://bitbucket.org/forsyth/libsexp)
