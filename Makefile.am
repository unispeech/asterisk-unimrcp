macrodir             = @ac_macro_dir@
auxdir               = @ac_aux_dir@

AUX_DIST             = $(auxdir)/config.guess \
                       $(auxdir)/config.sub \
                       $(auxdir)/install-sh \
                       $(auxdir)/ltconfig \
                       $(auxdir)/ltmain.sh \
                       $(auxdir)/depcomp \
                       $(auxdir)/missing

MACRO_DIST           = $(macrodir)/libtool.m4 \
                       $(macrodir)/ltoptions.m4 \
                       $(macrodir)/ltsugar.m4 \
                       $(macrodir)/ltversion.m4 \
                       $(macrodir)/lt~obsolete.m4

EXTRA_DIST           = bootstrap

AUTOMAKE_OPTIONS     = foreign
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config.nice $(AUX_DIST) $(MACRO_DIST)

ACLOCAL              = aclocal -I $(macrodir)

SUBDIRS              =

if RES_SPEECH_UNIMRCP
SUBDIRS              += res-speech-unimrcp
endif

if APP_UNIMRCP
SUBDIRS              += app-unimrcp
endif
