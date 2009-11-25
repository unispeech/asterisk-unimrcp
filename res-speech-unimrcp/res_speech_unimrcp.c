/* 
 * Asterisk UniMRCP Speech Module
 *
 * Copyright (C) 2009, Arsen Chaloyan  <achaloyan@gmail.com>
 *
 */

/*** MODULEINFO
	<depend>unimrcp</depend>
 ***/

#include "asterisk.h"
#define AST_MODULE "res_speech_unimrcp" 
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/strings.h>
#include <asterisk/config.h>
#include <asterisk/frame.h>
#include <asterisk/dsp.h>
#include <asterisk/speech.h>

#include <apr_thread_cond.h>
#include <apr_thread_proc.h>
#include <unimrcp_client.h>
#include <mrcp_application.h>
#include <mrcp_message.h>
#include <mrcp_generic_header.h>
#include <mrcp_recog_header.h>
#include <mrcp_recog_resource.h>
#include <mpf_frame_buffer.h>
#include <apt_pool.h>
#include <apt_log.h>


#define UNI_ENGINE_NAME "unimrcp"

/** \brief Forward declaration of speech */
typedef struct uni_speech_t uni_speech_t;
/** \brief Forward declaration of engine */
typedef struct uni_engine_t uni_engine_t;

/** \brief Enumeration of session management request types */
typedef enum {
	UNI_SMR_NONE,
	UNI_SMR_ADD_CHANNEL,
	UNI_SMR_REMOVE_CHANNEL,
	UNI_SMR_UPDATE_SESSION,
	UNI_SMR_TERMINATE_SESSION
} uni_smr_type_e;

/** \brief Declaration of UniMRCP based speech structure */
struct uni_speech_t {
	/* Client session */
	mrcp_session_t        *session;
	/* Client channel */
	mrcp_channel_t        *channel;
	/* Asterisk speech base */
	struct ast_speech     *speech_base;

	/* Conditional wait object */
	apr_thread_cond_t     *wait_object;
	/* Mutex of the wait object */
	apr_thread_mutex_t    *mutex;

	/* Buffer of media frames */
	mpf_frame_buffer_t    *media_buffer;

	/* Active grammar name (Content-ID) */
	const char            *active_grammar_name;
	
	/* In-progress session management request */
	uni_smr_type_e         session_request;
	/* Satus code of session management response */
	mrcp_sig_status_code_e session_response;
	
	/* In-progress request sent to server */
	mrcp_message_t        *mrcp_request;
	/* Response received from server */
	mrcp_message_t        *mrcp_response;
	/* Event received from server */
	mrcp_message_t        *mrcp_event;
};

/** \brief Declaration of UniMRCP based recognition engine */
struct uni_engine_t {
	/* Memory pool */
	apr_pool_t         *pool;
	/* Client stack instance */
	mrcp_client_t      *client;
	/* Application instance */
	mrcp_application_t *application;
};

static struct uni_engine_t uni_engine;



/** \brief Set up the speech structure within the engine */
static int uni_recog_create(struct ast_speech *speech, int format)
{
	return 0;
}

/** \brief Destroy any data set on the speech structure by the engine */
static int uni_recog_destroy(struct ast_speech *speech)
{
	return 0;
}

/** \brief Load a local grammar on the speech structure */
static int uni_recog_load_grammar(struct ast_speech *speech, char *grammar_name, char *grammar)
{
	return 0;
}

/** \brief Unload a local grammar */
static int uni_recog_unload_grammar(struct ast_speech *speech, char *grammar_name)
{
	return 0;
}

/** \brief Activate a loaded grammar */
static int uni_recog_activate_grammar(struct ast_speech *speech, char *grammar_name)
{
	return 0;
}

/** \brief Deactivate a loaded grammar */
static int uni_recog_deactivate_grammar(struct ast_speech *speech, char *grammar_name)
{
	return 0;
}

/** \brief Write audio to the speech engine */
static int uni_recog_write(struct ast_speech *speech, void *data, int len)
{
	return 0;
}

/** \brief Signal DTMF was received */
static int uni_recog_dtmf(struct ast_speech *speech, const char *dtmf)
{
	return 0;
}

/** brief Prepare engine to accept audio */
static int uni_recog_start(struct ast_speech *speech)
{
	return 0;
}

/** \brief Change an engine specific setting */
static int uni_recog_change(struct ast_speech *speech, char *name, const char *value)
{
	return 0;
}

/** \brief Change the type of results we want back */
static int uni_recog_change_results_type(struct ast_speech *speech,enum ast_speech_results_type results_type)
{
	return -1;
}

/** \brief Try to get result */
struct ast_speech_result* uni_recog_get(struct ast_speech *speech)
{
	return speech->results;
}

/** \brief Received session update response */
static apt_bool_t on_session_update(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	return TRUE;
}

/** \brief Received session termination response */
static apt_bool_t on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	return TRUE;
}

/** \brief Received channel add response */
static apt_bool_t on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	return TRUE;
}

