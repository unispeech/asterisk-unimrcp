dnl FIND_ASTERISK: find where Asterisk is located
AC_DEFUN([FIND_ASTERISK], [

    AC_MSG_NOTICE([Asterisk configuration])

    AC_ARG_WITH([asterisk],
        [  --with-asterisk=DIR         specify Asterisk location],
        [asterisk_dir=$withval],
        [asterisk_dir=""])

    AC_ARG_WITH([asterisk-conf],
        [  --with-asterisk-conf=DIR    specify Asterisk config location],
        [asterisk_conf_dir=$withval],
        [asterisk_conf_dir=""])

    AC_ARG_WITH([asterisk-version],
        [  --with-asterisk-version=DIR specify Asterisk version],
        [asterisk_version=$withval],
        [asterisk_version=""])

    dnl Test and set Asterisk directory
    if test "${asterisk_dir}" = ""; then
        asterisk_dir="/usr"
        if test "${asterisk_conf_dir}" = ""; then
            asterisk_conf_dir="/etc/asterisk"
        fi
        asterisk_xmldoc_dir="/var/lib/asterisk/documentation/thirdparty"
    else
        if test "${asterisk_conf_dir}" = ""; then
            asterisk_conf_dir="${asterisk_dir}/etc/asterisk"
        fi
        asterisk_xmldoc_dir="${asterisk_dir}/var/lib/asterisk/documentation/thirdparty"
    fi

    dnl Detect Asterisk version
    if test "${asterisk_version}" = ""; then
        if test -f "$asterisk_dir/sbin/asterisk"; then
            asterisk_version=$($asterisk_dir/sbin/asterisk -V | grep Asterisk | cut -d' ' -f2)
        else
            echo "Asterisk binary not found, using version.h to detect version"
            asterisk_version=$(cat $asterisk_dir/include/asterisk/version.h | sed -n 's/#define ASTERISK_VERSION "\(.*\)"/\1/p')
        fi
    fi

    AC_MSG_RESULT([$asterisk_version])

    case $asterisk_version in
        SVN*)
            dnl If using an SVN revision, the version number of Asterisk is supposed to be explicitly specified.
            AC_MSG_ERROR([Could not detect Asterisk version, please explicitly specify it as follows: --with-asterisk-version=major.minor.patch])
            ;;
    esac

    dnl Set major, minor, and patch version numbers
    ASTERISK_MAJOR_VERSION=$(echo $asterisk_version | cut -d. -f1)
    ASTERISK_MINOR_VERSION=$(echo $asterisk_version | cut -d. -f2)
    ASTERISK_PATCH_VERSION=$(echo $asterisk_version | cut -d. -f3)

    AC_DEFINE_UNQUOTED(ASTERISK_MAJOR_VERSION, $ASTERISK_MAJOR_VERSION)
    AC_DEFINE_UNQUOTED(ASTERISK_MINOR_VERSION, $ASTERISK_MINOR_VERSION)
    AC_DEFINE_UNQUOTED(ASTERISK_PATCH_VERSION, $ASTERISK_PATCH_VERSION)

    dnl Original (legacy) definitions
    case $asterisk_version in
        SVN-*1.2*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK12)
            AC_DEFINE([ASTERISK12], [], [Asterisk 1.2])
            ;;
        1.2*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK12)
            AC_DEFINE([ASTERISK12], [], [Asterisk 1.2])
            ;;
        SVN-*1.4*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK14)
            AC_DEFINE([ASTERISK14], [], [Asterisk 1.4])
            ;;
        1.4*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK14)
            AC_DEFINE([ASTERISK14], [], [Asterisk 1.4])
            ;;
        SVN-*1.6.0.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK160)
            AC_DEFINE([ASTERISK160], [], [Asterisk 1.6.0])
            ;;
        1.6.0.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK160)
            AC_DEFINE([ASTERISK160], [], [Asterisk 1.6.0])
            ;;
        SVN-*1.6.1.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK161)
            AC_DEFINE([ASTERISK161], [], [Asterisk 1.6.1])
            ;;
        1.6.1.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK161)
            AC_DEFINE([ASTERISK161], [], [Asterisk 1.6.1])
            ;;
        SVN-*1.6.2.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK162)
            AC_DEFINE([ASTERISK162], [], [Asterisk 1.6.2])
            ;;
        1.6.2.*)
            dnl AC_DEFINE_UNQUOTED(ASTERISK162)
            AC_DEFINE([ASTERISK162], [], [Asterisk 1.6.2])
            ;;
        SVN-trunk*)
            dnl AC_DEFINE_UNQUOTED(ASTERISKSVN)
            AC_DEFINE([ASTERISKSVN], [], [Asterisk SVN])
            ;;
        SVN*)
            dnl AC_DEFINE_UNQUOTED(ASTERISKSVN)
            AC_DEFINE([ASTERISKSVN], [], [Asterisk SVN])
            ;;
        *)
            dnl AC_DEFINE_UNQUOTED(ASTERISKOTHER)
            AC_DEFINE([ASTERISKOTHER], [], [Asterisk Other])
            ;;
    esac

    ASTERISK_INCLUDES="-I$asterisk_dir/include"
    AC_SUBST(ASTERISK_INCLUDES)
    AC_SUBST(asterisk_conf_dir)
])
