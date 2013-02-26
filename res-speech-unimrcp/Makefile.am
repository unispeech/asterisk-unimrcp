MAINTAINERCLEANFILES 	      = Makefile.in

INCLUDES                      = -I../include $(UNIMRCP_INCLUDES) $(ASTERISK_INCLUDES)

mod_LTLIBRARIES               = res_speech_unimrcp.la

res_speech_unimrcp_la_SOURCES = res_speech_unimrcp.c

res_speech_unimrcp_la_LDFLAGS = -avoid-version -no-undefined -module

res_speech_unimrcp_la_LIBADD  = $(UNIMRCP_LIBS)


load: 
	asterisk -rx "module load res_speech_unimrcp.so"

unload: 
	asterisk -rx "module unload res_speech_unimrcp.so"
