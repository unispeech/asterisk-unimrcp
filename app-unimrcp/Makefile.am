MAINTAINERCLEANFILES   = Makefile.in

INCLUDES               = -I../include $(UNIMRCP_INCLUDES) $(ASTERISK_INCLUDES)

mod_LTLIBRARIES        = app_unimrcp.la

app_unimrcp_la_SOURCES = app_unimrcp.c
app_unimrcp_la_LDFLAGS = -avoid-version -no-undefined -module

app_unimrcp_la_LIBADD  = $(UNIMRCP_LIBS)

all-local: .xmldocs/app_unimrcp-en_US.xml

.xmldocs/app_unimrcp-en_US.xml: app_unimrcp.c
	mkdir -p .xmldocs
	@printf "Building Documentation: $@"
	@echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" > $@
	@echo "<!DOCTYPE docs SYSTEM \"appdocsxml.dtd\">" >> $@
	@echo "<docs xmlns:xi=\"http://www.w3.org/2001/XInclude\">" >> $@
	@for i in *.c; do \
		$(AWK) -f ../build/xmldocs/get_documentation $$i >> $@ ; \
	done ;
	@echo
	@echo "</docs>" >> $@ 

clean-local:
	rm -rf .xmldocs

install-data-local:
	$(INSTALL) -m 644 .xmldocs/*.xml $(asterisk_xmldoc_dir)

load: 
	asterisk -rx "module load app_unimrcp.so"

unload: 
	asterisk -rx "module unload app_unimrcp.so"
