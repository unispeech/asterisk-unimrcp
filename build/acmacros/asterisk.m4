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
    	asterisk_version=$(/usr/sbin/asterisk -V | cut -d' ' -f2)
    else
	asterisk_version=$($asterisk_dir/sbin/asterisk -V | cut -d' ' -f2)
    fi

    AC_MSG_RESULT([$asterisk_version])

    case $asterisk_version in
        1.4*)
	        AC_DEFINE_UNQUOTED(ASTERISK14)
                ;;
        1.6.0.*)
	        AC_DEFINE_UNQUOTED(ASTERISK160)
                ;;
        1.6.1.*)
	        AC_DEFINE_UNQUOTED(ASTERISK161)
                ;;
        1.6.2.*)
	        AC_DEFINE_UNQUOTED(ASTERISK162)
                ;;
        SVN*)
	        AC_DEFINE_UNQUOTED(ASTERISK162)
                ;;
        *)
	        AC_DEFINE_UNQUOTED(ASTERISK162)
                ;;
    esac

    AC_SUBST(ASTERISK_INCLUDES)
])
