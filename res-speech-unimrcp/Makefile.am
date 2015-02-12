MAINTAINERCLEANFILES          = Makefile.in

AM_CPPFLAGS                   = -I$(top_srcdir)/include $(UNIMRCP_INCLUDES) $(ASTERISK_INCLUDES)

mod_LTLIBRARIES               = res_speech_unimrcp.la

res_speech_unimrcp_la_SOURCES = res_speech_unimrcp.c

res_speech_unimrcp_la_LDFLAGS = -avoid-version -no-undefined -module

res_speech_unimrcp_la_LIBADD  = $(UNIMRCP_LIBS)

install-data-local:
	test -d $(DESTDIR)$(asterisk_conf_dir) || $(mkinstalldirs) $(DESTDIR)$(asterisk_conf_dir)
	test -f $(DESTDIR)$(asterisk_conf_dir)/res-speech-unimrcp.conf || $(INSTALL) -m 644 $(top_srcdir)/conf/res-speech-unimrcp.conf $(DESTDIR)$(asterisk_conf_dir)

load: 
	asterisk -rx "module load res_speech_unimrcp.so"

unload: 
	asterisk -rx "module unload res_speech_unimrcp.so"
