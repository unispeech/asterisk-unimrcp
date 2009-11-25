#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)

AC_INIT([asterisk-unimrcp],[0.1.0])

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/acmacros])
AC_PREFIX_DEFAULT(/usr/lib/asterisk/modules)

moddir='${prefix}'

AM_INIT_AUTOMAKE(foreign)

# Set default language
AC_LANG_C

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

# Skip detection of Fortran
m4_undefine([AC_PROG_F77])
m4_defun([AC_PROG_F77],[])
AC_PROG_LIBTOOL

AC_SUBST(moddir)

FIND_UNIMRCP


AC_DEFUN([AX_COMPILER_VENDOR],
[
AC_CACHE_CHECK([for _AC_LANG compiler vendor], ax_cv_[]_AC_LANG_ABBREV[]_compiler_vendor,
 [ax_cv_[]_AC_LANG_ABBREV[]_compiler_vendor=unknown
  # note: don't check for gcc first since some other compilers define __GNUC__
  for ventest in intel:__ICC,__ECC,__INTEL_COMPILER ibm:__xlc__,__xlC__,__IBMC__,__IBMCPP__ gnu:__GNUC__ sun:__SUNPRO_C,__SUNPRO_CC hp:__HP_cc,__HP_aCC dec:__DECC,__DECCXX,__DECC_VER,__DECCXX_VER borland:__BORLANDC__,__TURBOC__ comeau:__COMO__ cray:_CRAYC kai:__KCC lcc:__LCC__ metrowerks:__MWERKS__ sgi:__sgi,sgi microsoft:_MSC_VER watcom:__WATCOMC__ portland:__PGI; do
    vencpp="defined("`echo $ventest | cut -d: -f2 | sed 's/,/) || defined(/g'`")"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM(,[
#if !($vencpp)
      thisisanerror;
#endif
])], [ax_cv_]_AC_LANG_ABBREV[_compiler_vendor=`echo $ventest | cut -d: -f1`; break])
  done
 ])
])

AX_COMPILER_VENDOR

#Enable maintainer mode
AC_ARG_ENABLE(maintainer-mode,
    [AC_HELP_STRING([--enable-maintainer-mode  ],[turn on debugging and compile time warnings])],
    [enable_maintainer_mode="$enableval"],
    [enable_maintainer_mode="no"])

if test "${enable_maintainer_mode}" != "no"; then
    CFLAGS="$CFLAGS -g"
    if test "x${ax_cv_c_compiler_vendor}"  =  "xgnu" ; then
      CFLAGS="$CFLAGS -Wall -Werror"
    fi
fi


AM_CONDITIONAL(ISMAC, [test `uname -s` = Darwin])

AC_CONFIG_FILES([
  Makefile
  res-speech-unimrcp/Makefile
])

AC_OUTPUT
