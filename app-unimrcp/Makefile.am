MAINTAINERCLEANFILES 	      = Makefile.in

INCLUDES                      = -I../include $(UNIMRCP_INCLUDES) $(ASTERISK_INCLUDES)

mod_LTLIBRARIES               = app_unimrcp.la

app_unimrcp_la_SOURCES = app_unimrcp.c
app_unimrcp_la_LDFLAGS = -avoid-version -no-undefined -module

app_unimrcp_la_LIBADD  = $(UNIMRCP_LIBS)


load: 
	asterisk -rx "module load app_unimrcp.so"

unload: 
	asterisk -rx "module unload app_unimrcp.so"
