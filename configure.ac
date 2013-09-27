dnl 
dnl Autoconf configuration file for the UniMRCP modules for Asterisk.
dnl 
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([asterisk-unimrcp],[1.1.0])

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/acmacros])

dnl Set ac_macro_dir variable manually for autoconf 2.61 and above
ac_macro_dir='build/acmacros'

dnl Include m4 macros for libtool 2
sinclude(build/acmacros/libtool.m4)
sinclude(build/acmacros/ltoptions.m4)
sinclude(build/acmacros/ltsugar.m4)
sinclude(build/acmacros/ltversion.m4)
sinclude(build/acmacros/lt~obsolete.m4)

AM_INIT_AUTOMAKE(foreign)

dnl Enable silent build rules (available since automake 1.11)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Set default language
AC_LANG_C

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

dnl Skip detection of Fortran
m4_undefine([AC_PROG_F77])
m4_defun([AC_PROG_F77],[])
AC_PROG_LIBTOOL

dnl Search for pkg-config
AC_PATH_PROG(PKG_CONFIG, pkg-config)

AC_SUBST(ac_aux_dir)
AC_SUBST(ac_macro_dir)

AC_SUBST(moddir)

FIND_UNIMRCP
FIND_ASTERISK

if test "x$prefix" = "xNONE"; then
    echo "Prefix empty, so using Asterisk directory"
    prefix="${asterisk_dir}/lib/asterisk/modules"
fi

moddir='${prefix}'

AC_DEFUN([AX_COMPILER_VENDOR],
[
AC_CACHE_CHECK([for _AC_LANG compiler vendor], ax_cv_[]_AC_LANG_ABBREV[]_compiler_vendor,
 [ax_cv_[]_AC_LANG_ABBREV[]_compiler_vendor=unknown
  dnl Note: don't check for gcc first since some other compilers define __GNUC__
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

dnl Enable maintainer mode
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

dnl Enable speech resource module (res-speech-unimrcp)
AC_ARG_ENABLE(res-speech-unimrcp,
    [AC_HELP_STRING([--disable-res-speech-unimrcp  ],[exclude UniMRCP speech resource module from build])],
    [enable_res_speech_unimrcp="$enableval"],
    [enable_res_speech_unimrcp="yes"])

    case $asterisk_version in
        SVN-1.2*)
            enable_res_speech_unimrcp="no"
            ;;
        1.2*)
            enable_res_speech_unimrcp="no"
            ;;
    esac

AM_CONDITIONAL([RES_SPEECH_UNIMRCP],[test "${enable_res_speech_unimrcp}" = "yes"])

dnl Enable application module (app-unimrcp)
AC_ARG_ENABLE(app-unimrcp,
    [AC_HELP_STRING([--disable-app-unimrcp  ],[exclude UniMRCP application module from build])],
    [enable_app_unimrcp="$enableval"],
    [enable_app_unimrcp="yes"])

AM_CONDITIONAL([APP_UNIMRCP],[test "${enable_app_unimrcp}" = "yes"])

AM_CONDITIONAL(ISMAC, [test `uname -s` = Darwin])

AC_CONFIG_FILES([
    Makefile
    res-speech-unimrcp/Makefile
    app-unimrcp/Makefile
])

AC_OUTPUT

dnl Report configuration.
echo
echo "Modules install path.............. ${prefix}"
echo "Configuration file install path... ${asterisk_conf_dir}"
echo "Speech resource module............ ${enable_res_speech_unimrcp}"
echo "Application module................ ${enable_app_unimrcp}"
