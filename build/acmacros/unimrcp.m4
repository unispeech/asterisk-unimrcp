dnl FIND_UNIMRCP: find where UniMRCP is located (or use bundled)
AC_DEFUN([FIND_UNIMRCP], [

    AC_MSG_NOTICE([UniMRCP configuration])
    
    AC_ARG_WITH([unimrcp],
	[  --with-unimrcp=DIR      specify UniMRCP location, or 'builtin'],
	[unimrcp_dir=$withval],
        [unimrcp_dir="/usr/local/unimrcp"])

    dnl whether unimrcp_dir points to source or installed dir
    is_source_dir=1

    if test "$unimrcp_dir" = "builtin"; then
	dnl bundled (builtin) subdir
        unimrcp_dir="`pwd`/unimrcp"
    else
        unimrcp_config=$unimrcp_dir/lib/pkgconfig/unimrcpclient.pc
        if test -f $unimrcp_config; then
	    is_source_dir=0
        fi
    fi

    if test $is_source_dir = 1; then
	AC_MSG_ERROR([Only installed dir location is supported now])
    else
	UNIMRCP_INCLUDES="`pkg-config --cflags $unimrcp_config`"
	UNIMRCP_LIBS="`pkg-config --libs $unimrcp_config`"
	uni_version="`pkg-config --modversion $unimrcp_config`"
    fi

    AC_MSG_RESULT([$uni_version])

    AC_SUBST(UNIMRCP_INCLUDES)
    AC_SUBST(UNIMRCP_LIBS)
])
