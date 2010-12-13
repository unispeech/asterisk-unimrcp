dnl FIND_ASTERISK: find where Asterisk is located
AC_DEFUN([FIND_ASTERISK], [

    AC_MSG_NOTICE([Asterisk configuration])
    
    AC_ARG_WITH([asterisk],
	[  --with-asterisk=DIR      specify Asterisk location],
	[asterisk_dir=$withval],
        [asterisk_dir=""])

    AC_ARG_WITH([asterisk-conf],
	[  --with-asterisk-conf=DIR      specify Asterisk config location],
	[asterisk_conf_dir=$withval],
        [asterisk_conf_dir=""])

    ASTERISK_INCLUDES="-I$asterisk_dir/include"

    if test -z "${asterisk_dir}"; then
	echo "Asterisk install directory not specified, using /usr"
	if test -f "/usr/sbin/asterisk"; then
 	   	asterisk_version=$(/usr/sbin/asterisk -V | grep Asterisk | cut -d' ' -f2)
	else
		echo "Asterisk binary not found, using version.h to detect version"
		asterisk_version=$(cat /usr/include/asterisk/version.h | sed -n 's/#define ASTERISK_VERSION "\(.*\)"/\1/p')
	fi
    else
	if test -f "$asterisk_dir/sbin/asterisk"; then
 	   	asterisk_version=$($asterisk_dir/sbin/asterisk -V | grep Asterisk | cut -d' ' -f2)
	else
		echo "Asterisk binary not found, using version.h to detect version"
		asterisk_version=$(cat $asterisk_dir/include/asterisk/version.h | sed -n 's/#define ASTERISK_VERSION "\(.*\)"/\1/p')
	fi
    fi

    AC_MSG_RESULT([$asterisk_version])

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
	        dnl AC_DEFINE_UNQUOTED(ASTERISKSVN)
		AC_DEFINE([ASTERISKSVN], [], [Asterisk SVN])
                ;;
    esac

    AC_SUBST(ASTERISK_INCLUDES)
])
