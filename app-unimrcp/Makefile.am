MAINTAINERCLEANFILES   = Makefile.in

AM_CPPFLAGS            = -I../include $(UNIMRCP_INCLUDES) $(ASTERISK_INCLUDES)

mod_LTLIBRARIES        = app_unimrcp.la

app_unimrcp_la_SOURCES = audio_queue.c \
                         speech_channel.c \
                         ast_unimrcp_framework.c \
                         recog_datastore.c \
                         app_mrcpsynth.c \
                         app_mrcprecog.c \
                         app_synthandrecog.c \
                         app_unimrcp.c
app_unimrcp_la_LDFLAGS = -avoid-version -no-undefined -module
app_unimrcp_la_LIBADD  = $(UNIMRCP_LIBS)

XMLDOC_FILES           = app_mrcpsynth.c \
                         app_mrcprecog.c \
                         app_synthandrecog.c \
                         recog_datastore.c

all-local: .xmldocs/app_unimrcp-en_US.xml

.xmldocs/app_unimrcp-en_US.xml: $(XMLDOC_FILES)
	mkdir -p .xmldocs
	@printf "Building Documentation: $@"
	@echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" > $@
	@echo "<!DOCTYPE docs SYSTEM \"appdocsxml.dtd\">" >> $@
	@echo "<docs xmlns:xi=\"http://www.w3.org/2001/XInclude\">" >> $@
	@for i in $(XMLDOC_FILES); do \
		$(AWK) -f ../build/xmldocs/get_documentation $$i >> $@ ; \
	done ;
	@echo
	@echo "</docs>" >> $@ 

clean-local:
	rm -rf .xmldocs

install-data-local:
	$(INSTALL) -m 644 .xmldocs/*.xml $(DESTDIR)$(asterisk_xmldoc_dir)
	test -f $(DESTDIR)$(asterisk_conf_dir)/mrcp.conf || $(INSTALL) -m 644 ../conf/mrcp.conf $(DESTDIR)$(asterisk_conf_dir)

load: 
	asterisk -rx "module load app_unimrcp.so"

unload: 
	asterisk -rx "module unload app_unimrcp.so"
