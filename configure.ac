dnl
dnl Autoconf configuration file for the UniMRCP modules for Asterisk.
dnl
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([asterisk-unimrcp],[1.2.0])

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/acmacros])

dnl Set ac_macro_dir variable manually for autoconf 2.61 and above.
ac_macro_dir="build/acmacros"

AC_SUBST(ac_aux_dir)
AC_SUBST(ac_macro_dir)

dnl Generate ./config.nice to reuse ./configure command-line.
APR_CONFIG_NICE(config.nice)

dnl Include m4 macros for libtool.
sinclude(build/acmacros/libtool.m4)
sinclude(build/acmacros/ltoptions.m4)
sinclude(build/acmacros/ltsugar.m4)
sinclude(build/acmacros/ltversion.m4)
sinclude(build/acmacros/lt~obsolete.m4)

AM_INIT_AUTOMAKE([no-define nostdinc foreign])

dnl Enable silent build rules available since automake 1.11.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Set default language.
AC_LANG_C

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

dnl Skip detection of Fortran.
m4_undefine([AC_PROG_F77])
m4_defun([AC_PROG_F77],[])

dnl Turn off static libraries.
AC_DISABLE_STATIC

dnl Add libtool support.
AC_PROG_LIBTOOL

dnl Do not use autoconf generated compiler DEFS.
rm confdefs.h
touch confdefs.h

dnl Check for C compiler vendor.
AX_COMPILER_VENDOR

dnl Search for pkg-config.
AC_PATH_PROG(PKG_CONFIG, pkg-config)

FIND_UNIMRCP
FIND_ASTERISK

if test "x$prefix" = "xNONE"; then
    prefix="${asterisk_mod_dir}"
fi

moddir="${prefix}"

AC_SUBST(moddir)

dnl Enable maintainer mode.
AC_ARG_ENABLE(maintainer-mode,
    [AC_HELP_STRING([--enable-maintainer-mode  ],[turn on debugging and compile time warnings])],
    [enable_maintainer_mode="$enableval"],
    [enable_maintainer_mode="no"])

AC_MSG_NOTICE([enable maintainer mode: $enable_maintainer_mode])
if test "${enable_maintainer_mode}" != "no"; then
    APR_ADDTO(CFLAGS,-g)
    if test "x${ax_cv_c_compiler_vendor}" = "xgnu"; then
        APR_ADDTO(CFLAGS,-Wall -Werror)
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

echo
echo '****************************** REPORT ******************************'
echo
echo Asterisk version.............. : $asterisk_version
echo UniMRCP version............... : $uni_version
echo UniMRCP modules version....... : $PACKAGE_VERSION
echo
echo Compiler...................... : $CC
echo Compiler flags................ : $CFLAGS
echo Preprocessor definitions...... : $CPPFLAGS
echo Linker flags.................. : $LDFLAGS
echo
echo Modules install path...........: $prefix
echo Configuration install path.....: $asterisk_conf_dir
echo XML doc install path...........: $asterisk_xmldoc_dir
echo
echo Speech resource module.........: $enable_res_speech_unimrcp
echo Application module.............: $enable_app_unimrcp
echo
echo '********************************************************************'
