/*
 * Asterisk -- An open source telephony toolkit.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * Please follow coding guidelines 
 * http://svn.digium.com/view/asterisk/trunk/doc/CODING-GUIDELINES
 */

#ifndef AST_UNIMRCP_FRAMEWORK_H
#define AST_UNIMRCP_FRAMEWORK_H

/* UniMRCP includes. */
#include <apr_general.h>
#include <apr_hash.h>
#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>
#include "apt.h"
#include "apt_log.h"
#include "apt_net.h"
#include "apt_pool.h"
#include "unimrcp_client.h"
#include "mrcp_application.h"
#include "mrcp_session.h"
#include "mrcp_message.h"
#include "mrcp_generic_header.h"
#include "mrcp_synth_header.h"
#include "mrcp_synth_resource.h"
#include "mrcp_recog_header.h"
#include "mrcp_recog_resource.h"
#include "uni_version.h"
#include "mrcp_resource_loader.h"
#include "mpf_engine.h"
#include "mpf_codec_manager.h"
#include "mpf_dtmf_generator.h"
#include "mpf_rtp_termination_factory.h"
#include "mrcp_sofiasip_client_agent.h"
#include "mrcp_unirtsp_client_agent.h"
#include "mrcp_client_connection.h"

typedef int (*app_exec_f)(struct ast_channel *chan, ast_app_data data);

/* MRCP application. */
struct ast_mrcp_application_t {
	/* Application name. */
	const char         *name;
	/* Pointer to function which executes the application. */
	app_exec_f          exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	/* Application synopsis. */
	const char         *synopsis;
	/* Application description. */
	const char         *description;
#endif
	/* UniMRCP application. */
	mrcp_application_t *app;
	/* MRCP callbacks from UniMRCP to this module's application. */
	mrcp_app_message_dispatcher_t dispatcher;
	/* Audio callbacks from UniMRCP to this module's application. */
	mpf_audio_stream_vtable_t audio_stream_vtable;
};
typedef struct ast_mrcp_application_t ast_mrcp_application_t;

/* MRCP globals configuration and variables. */
struct ast_mrcp_globals_t {
	/* The memory pool to use. */
	apr_pool_t* pool;

	/* The max-connection-count configuration. */
	char *unimrcp_max_connection_count;
	/* The offer-new-connection configuration. */
	char *unimrcp_offer_new_connection;
	/* The rx-buffer-size configuration. */
	char *unimrcp_rx_buffer_size;
	/* The tx-buffer-size configuration. */
	char *unimrcp_tx_buffer_size;
	/* The reqest timeout configuration. */
	char *unimrcp_request_timeout;
	/* The default text-to-speech profile to use. */
	char *unimrcp_default_synth_profile;
	/* The default speech recognition profile to use. */
	char *unimrcp_default_recog_profile;
	/* Log level to use for the UniMRCP library. */
	char *unimrcp_log_level;

	/* The MRCP client stack. */
	mrcp_client_t *mrcp_client;

	/* The available applications. */
	apr_hash_t    *apps;

	/* Mutex to be used for speech channel numbering. */
	apr_thread_mutex_t *mutex;
	/* Next available speech channel number. */
	apr_uint32_t speech_channel_number;
	/* The available profiles. */
	apr_hash_t *profiles;
};
typedef struct ast_mrcp_globals_t ast_mrcp_globals_t;

/* Profile-specific configuration. This allows us to handle differing MRCP
 * server behavior on a per-profile basis.
 */
struct ast_mrcp_profile_t {
	/* Name of the profile. */
	char *name;
	/* MRCP version of the profile. */
	char *version;
	/* MIME type to use for JSGF grammars. */
	const char *jsgf_mime_type;
	/* MIME type to use for GSL grammars. */
	const char *gsl_mime_type;
	/* MIME type to use for SRGS XML grammars. */
	const char *srgs_xml_mime_type;
	/* MIME type to use for SRGS ABNF grammars. */
	const char *srgs_mime_type;
	/* MIME type to use for SSML (TTS) */
	const char *ssml_mime_type;
	/* The profile configuration. */
	apr_hash_t *cfg;
};
typedef struct ast_mrcp_profile_t ast_mrcp_profile_t;

extern ast_mrcp_globals_t globals;

void globals_destroy(void);

int globals_init(void);

apr_uint32_t get_next_speech_channel_number(void);

ast_mrcp_profile_t* get_synth_profile(const char *option_profile);

ast_mrcp_profile_t* get_recog_profile(const char *option_profile);

int profile_create(ast_mrcp_profile_t **profile, const char *name, const char *version, apr_pool_t *pool);

mrcp_client_t *mod_unimrcp_client_create(apr_pool_t *mod_pool);

int load_mrcp_config(const char *filename, const char *who_asked);

#endif /* AST_UNIMRCP_FRAMEWORK_H */
