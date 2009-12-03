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

    ASTERISKVER=$(echo $asterisk_version | grep -o -E "(^1\.4)|(^1\.6\.[0-9])|(^SVN)" | sed -e 's/\.//g' | sed -e 's/SVN/162/g')

    AC_DEFINE_UNQUOTED(ASTERISK$ASTERISKVER)
    AC_MSG_RESULT([$asterisk_version])

    AC_SUBST(ASTERISK_INCLUDES)
])
