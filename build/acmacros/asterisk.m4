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
        [  --with-asterisk-version=VER specify Asterisk version],
        [asterisk_version=$withval],
        [asterisk_version=""])

    dnl Test and set Asterisk directory.
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

    dnl Determine Asterisk version.
    if test "${asterisk_version}" = ""; then
        if test -f "$asterisk_dir/sbin/asterisk"; then
            asterisk_version=$($asterisk_dir/sbin/asterisk -V | grep Asterisk | cut -d' ' -f2)
        else
            echo "Asterisk binary not found, using version.h to detect version"
            asterisk_version=$(cat $asterisk_dir/include/asterisk/version.h | sed -n 's/#define ASTERISK_VERSION "\(.*\)"/\1/p')
        fi
    fi

    if test "${asterisk_version}" = ""; then
        AC_MSG_ERROR([Could not determine Asterisk version, please explicitly specify version string using the option --with-asterisk-version=major.minor.patch])
    fi

    AC_MSG_RESULT([$asterisk_version])

    case $asterisk_version in
        SVN*)
            dnl The version number is supposed to be explicitly specified in case an SVN revision of Asterisk is used.
            AC_MSG_ERROR([Could not determine major, minor, and patch version numbers of Asterisk, please explicitly specify version string using the option --with-asterisk-version=major.minor.patch])
            ;;
    esac

    dnl Set major, minor, and patch version numbers.
    ASTERISK_MAJOR_VERSION=$(echo $asterisk_version | cut -d. -f1)
    ASTERISK_MINOR_VERSION=$(echo $asterisk_version | cut -d. -f2)
    ASTERISK_PATCH_VERSION=$(echo $asterisk_version | cut -d. -f3)

    AC_DEFINE_UNQUOTED(ASTERISK_MAJOR_VERSION, $ASTERISK_MAJOR_VERSION)
    AC_DEFINE_UNQUOTED(ASTERISK_MINOR_VERSION, $ASTERISK_MINOR_VERSION)
    AC_DEFINE_UNQUOTED(ASTERISK_PATCH_VERSION, $ASTERISK_PATCH_VERSION)

    dnl Set Asterisk includes, conf and xmldoc directories.
    ASTERISK_INCLUDES="-I$asterisk_dir/include"
    AC_SUBST(ASTERISK_INCLUDES)
    AC_SUBST(asterisk_conf_dir)
    AC_SUBST(asterisk_xmldoc_dir)
])
