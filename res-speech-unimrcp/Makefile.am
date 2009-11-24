MAINTAINERCLEANFILES 	= Makefile.in

# Path to UniMRCP dir must be configured next
UNIMRCP_DIR=`pwd`/../unimrcp/

INCLUDES                = -Iinclude \
                          -I$(UNIMRCP_DIR)/platforms/libunimrcp-client/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp-client/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp-signaling/include \
                          -I$(UNIMRCP_DIR)/libs/mrcpv2-transport/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp/message/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp/control/include \
                          -I$(UNIMRCP_DIR)/libs/mrcp/resources/include \
                          -I$(UNIMRCP_DIR)/libs/mpf/include \
                          -I$(UNIMRCP_DIR)/libs/apr-toolkit/include \
                          -I$(UNIMRCP_DIR)/libs/apr/include \
                          -I$(UNIMRCP_DIR)/libs/apr-util/include

plugin_LTLIBRARIES      = res_speech_unimrcp.la

res_speech_unimrcp_la_SOURCES   = res_speech_unimrcp.c
res_speech_unimrcp_la_LDFLAGS   = -module
#res_speech_unimrcp_la_LIBADD    = $(UNIMRCP_DIR)/platforms/libunimrcp-client/libunimrcpclient.la
