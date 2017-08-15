dnl FIND_ASTERISK: find where Asterisk is located
AC_DEFUN([FIND_ASTERISK], [

    AC_MSG_NOTICE([Asterisk configuration])

    AC_ARG_WITH([asterisk],
        [  --with-asterisk=DIR         specify location of Asterisk],
        [asterisk_dir=$withval],
        [asterisk_dir=""])

    AC_ARG_WITH([asterisk-conf],
        [  --with-asterisk-conf=DIR    specify location of Asterisk config dir],
        [asterisk_conf_dir=$withval],
        [asterisk_conf_dir=""])

    AC_ARG_WITH([asterisk-doc],
        [  --with-asterisk-doc=DIR     specify location of Asterisk doc dir],
        [asterisk_xmldoc_dir=$withval],
        [asterisk_xmldoc_dir=""])

    AC_ARG_WITH([asterisk-version],
        [  --with-asterisk-version=VER specify version of Asterisk],
        [asterisk_version=$withval],
        [asterisk_version=""])

    dnl Test and set Asterisk directories.
    if test "${asterisk_dir}" = ""; then
        asterisk_dir="/usr"
        if test "${asterisk_conf_dir}" = ""; then
            asterisk_conf_dir="/etc/asterisk"
        fi
        if test "${asterisk_xmldoc_dir}" = ""; then
            asterisk_xmldoc_dir="/var/lib/asterisk/documentation/thirdparty"
        fi
    else
        if test "${asterisk_conf_dir}" = ""; then
            asterisk_conf_dir="${asterisk_dir}/etc/asterisk"
        fi
        if test "${asterisk_xmldoc_dir}" = ""; then
            asterisk_xmldoc_dir="${asterisk_dir}/var/lib/asterisk/documentation/thirdparty"
        fi
    fi
    asterisk_mod_dir="${asterisk_dir}/lib/asterisk/modules"
    asterisk_include_dir="${asterisk_dir}/include"

    dnl Determine Asterisk version.
    if test "${asterisk_version}" = ""; then
        if test -f "$asterisk_dir/sbin/asterisk"; then
            asterisk_version=$($asterisk_dir/sbin/asterisk -V | grep Asterisk | cut -d' ' -f2)
        else
            if test -f "$asterisk_include_dir/asterisk/version.h"; then
               echo "Asterisk binary not found, using version.h to determine version"
               asterisk_version=$(cat $asterisk_include_dir/asterisk/version.h | sed -n 's/#define ASTERISK_VERSION "\(.*\)"/\1/p')
            fi
        fi
    fi

    if test "${asterisk_version}" = ""; then
        AC_MSG_ERROR([Could not determine Asterisk version, please explicitly specify version string using the option --with-asterisk-version=major.minor.patch])
    fi

    AC_MSG_RESULT([$asterisk_version])

    dnl Strip off trailing characters, if present.
    asterisk_version=$(echo $asterisk_version | cut -d- -f1 | cut -d~ -f1)

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

    dnl Test Asterisk include directory and header file.
    if test -f "$asterisk_include_dir/asterisk.h"; then
        if test -d "$asterisk_include_dir/asterisk"; then
            ASTERISK_INCLUDES="-I$asterisk_include_dir"
        else
            AC_MSG_ERROR([Could not find Asterisk include directory, make sure Asterisk development package is installed])
        fi
    else
        AC_MSG_ERROR([Could not find asterisk.h, make sure Asterisk development package is installed])
    fi

    dnl Set Asterisk includes, conf and xmldoc directories.
    AC_SUBST(ASTERISK_INCLUDES)
    AC_SUBST(asterisk_conf_dir)
    AC_SUBST(asterisk_xmldoc_dir)
])