/** \brief Received channel remove response */
static apt_bool_t on_channel_remove(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	return TRUE;
}

/** \brief Received MRCP message */
static apt_bool_t on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	return TRUE;
}

/** \brief Received ready event */
static apt_bool_t on_application_ready(mrcp_application_t *application, mrcp_sig_status_code_e status)
{
	return TRUE;
}

/** \brief Received unexpected session/channel termination event */
static apt_bool_t on_terminate_event(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel)
{
	return TRUE;
}

/** \brief Received response to resource discovery request */
static apt_bool_t on_resource_discover(mrcp_application_t *application, mrcp_session_t *session, mrcp_session_descriptor_t *descriptor, mrcp_sig_status_code_e status)
{
	return TRUE;
}

static const mrcp_app_message_dispatcher_t uni_dispatcher = {
	on_session_update,
	on_session_terminate,
	on_channel_add,
	on_channel_remove,
	on_message_receive,
	on_application_ready,
	on_terminate_event,
	on_resource_discover
};

/** \brief UniMRCP message handler */
static apt_bool_t uni_message_handler(const mrcp_app_message_t *app_message)
{
	return mrcp_application_message_dispatch(&uni_dispatcher,app_message);
}

/** \brief Speech engine declaration */
static struct ast_speech_engine ast_engine = { 
    "unimrcp",
    uni_recog_create,
    uni_recog_destroy,
    uni_recog_load_grammar,
    uni_recog_unload_grammar,
    uni_recog_activate_grammar,
    uni_recog_deactivate_grammar,
    uni_recog_write,
    uni_recog_dtmf,
    uni_recog_start,
    uni_recog_change,
    uni_recog_change_results_type,
    uni_recog_get,
    AST_FORMAT_SLINEAR
};

/** \brief Unload UniMRCP engine */
static apt_bool_t uni_engine_unload()
{
	if(uni_engine.client) {
		mrcp_client_destroy(uni_engine.client);
		uni_engine.client = NULL;
	}
	if(uni_engine.pool) {
		apr_pool_destroy(uni_engine.pool);
		uni_engine.pool = NULL;
	}

	/* APR global termination */
	apr_terminate();
	return TRUE;
}

/** \brief Load UniMRCP engine */
static apt_bool_t uni_engine_load()
{
	apr_pool_t *pool;
	apt_dir_layout_t *dir_layout;

	
	/* APR global initialization */
	if(apr_initialize() != APR_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to initialize APR\n");
		return FALSE;
	}

	uni_engine.pool = NULL;
	uni_engine.client = NULL;
	uni_engine.application = NULL;

	pool = apt_pool_create();
	if(!pool) {
		ast_log(LOG_ERROR, "Failed to create APR pool\n");
		uni_engine_unload();
		return FALSE;
	}

	uni_engine.pool = pool;

	dir_layout = apt_default_dir_layout_create(UNIMRCP_DIR_LOCATION,pool);
	/* create singleton logger */
	apt_log_instance_create(APT_LOG_OUTPUT_CONSOLE | APT_LOG_OUTPUT_FILE, APT_PRIO_INFO, pool);
	/* open the log file */
	apt_log_file_open(dir_layout->log_dir_path,"unimrcpclient",MAX_LOG_FILE_SIZE,MAX_LOG_FILE_COUNT,pool);


	uni_engine.client = unimrcp_client_create(dir_layout);
	if(uni_engine.client) {
		uni_engine.application = mrcp_application_create(
										uni_message_handler,
										&uni_engine,
										pool);
		if(uni_engine.application) {
			mrcp_client_application_register(
							uni_engine.client,
							uni_engine.application,
							"ASTMRCP");
		}
	}


	if(!uni_engine.client || !uni_engine.application) {
		ast_log(LOG_ERROR, "Failed to initialize client stack\n");
		uni_engine_unload();
		return FALSE;
	}

	return TRUE;
}

/** \brief Load module */
static int load_module(void)
{
	ast_log(LOG_NOTICE, "Load UniMRCP module\n");

	if(uni_engine_load() == FALSE) {
		return AST_MODULE_LOAD_FAILURE;
	}
	
	if(mrcp_client_start(uni_engine.client) != TRUE) {
		ast_log(LOG_ERROR, "Failed to start client stack\n");
		uni_engine_unload();
		return AST_MODULE_LOAD_FAILURE;
	}

	if(ast_speech_register(&ast_engine)) {
		ast_log(LOG_ERROR, "Failed to register module\n");
		mrcp_client_shutdown(uni_engine.client);
		uni_engine_unload();
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

/** \brief Unload module */
static int unload_module(void)
{
	ast_log(LOG_NOTICE, "Unload UniMRCP module\n");
	if(ast_speech_unregister(UNI_ENGINE_NAME)) {
		ast_log(LOG_ERROR, "Failed to unregister module\n");
	}

	if(uni_engine.client) {
		mrcp_client_shutdown(uni_engine.client);
	}

	uni_engine_unload();
	return 0;
}


AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "UniMRCP Speech Engine");
