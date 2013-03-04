macrodir             = @ac_macro_dir@
auxdir               = @ac_aux_dir@
AUX_DIST             = $(auxdir)/config.guess \
                       $(auxdir)/config.sub \
                       $(auxdir)/install-sh \
                       $(auxdir)/ltconfig \
                       $(auxdir)/ltmain.sh \
                       $(auxdir)/depcomp \
                       $(auxdir)/missing
EXTRA_DIST           = bootstrap

AUTOMAKE_OPTIONS     = foreign
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure $(AUX_DIST)

ACLOCAL              = aclocal -I $(macrodir)

SUBDIRS              =

if RES_SPEECH_UNIMRCP
SUBDIRS              += res-speech-unimrcp
endif

if APP_UNIMRCP
SUBDIRS              += app-unimrcp
endif

install-data-local:
	test -f $(asterisk_conf_dir)/res-speech-unimrcp.conf || $(INSTALL) -m 644 conf/res-speech-unimrcp.conf $(asterisk_conf_dir)
	test -f $(asterisk_conf_dir)/mrcp.conf || $(INSTALL) -m 644 conf/mrcp.conf $(asterisk_conf_dir)
