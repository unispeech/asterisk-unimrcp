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

/*! \file
 *
 * \brief Implementation of the Asterisk's Speech API via UniMRCP
 *
 * \author Arsen Chaloyan arsen.chaloyan@unimrcp.org
 * 
 * \ingroup applications
 */

/* Asterisk includes. */
#include "ast_compat_defs.h"

#define AST_MODULE "res_speech_unimrcp" 
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include <asterisk/module.h>
#include <asterisk/config.h>
#include <asterisk/frame.h>
#include <asterisk/speech.h>

#include <apr_thread_cond.h>
#include <apr_thread_proc.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <unimrcp_client.h>
#include <mrcp_application.h>
#include <mrcp_message.h>
#include <mrcp_generic_header.h>
#include <mrcp_recog_header.h>
#include <mrcp_recog_resource.h>
#include <mpf_frame_buffer.h>
#include <apt_nlsml_doc.h>
#include <apt_pool.h>
#include <apt_log.h>


#define UNI_ENGINE_NAME "unimrcp"
#define UNI_ENGINE_CONFIG "res-speech-unimrcp.conf"

/** Timeout to wait for asynchronous response (actually this timeout shouldn't expire) */
#define MRCP_APP_REQUEST_TIMEOUT 60 * 1000000

/** \brief Forward declaration of speech */
typedef struct uni_speech_t uni_speech_t;
/** \brief Forward declaration of engine */
typedef struct uni_engine_t uni_engine_t;

/** \brief Declaration of UniMRCP based speech structure */
struct uni_speech_t {
	/* Name of the speech object to be used for logging */
	const char            *name;
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

	/* Active grammars (Content-IDs) */
	apr_hash_t            *active_grammars;

	/* Is session management request in-progress or not */
	apt_bool_t             is_sm_request;
	/* Session management request sent to server */
	mrcp_sig_command_e     sm_request;
	/* Satus code of session management response */
	mrcp_sig_status_code_e sm_response;

	/* Is recognition in-progress or not */
	apt_bool_t             is_inprogress;

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
	apr_pool_t            *pool;
	/* Client stack instance */
	mrcp_client_t         *client;
	/* Application instance */
	mrcp_application_t    *application;

	/* Profile name */
	const char            *profile;
	/* Log level */
	apt_log_priority_e     log_level;
	/* Log output */
	apt_log_output_e       log_output;

	/* Grammars to be preloaded with each MRCP session, if specified in config [grammars] */
	apr_table_t           *grammars;
	/* MRCPv2 properties (header fields) loaded from config */
	mrcp_message_header_t *v2_properties;
	/* MRCPv1 properties (header fields) loaded from config */
	mrcp_message_header_t *v1_properties;

	/* Mutex to be used for speech object numbering */
	apr_thread_mutex_t    *mutex;
	/* Current speech object number. */
	apr_uint16_t           current_speech_index;
};

static struct uni_engine_t uni_engine;

static int uni_recog_create_internal(struct ast_speech *speech, ast_format_compat *format);
static apt_bool_t uni_recog_channel_create(uni_speech_t *uni_speech, ast_format_compat *format);
static apt_bool_t uni_recog_properties_set(uni_speech_t *uni_speech);
static apt_bool_t uni_recog_grammars_preload(uni_speech_t *uni_speech);
static apt_bool_t uni_recog_sm_request_send(uni_speech_t *uni_speech, mrcp_sig_command_e sm_request);
static apt_bool_t uni_recog_mrcp_request_send(uni_speech_t *uni_speech, mrcp_message_t *message);
static void uni_recog_cleanup(uni_speech_t *uni_speech);

/** \brief Backward compatible define for the const qualifier */
#if AST_VERSION_AT_LEAST(1,8,0)
#define ast_compat_const const
#else /* < 1.8 */
#define ast_compat_const
#endif

/** \brief Get next speech identifier to be used for logging */
static apr_uint16_t uni_speech_id_get()
{
	apr_uint16_t id;

	if(uni_engine.mutex) apr_thread_mutex_lock(uni_engine.mutex);

	id = uni_engine.current_speech_index;

	if (uni_engine.current_speech_index == APR_UINT16_MAX)
		uni_engine.current_speech_index = 0;
	else
		uni_engine.current_speech_index++;

	if(uni_engine.mutex) apr_thread_mutex_unlock(uni_engine.mutex);

	return id;
}


/** \brief Version dependent prototypes of uni_recog_create() function */
#if AST_VERSION_AT_LEAST(10,0,0)
static int uni_recog_create(struct ast_speech *speech, ast_format_compat *format)
{
	return uni_recog_create_internal(speech,format);
}
#elif AST_VERSION_AT_LEAST(1,6,0)
static int uni_recog_create(struct ast_speech *speech, int format_id)
{
	ast_format_compat format;
	ast_format_clear(&format);
	format.id = format_id;
	return uni_recog_create_internal(speech,&format);
}
#else /* 1.4 */
static int uni_recog_create(struct ast_speech *speech)
{
	ast_format_compat format;
	ast_format_clear(&format);
	format.id = 0;
	return uni_recog_create_internal(speech,&format);
}
#endif

/** \brief Set up the speech structure within the engine */
static int uni_recog_create_internal(struct ast_speech *speech, ast_format_compat *format)
{
	uni_speech_t *uni_speech;
	mrcp_session_t *session;
	apr_pool_t *pool;
	const mpf_codec_descriptor_t *descriptor;

	/* Create session instance */
	session = mrcp_application_session_create(uni_engine.application,uni_engine.profile,speech);
	if(!session) {
		ast_log(LOG_ERROR, "Failed to create MRCP session\n");
		return -1;
	}
	pool = mrcp_application_session_pool_get(session);
	uni_speech = apr_palloc(pool,sizeof(uni_speech_t));
	uni_speech->name = apr_psprintf(pool, "RSU-%hu", uni_speech_id_get());
	uni_speech->session = session;
	uni_speech->channel = NULL;
	uni_speech->wait_object = NULL;
	uni_speech->mutex = NULL;
	uni_speech->media_buffer = NULL;
	uni_speech->active_grammars = apr_hash_make(pool);
	uni_speech->is_sm_request = FALSE;
	uni_speech->is_inprogress = FALSE;
	uni_speech->sm_request = 0;
	uni_speech->sm_response = MRCP_SIG_STATUS_CODE_SUCCESS;
	uni_speech->mrcp_request = NULL;
	uni_speech->mrcp_response = NULL;
	uni_speech->mrcp_event = NULL;

	uni_speech->speech_base = speech;
	speech->data = uni_speech;

	/* Create cond wait object and mutex */
	apr_thread_mutex_create(&uni_speech->mutex,APR_THREAD_MUTEX_DEFAULT,pool);
	apr_thread_cond_create(&uni_speech->wait_object,pool);

	ast_log(LOG_NOTICE, "(%s) Create speech resource\n",uni_speech->name);

	/* Set session name for logging purposes. */
	mrcp_application_session_name_set(session,uni_speech->name);

	/* Create recognition channel instance */
	if(uni_recog_channel_create(uni_speech,format) != TRUE) {
		ast_log(LOG_ERROR, "(%s) Failed to create MRCP channel\n",uni_speech->name);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	/* Send add channel request and wait for response */
	if(uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_CHANNEL_ADD) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to send add-channel request\n",uni_speech->name);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	/* Check received response */
	if(uni_speech->sm_response != MRCP_SIG_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "(%s) Failed to add MRCP channel\n",uni_speech->name);
		uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	descriptor = mrcp_application_source_descriptor_get(uni_speech->channel);
	if(descriptor) {
		mpf_frame_buffer_t *media_buffer;
		apr_size_t frame_size = mpf_codec_linear_frame_size_calculate(descriptor->sampling_rate,descriptor->channel_count);
		/* Create media buffer */
		ast_log(LOG_DEBUG, "(%s) Create media buffer frame_size:%"APR_SIZE_T_FMT"\n",uni_speech->name,frame_size);
		media_buffer = mpf_frame_buffer_create(frame_size,20,pool);
		uni_speech->media_buffer = media_buffer;
	}

	if(!uni_speech->media_buffer) {
		ast_log(LOG_WARNING, "(%s) Failed to create media buffer\n",uni_speech->name);
		uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	/* Set properties for session */
	uni_recog_properties_set(uni_speech);
	/* Preload grammars */
	uni_recog_grammars_preload(uni_speech);
	return 0;
}

/** \brief Destroy any data set on the speech structure by the engine */
static int uni_recog_destroy(struct ast_speech *speech)
{
	uni_speech_t *uni_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Destroy speech resource\n",uni_speech->name);

	/* Terminate session first */
	uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE);
	/* Then cleanup it */
	uni_recog_cleanup(uni_speech);
	return 0;
}

/*! \brief Cleanup already allocated data */
static void uni_recog_cleanup(uni_speech_t *uni_speech)
{
	if(uni_speech->speech_base) {
		uni_speech->speech_base->data = NULL;
	}
	if(uni_speech->mutex) {
		apr_thread_mutex_destroy(uni_speech->mutex);
		uni_speech->mutex = NULL;
	}
	if(uni_speech->wait_object) {
		apr_thread_cond_destroy(uni_speech->wait_object);
		uni_speech->wait_object = NULL;
	}
	if(uni_speech->media_buffer) {
		mpf_frame_buffer_destroy(uni_speech->media_buffer);
		uni_speech->media_buffer = NULL;
	}

	mrcp_application_session_destroy(uni_speech->session);
}

/*! \brief Stop the in-progress recognition */
static int uni_recog_stop(struct ast_speech *speech)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;

	if(!uni_speech->is_inprogress) {
		return 0;
	}

	ast_log(LOG_NOTICE, "(%s) Stop recognition\n",uni_speech->name);
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_STOP);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "(%s) Failed to create MRCP message\n",uni_speech->name);
		return -1;
	}

	/* Reset last event (if any) */
	uni_speech->mrcp_event = NULL;

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to stop recognition\n",uni_speech->name);
		return -1;
	}

	/* Reset media buffer */
	mpf_frame_buffer_restart(uni_speech->media_buffer);

	ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);

	uni_speech->is_inprogress = FALSE;
	return 0;
}

/*! \brief Load a local grammar on the speech structure */
static int uni_recog_load_grammar(struct ast_speech *speech, ast_compat_const char *grammar_name, ast_compat_const char *grammar_path)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;
	mrcp_generic_header_t *generic_header;
	const char *content_type = NULL;
	apt_bool_t inline_content = FALSE;
	char *tmp;
	apr_file_t *file;
	apt_str_t *body = NULL;

	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_DEFINE_GRAMMAR);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "(%s) Failed to create MRCP message\n",uni_speech->name);
		return -1;
	}

	/* 
	 * Grammar name and path are mandatory attributes, 
	 * grammar type can be optionally specified with path.
	 *
	 * SpeechLoadGrammar(name|path)
	 * SpeechLoadGrammar(name|type:path)
	 * SpeechLoadGrammar(name|uri:path)
	 * SpeechLoadGrammar(name|builtin:grammar/digits)
	 */

	tmp = strchr(grammar_path,':');
	if(tmp) {
		const char builtin_token[] = "builtin";
		const char uri_token[] = "uri";
		if(strncmp(grammar_path,builtin_token,sizeof(builtin_token)-1) == 0) {
			content_type = "text/uri-list";
			inline_content = TRUE;
		}
		else if(strncmp(grammar_path,uri_token,sizeof(uri_token)-1) == 0) {
			content_type = "text/uri-list";
			inline_content = TRUE;
			grammar_path = tmp+1;
		}
		else {
			*tmp = '\0';
			content_type = grammar_path;
			grammar_path = tmp+1;
		}
	}

	if(inline_content == TRUE) {
		body = &mrcp_message->body;
		apt_string_assign(body,grammar_path,mrcp_message->pool);
	}
	else {
		if(apr_file_open(&file,grammar_path,APR_FOPEN_READ|APR_FOPEN_BINARY,0,mrcp_message->pool) == APR_SUCCESS) {
			apr_finfo_t finfo;
			if(apr_file_info_get(&finfo,APR_FINFO_SIZE,file) == APR_SUCCESS) {
				/* Read message body */
				body = &mrcp_message->body;
				body->buf = apr_palloc(mrcp_message->pool,finfo.size+1);
				body->length = (apr_size_t)finfo.size;
				if(apr_file_read(file,body->buf,&body->length) != APR_SUCCESS) {
					ast_log(LOG_WARNING, "(%s) Failed to read grammar file %s\n",uni_speech->name,grammar_path);
				}
				body->buf[body->length] = '\0';
			}
			apr_file_close(file);
		}
		else {
			ast_log(LOG_WARNING, "(%s) No such grammar file available %s\n",uni_speech->name,grammar_path);
			return -1;
		}
	}

	if(!body || !body->buf) {
		ast_log(LOG_WARNING, "(%s) No grammar content available %s\n",uni_speech->name,grammar_path);
		return -1;
	}

	/* Try to implicitly detect content type, if it's not specified */
	if(!content_type) {
		if(strstr(body->buf,"#JSGF")) {
			content_type = "application/x-jsgf";
		}
		else if(strstr(body->buf,"#ABNF")) {
			content_type = "application/srgs";
		}
		else {
			content_type = "application/srgs+xml";
		}
	}

	ast_log(LOG_NOTICE, "(%s) Load grammar name: %s type: %s path: %s\n",
				uni_speech->name,
				grammar_name,
				content_type,
				grammar_path);
	/* Get/allocate generic header */
	generic_header = mrcp_generic_header_prepare(mrcp_message);
	if(generic_header) {
		/* Set generic header fields */
		apt_string_assign(&generic_header->content_type,content_type,mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message,GENERIC_HEADER_CONTENT_TYPE);
		apt_string_assign(&generic_header->content_id,grammar_name,mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message,GENERIC_HEADER_CONTENT_ID);
	}

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to load grammar\n",uni_speech->name);
		return -1;
	}

	return 0;
}

/** \brief Unload a local grammar */
static int uni_recog_unload_grammar(struct ast_speech *speech, ast_compat_const char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;
	mrcp_generic_header_t *generic_header;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	ast_log(LOG_NOTICE, "(%s) Unload grammar name: %s\n",
				uni_speech->name,
				grammar_name);

	apr_hash_set(uni_speech->active_grammars,grammar_name,APR_HASH_KEY_STRING,NULL);

	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_DEFINE_GRAMMAR);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "(%s) Failed to create MRCP message\n",uni_speech->name);
		return -1;
	}

	/* Get/allocate generic header */
	generic_header = mrcp_generic_header_prepare(mrcp_message);
	if(generic_header) {
		/* Set generic header fields */
		apt_string_assign(&generic_header->content_id,grammar_name,mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message,GENERIC_HEADER_CONTENT_ID);
	}

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to unload grammar\n",uni_speech->name);
		return -1;
	}

	return 0;
}

/** \brief Activate a loaded grammar */
static int uni_recog_activate_grammar(struct ast_speech *speech, ast_compat_const char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;
	apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);
	const char *entry;

	ast_log(LOG_NOTICE, "(%s) Activate grammar name: %s\n",uni_speech->name,grammar_name);
	entry = apr_pstrdup(pool,grammar_name);
	apr_hash_set(uni_speech->active_grammars,entry,APR_HASH_KEY_STRING,entry);
	return 0;
}

/** \brief Deactivate a loaded grammar */
static int uni_recog_deactivate_grammar(struct ast_speech *speech, ast_compat_const char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	ast_log(LOG_NOTICE, "(%s) Deactivate grammar name: %s\n",uni_speech->name,grammar_name);
	apr_hash_set(uni_speech->active_grammars,grammar_name,APR_HASH_KEY_STRING,NULL);
	return 0;
}

/** \brief Write audio to the speech engine */
static int uni_recog_write(struct ast_speech *speech, void *data, int len)
{
	uni_speech_t *uni_speech = speech->data;
	mpf_frame_t frame;

#if 0
	ast_log(LOG_DEBUG, "(%s) Write audio len: %d\n",uni_speech->name,len);
#endif
	frame.type = MEDIA_FRAME_TYPE_AUDIO;
	frame.marker = MPF_MARKER_NONE;
	frame.codec_frame.buffer = data;
	frame.codec_frame.size = len;

	if(mpf_frame_buffer_write(uni_speech->media_buffer,&frame) != TRUE) {
		ast_log(LOG_DEBUG, "(%s) Failed to write audio len: %d\n",uni_speech->name,len);
	}
	return 0;
}

/** \brief Signal DTMF was received */
static int uni_recog_dtmf(struct ast_speech *speech, const char *dtmf)
{
	uni_speech_t *uni_speech = speech->data;
	ast_log(LOG_NOTICE, "(%s) Signal DTMF %s\n",uni_speech->name,dtmf);
	return 0;
}

/** brief Prepare engine to accept audio */
static int uni_recog_start(struct ast_speech *speech)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;
	mrcp_generic_header_t *generic_header;
	mrcp_recog_header_t *recog_header;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	ast_log(LOG_NOTICE, "(%s) Start recognition\n",uni_speech->name);
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_RECOGNIZE);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "(%s) Failed to create MRCP message\n",uni_speech->name);
		return -1;
	}

	/* Get/allocate generic header */
	generic_header = mrcp_generic_header_prepare(mrcp_message);
	if(generic_header) {
		apr_hash_index_t *it;
		void *val;
		const char *grammar_name;
		const char *content = NULL;
		/* Set generic header fields */
		apt_string_assign(&generic_header->content_type,"text/uri-list",mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message,GENERIC_HEADER_CONTENT_TYPE);

		/* Construct and set message body */
		it = apr_hash_first(mrcp_message->pool,uni_speech->active_grammars);
		if(it) {
			apr_hash_this(it,NULL,NULL,&val);
			grammar_name = val;
			content = apr_pstrcat(mrcp_message->pool,"session:",grammar_name,NULL);
			it = apr_hash_next(it);
		}
		for(; it; it = apr_hash_next(it)) {
			apr_hash_this(it,NULL,NULL,&val);
			grammar_name = val;
			content = apr_pstrcat(mrcp_message->pool,content,"\nsession:",grammar_name,NULL);
		}
		if(content) {
			apt_string_set(&mrcp_message->body,content);
		}
	}

	/* Get/allocate recognizer header */
	recog_header = (mrcp_recog_header_t*) mrcp_resource_header_prepare(mrcp_message);
	if(recog_header) {
		/* Set recognizer header fields */
		if(mrcp_message->start_line.version == MRCP_VERSION_2) {
			recog_header->cancel_if_queue = FALSE;
			mrcp_resource_header_property_add(mrcp_message,RECOGNIZER_HEADER_CANCEL_IF_QUEUE);
		}
		recog_header->start_input_timers = TRUE;
		mrcp_resource_header_property_add(mrcp_message,RECOGNIZER_HEADER_START_INPUT_TIMERS);
	}

	/* Reset last event (if any) */
	uni_speech->mrcp_event = NULL;

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to start recognition\n",uni_speech->name);
		return -1;
	}

	/* Reset media buffer */
	mpf_frame_buffer_restart(uni_speech->media_buffer);

	ast_speech_change_state(speech, AST_SPEECH_STATE_READY);

	uni_speech->is_inprogress = TRUE;
	return 0;
}

/** \brief Change an engine specific setting */
static int uni_recog_change(struct ast_speech *speech, ast_compat_const char *name, const char *value)
{
	uni_speech_t *uni_speech = speech->data;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	ast_log(LOG_NOTICE, "(%s) Change setting name: %s value:%s\n",uni_speech->name,name,value);
	return 0;
}

/** \brief Change the type of results we want back */
static int uni_recog_change_results_type(struct ast_speech *speech,enum ast_speech_results_type results_type)
{
	uni_speech_t *uni_speech = speech->data;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	ast_log(LOG_NOTICE, "(%s) Change result type %d\n",uni_speech->name,results_type);
	return -1;
}

/** \brief Build ast_speech_result based on the NLSML result */
static struct ast_speech_result* uni_recog_speech_result_build(uni_speech_t *uni_speech, const apt_str_t *nlsml_result, mrcp_version_e mrcp_version)
{
	float confidence;
	const char *grammar;
	const char *text;
	struct ast_speech_result *speech_result;
	struct ast_speech_result *first_speech_result;
	nlsml_interpretation_t *interpretation;
	nlsml_instance_t *instance;
	nlsml_input_t *input;
	int interpretation_count;
	int instance_count;

	apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);
	nlsml_result_t *result = nlsml_result_parse(nlsml_result->buf, nlsml_result->length, pool);
	if(!result) {
		ast_log(LOG_WARNING, "(%s) Failed to parse NLSML result: %s\n",uni_speech->name,nlsml_result->buf);
		return NULL;
	}

#if 1 /* enable/disable debug output of parsed results */
	nlsml_result_trace(result, pool);
#endif

	first_speech_result = NULL;
#if AST_VERSION_AT_LEAST(1,6,0)
	AST_LIST_HEAD_NOLOCK(, ast_speech_result) speech_results;
	AST_LIST_HEAD_INIT_NOLOCK(&speech_results);
#else
	struct ast_speech_result *last_speech_result = NULL;
#endif

	interpretation_count = 0;
	interpretation = nlsml_first_interpretation_get(result);
	while(interpretation) {
		input = nlsml_interpretation_input_get(interpretation);
		if(!input) {
			ast_log(LOG_WARNING, "(%s) Failed to get NLSML input\n",uni_speech->name);
			continue;
		}

		instance_count = 0;
		instance = nlsml_interpretation_first_instance_get(interpretation);
		if(!instance) {
			ast_log(LOG_WARNING, "(%s) Failed to get NLSML instance\n",uni_speech->name);
			continue;
		}

		confidence = nlsml_interpretation_confidence_get(interpretation);
		grammar = nlsml_interpretation_grammar_get(interpretation);

		if(grammar) {
			const char session_token[] = "session:";
			char *str = strstr(grammar,session_token);
			if(str) {
				grammar = str + sizeof(session_token) - 1;
			}
		}

		do {
			nlsml_instance_swi_suppress(instance);
			text = nlsml_instance_content_generate(instance, pool);

			speech_result = ast_calloc(sizeof(struct ast_speech_result), 1);
			if(text)
				speech_result->text = strdup(text);
			speech_result->score = confidence * 100;
			if(grammar)
				speech_result->grammar = strdup(grammar);
			speech_result->nbest_num = interpretation_count;
			if(!first_speech_result)
				first_speech_result = speech_result;
#if AST_VERSION_AT_LEAST(1,6,0)
			AST_LIST_INSERT_TAIL(&speech_results, speech_result, list);
#else
			speech_result->next = last_speech_result;
			last_speech_result = speech_result;
#endif
			ast_log(LOG_NOTICE, "(%s) Speech result[%d/%d]: %s, score: %d, grammar: %s\n",
					uni_speech->name,
					interpretation_count,
					instance_count,
					speech_result->text,
					speech_result->score,
					speech_result->grammar);

			instance_count++;
			instance = nlsml_interpretation_next_instance_get(interpretation, instance);
		}
		while(instance);

		interpretation_count++;
		interpretation = nlsml_next_interpretation_get(result, interpretation);
	}

	return first_speech_result;
}

/** \brief Try to get result */
struct ast_speech_result* uni_recog_get(struct ast_speech *speech)
{
	mrcp_recog_header_t *recog_header;

	uni_speech_t *uni_speech = speech->data;

	if(uni_speech->is_inprogress) {
		uni_recog_stop(speech);
	}

	if(!uni_speech->mrcp_event) {
		ast_log(LOG_WARNING, "(%s) No RECOGNITION-COMPLETE message received\n",uni_speech->name);
		return NULL;
	}

	/* Get recognizer header */
	recog_header = mrcp_resource_header_get(uni_speech->mrcp_event);
	if(!recog_header || mrcp_resource_header_property_check(uni_speech->mrcp_event,RECOGNIZER_HEADER_COMPLETION_CAUSE) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Missing completion cause in RECOGNITION-COMPLETE message\n",uni_speech->name);
		return NULL;
	}

	ast_log(LOG_NOTICE, "(%s) Get result, completion cause: %03d reason: %s\n",
			uni_speech->name,
			recog_header->completion_cause,
			recog_header->completion_reason.buf ? recog_header->completion_reason.buf : "none");

	if(recog_header->completion_cause != RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
		ast_log(LOG_WARNING, "(%s) Recognition completed abnormally cause: %03d reason: %s\n",
				uni_speech->name,
				recog_header->completion_cause,
				recog_header->completion_reason.buf ? recog_header->completion_reason.buf : "none");
		return NULL;
	}

	if(speech->results) {
		ast_speech_results_free(speech->results);
	}

	speech->results = uni_recog_speech_result_build(
						uni_speech,
						&uni_speech->mrcp_event->body,
						uni_speech->mrcp_event->start_line.version);

	if(speech->results) {
		ast_set_flag(speech,AST_SPEECH_HAVE_RESULTS);
	}
	return speech->results;
}


/*! \brief Signal session management response */
static apt_bool_t uni_recog_sm_response_signal(uni_speech_t *uni_speech, mrcp_sig_command_e request, mrcp_sig_status_code_e status)
{
	apr_thread_mutex_lock(uni_speech->mutex);

	if(uni_speech->sm_request == request) {
		uni_speech->sm_response = status;
		apr_thread_cond_signal(uni_speech->wait_object);
	}
	else {
		ast_log(LOG_WARNING, "(%s) Received unexpected response %d, while waiting for %d\n",
					uni_speech->name,
					request, 
					uni_speech->sm_request);
	}

	apr_thread_mutex_unlock(uni_speech->mutex);
	return TRUE;
}

/*! \brief Signal MRCP response */
static apt_bool_t uni_recog_mrcp_response_signal(uni_speech_t *uni_speech, mrcp_message_t *message)
{
	apr_thread_mutex_lock(uni_speech->mutex);

	if(uni_speech->mrcp_request) {
		uni_speech->mrcp_response = message;
		apr_thread_cond_signal(uni_speech->wait_object);
	}
	else {
		ast_log(LOG_WARNING, "(%s) Received unexpected MRCP response\n",uni_speech->name);
	}
 
	apr_thread_mutex_unlock(uni_speech->mutex);
	return TRUE;
}

/** \brief Received session update response */
static apt_bool_t on_session_update(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	struct ast_speech *speech = mrcp_application_session_object_get(session);
	uni_speech_t *uni_speech = speech->data;

	ast_log(LOG_DEBUG, "(%s) On session update\n",uni_speech->name);
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_SESSION_UPDATE,status);
}

/** \brief Received session termination response */
static apt_bool_t on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	struct ast_speech *speech = mrcp_application_session_object_get(session);
	uni_speech_t *uni_speech = speech->data;

	ast_log(LOG_DEBUG, "(%s) On session terminate\n",uni_speech->name);
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE,status);
}

/** \brief Received channel add response */
static apt_bool_t on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "(%s) On channel add\n",uni_speech->name);
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_CHANNEL_ADD,status);
}

/** \brief Received channel remove response */
static apt_bool_t on_channel_remove(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "(%s) On channel remove\n",uni_speech->name);
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_CHANNEL_REMOVE,status);
}

/** \brief Received MRCP message */
static apt_bool_t on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "(%s) On message receive\n",uni_speech->name);
	if(message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
		return uni_recog_mrcp_response_signal(uni_speech,message);
	}

	if(message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
		if(message->start_line.method_id == RECOGNIZER_RECOGNITION_COMPLETE) {
			uni_speech->is_inprogress = FALSE;
			if (uni_speech->speech_base->state != AST_SPEECH_STATE_NOT_READY) {
				uni_speech->mrcp_event = message;
				ast_speech_change_state(uni_speech->speech_base,AST_SPEECH_STATE_DONE);
			}
			else {
				uni_speech->mrcp_event = NULL;
				ast_speech_change_state(uni_speech->speech_base,AST_SPEECH_STATE_NOT_READY);
			}
		}
		else if(message->start_line.method_id == RECOGNIZER_START_OF_INPUT) {
			ast_set_flag(uni_speech->speech_base,AST_SPEECH_QUIET);
		}
	}

	return TRUE;
}

/** \brief Received unexpected session/channel termination event */
static apt_bool_t on_terminate_event(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);
	ast_log(LOG_WARNING, "(%s) Received unexpected session termination event\n",uni_speech->name);
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
	on_terminate_event,
	on_resource_discover
};

/** \brief UniMRCP message handler */
static apt_bool_t uni_message_handler(const mrcp_app_message_t *app_message)
{
	return mrcp_application_message_dispatch(&uni_dispatcher,app_message);
}

/** \brief Process MPF frame */
static apt_bool_t uni_recog_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
	uni_speech_t *uni_speech = stream->obj;

	if(uni_speech->media_buffer) {
		mpf_frame_buffer_read(uni_speech->media_buffer,frame);
#if 0
		ast_log(LOG_DEBUG, "(%s) Read audio type: %d len: %d\n",
			uni_speech->name,
			frame->type,
			frame->codec_frame.size);
#endif
	}
	return TRUE;
}

/** \brief Methods of audio stream */
static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	NULL,
	NULL,
	NULL,
	uni_recog_stream_read,
	NULL,
	NULL,
	NULL
};

/** \brief Create recognition channel */
static apt_bool_t uni_recog_channel_create(uni_speech_t *uni_speech, ast_format_compat *format)
{
	mrcp_channel_t *channel;
	mpf_termination_t *termination;
	mpf_stream_capabilities_t *capabilities;
	apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);

	/* Create source stream capabilities */
	capabilities = mpf_source_stream_capabilities_create(pool);
	/* Add codec capabilities (Linear PCM) */
	mpf_codec_capabilities_add(
			&capabilities->codecs,
			MPF_SAMPLE_RATE_8000,
			"LPCM");

	/* Create media termination */
	termination = mrcp_application_audio_termination_create(
			uni_speech->session,      /* session, termination belongs to */
			&audio_stream_vtable,     /* virtual methods table of audio stream */
			capabilities,             /* stream capabilities */
			uni_speech);              /* object to associate */

	/* Create MRCP channel */
	channel = mrcp_application_channel_create(
			uni_speech->session,      /* session, channel belongs to */
			MRCP_RECOGNIZER_RESOURCE, /* MRCP resource identifier */
			termination,              /* media termination, used to terminate audio stream */
			NULL,                     /* RTP descriptor, used to create RTP termination (NULL by default) */
			uni_speech);              /* object to associate */

	if(!channel) {
		return FALSE;
	}
	uni_speech->channel = channel;
	return TRUE;
}

/** \brief Set properties */
static apt_bool_t uni_recog_properties_set(uni_speech_t *uni_speech)
{
	mrcp_message_t *mrcp_message;
	mrcp_message_header_t *properties;
	ast_log(LOG_DEBUG, "(%s) Set properties\n",uni_speech->name);
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_SET_PARAMS);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "(%s) Failed to create MRCP message\n",uni_speech->name);
		return FALSE;
	}

	/* Inherit properties loaded from config */
	if(mrcp_message->start_line.version == MRCP_VERSION_2) {
		properties = uni_engine.v2_properties;
	}
	else {
		properties = uni_engine.v1_properties;
	}

	if(properties) {
#if defined(TRANSPARENT_HEADER_FIELDS_SUPPORT)
		mrcp_header_fields_inherit(&mrcp_message->header,properties,mrcp_message->pool);
#else
		mrcp_message_header_inherit(&mrcp_message->header,properties,mrcp_message->pool);
#endif
	}

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "(%s) Failed to set properties\n",uni_speech->name);
		return FALSE;
	}

	return TRUE;
}

/** \brief Preload grammar */
static apt_bool_t uni_recog_grammars_preload(uni_speech_t *uni_speech)
{
	apr_table_t *grammars = uni_engine.grammars;
	if(grammars && uni_speech->session) {
		int i;
		char *grammar_name;
		char *grammar_path;
		apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);
		const apr_array_header_t *header = apr_table_elts(grammars);
		apr_table_entry_t *entry = (apr_table_entry_t *) header->elts;
		for(i=0; i<header->nelts; i++) {
			grammar_name = apr_pstrdup(pool,entry[i].key);
			grammar_path = apr_pstrdup(pool,entry[i].val);
			uni_recog_load_grammar(uni_speech->speech_base,grammar_name,grammar_path);
		}
	}
	return TRUE;
}

/** \brief Send session management request to client stack and wait for async response */
static apt_bool_t uni_recog_sm_request_send(uni_speech_t *uni_speech, mrcp_sig_command_e sm_request)
{
	apt_bool_t res = FALSE;
	ast_log(LOG_DEBUG, "(%s) Send session request type: %d\n",uni_speech->name,sm_request);
	apr_thread_mutex_lock(uni_speech->mutex);
	uni_speech->is_sm_request = TRUE;
	uni_speech->sm_request = sm_request;
	switch(sm_request) {
		case MRCP_SIG_COMMAND_SESSION_UPDATE:
			res = mrcp_application_session_update(uni_speech->session);
			break;
		case MRCP_SIG_COMMAND_SESSION_TERMINATE:
			res = mrcp_application_session_terminate(uni_speech->session);
			break;
		case MRCP_SIG_COMMAND_CHANNEL_ADD:
			res = mrcp_application_channel_add(uni_speech->session,uni_speech->channel);
			break;
		case MRCP_SIG_COMMAND_CHANNEL_REMOVE:
			res = mrcp_application_channel_remove(uni_speech->session,uni_speech->channel);
			break;
		case MRCP_SIG_COMMAND_RESOURCE_DISCOVER:
			res = mrcp_application_resource_discover(uni_speech->session);
			break;
		default:
			break;
	}

	if(res == TRUE) {
		/* Wait for session response */
		ast_log(LOG_DEBUG, "(%s) Wait for session response type: %d\n",uni_speech->name,sm_request);
		if(apr_thread_cond_timedwait(uni_speech->wait_object,uni_speech->mutex,MRCP_APP_REQUEST_TIMEOUT) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) Failed to get session response: request timed out\n",uni_speech->name);
			uni_speech->sm_response = MRCP_SIG_STATUS_CODE_FAILURE;
		}
		ast_log(LOG_DEBUG, "(%s) Process session response type: %d status-code: %d\n",uni_speech->name,sm_request,uni_speech->sm_response);
	}

	uni_speech->is_sm_request = FALSE;
	apr_thread_mutex_unlock(uni_speech->mutex);
	return res;
}

/** \brief Send MRCP request to client stack and wait for async response */
static apt_bool_t uni_recog_mrcp_request_send(uni_speech_t *uni_speech, mrcp_message_t *message)
{
	apt_bool_t res = FALSE;
	apr_thread_mutex_lock(uni_speech->mutex);
	uni_speech->mrcp_request = message;

	/* Send MRCP request */
	ast_log(LOG_DEBUG, "(%s) Send MRCP request method-id: %d\n",uni_speech->name,(int)message->start_line.method_id);
	res = mrcp_application_message_send(uni_speech->session,uni_speech->channel,message);
	if(res == TRUE) {
		/* Wait for MRCP response */
		ast_log(LOG_DEBUG, "(%s) Wait for MRCP response\n",uni_speech->name);
		if(apr_thread_cond_timedwait(uni_speech->wait_object,uni_speech->mutex,MRCP_APP_REQUEST_TIMEOUT) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) Failed to get MRCP response: request timed out\n",uni_speech->name);
			uni_speech->mrcp_response = NULL;
		}

		/* Wake up and check received response */
		if(uni_speech->mrcp_response) {
			ast_log(LOG_DEBUG, "(%s) Process MRCP response method-id: %d status-code: %d\n",
					uni_speech->name, 
					(int)message->start_line.method_id,
					uni_speech->mrcp_response->start_line.status_code);
			
			if(uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS && 
				uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS_WITH_IGNORE) {
				ast_log(LOG_WARNING, "(%s) MRCP request failed method-id: %d status-code: %d\n",
					uni_speech->name,
					(int)message->start_line.method_id,
					uni_speech->mrcp_response->start_line.status_code);
				res = FALSE;
			}
		}
		else {
			ast_log(LOG_ERROR, "(%s) No MRCP response available\n",uni_speech->name);
			res = FALSE;
		}
	}
	else {
		ast_log(LOG_WARNING, "(%s) Failed to send MRCP request\n",uni_speech->name);
	}
	uni_speech->mrcp_request = NULL;
	apr_thread_mutex_unlock(uni_speech->mutex);
	return res;
}

/** \brief Speech engine declaration */
static struct ast_speech_engine ast_engine = {
	UNI_ENGINE_NAME,
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
	uni_recog_get
};

/** \brief Load properties from config */
static mrcp_message_header_t* uni_engine_properties_load(struct ast_config *cfg, const char *category, mrcp_version_e version, apr_pool_t *pool)
{
	struct ast_variable *var;
	mrcp_message_header_t *properties = NULL;

#if defined(TRANSPARENT_HEADER_FIELDS_SUPPORT)
	apt_header_field_t *header_field;
	properties = mrcp_message_header_create(
		mrcp_generic_header_vtable_get(version),
		mrcp_recog_header_vtable_get(version),
		pool);
	for(var = ast_variable_browse(cfg, category); var; var = var->next) {
		ast_log(LOG_DEBUG, "%s.%s=%s\n", category, var->name, var->value);
		header_field = apt_header_field_create_c(var->name,var->value,pool);
		if(header_field) {
			if(mrcp_header_field_add(properties,header_field,pool) == FALSE) {
				ast_log(LOG_WARNING, "Unknown MRCP header %s.%s=%s\n", category, var->name, var->value);
			}
		}
	}
#else
	apt_pair_t pair;
	properties = apr_palloc(pool,sizeof(mrcp_message_header_t));
	mrcp_message_header_init(properties);
	properties->generic_header_accessor.vtable = mrcp_generic_header_vtable_get(version);
	properties->resource_header_accessor.vtable = mrcp_recog_header_vtable_get(version);
	mrcp_header_allocate(&properties->generic_header_accessor,pool);
	mrcp_header_allocate(&properties->resource_header_accessor,pool);
	for(var = ast_variable_browse(cfg, category); var; var = var->next) {
		ast_log(LOG_DEBUG, "%s.%s=%s\n", category, var->name, var->value);
		apt_string_set(&pair.name,var->name);
		apt_string_set(&pair.value,var->value);
		if(mrcp_header_parse(&properties->resource_header_accessor,&pair,pool) != TRUE) {
			if(mrcp_header_parse(&properties->generic_header_accessor,&pair,pool) != TRUE) {
				ast_log(LOG_WARNING, "Unknown MRCP header %s.%s=%s\n", category, var->name, var->value);
			}
		}
	}
#endif
	return properties;
}

/** \brief Load grammars from config */
static apr_table_t* uni_engine_grammars_load(struct ast_config *cfg, const char *category, apr_pool_t *pool)
{
	struct ast_variable *var;
	apr_table_t *grammars = apr_table_make(pool,0);
	for(var = ast_variable_browse(cfg, category); var; var = var->next) {
		ast_log(LOG_DEBUG, "%s.%s=%s\n", category, var->name, var->value);
		apr_table_set(grammars,var->name,var->value);
	}
	return grammars;
}

/** \brief Load UniMRCP engine configuration (/etc/asterisk/res_speech_unimrcp.conf)*/
static apt_bool_t uni_engine_config_load(apr_pool_t *pool)
{
	const char *value = NULL;
#if AST_VERSION_AT_LEAST(1,6,0)
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(UNI_ENGINE_CONFIG, config_flags);
#else
	struct ast_config *cfg = ast_config_load(UNI_ENGINE_CONFIG);
#endif
	if(!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", UNI_ENGINE_CONFIG);
		return FALSE;
	}

	if((value = ast_variable_retrieve(cfg, "general", "unimrcp-profile")) != NULL) {
		ast_log(LOG_DEBUG, "general.unimrcp-profile=%s\n", value);
		uni_engine.profile = apr_pstrdup(uni_engine.pool, value);
	}

	if((value = ast_variable_retrieve(cfg, "general", "log-level")) != NULL) {
		ast_log(LOG_DEBUG, "general.log-level=%s\n", value);
		uni_engine.log_level = apt_log_priority_translate(value);
	}

	if((value = ast_variable_retrieve(cfg, "general", "log-output")) != NULL) {
		ast_log(LOG_DEBUG, "general.log-output=%s\n", value);
		uni_engine.log_output = atoi(value);
	}

	uni_engine.grammars = uni_engine_grammars_load(cfg,"grammars",pool);

	uni_engine.v2_properties = uni_engine_properties_load(cfg,"mrcpv2-properties",MRCP_VERSION_2,pool);
	uni_engine.v1_properties = uni_engine_properties_load(cfg,"mrcpv1-properties",MRCP_VERSION_1,pool);

	ast_config_destroy(cfg);
	return TRUE;
}

/** \brief Unload UniMRCP engine */
static apt_bool_t uni_engine_unload()
{
	if(uni_engine.client) {
		mrcp_client_destroy(uni_engine.client);
		uni_engine.client = NULL;
	}

	/* Destroy singleton logger */
	apt_log_instance_destroy();

	if(uni_engine.mutex) {
		apr_thread_mutex_destroy(uni_engine.mutex);
		uni_engine.mutex = NULL;
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
	uni_engine.profile = NULL;
	uni_engine.log_level = APT_PRIO_INFO;
	uni_engine.log_output = APT_LOG_OUTPUT_CONSOLE | APT_LOG_OUTPUT_FILE;
	uni_engine.grammars = NULL;
	uni_engine.v2_properties = NULL;
	uni_engine.v1_properties = NULL;
	uni_engine.mutex = NULL;
	uni_engine.current_speech_index = 0;

	pool = apt_pool_create();
	if(!pool) {
		ast_log(LOG_ERROR, "Failed to create APR pool\n");
		uni_engine_unload();
		return FALSE;
	}

	uni_engine.pool = pool;

	if(apr_thread_mutex_create(&uni_engine.mutex, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to create engine mutex\n");
		uni_engine_unload();
		return FALSE;
	}

	/* Load engine configuration */
	uni_engine_config_load(pool);

	if(!uni_engine.profile) {
		uni_engine.profile = "uni2";
	}

	dir_layout = apt_default_dir_layout_create(UNIMRCP_DIR_LOCATION,pool);
	/* Create singleton logger */
	apt_log_instance_create(uni_engine.log_output, uni_engine.log_level, pool);
	/* Open the log file */
	apt_log_file_open(dir_layout->log_dir_path,"astuni",MAX_LOG_FILE_SIZE,MAX_LOG_FILE_COUNT,TRUE,pool);

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
		ast_log(LOG_ERROR, "Failed to initialize MRCP client\n");
		uni_engine_unload();
		return FALSE;
	}

	return TRUE;
}

/** \brief Load module */
static int load_module(void)
{
	ast_log(LOG_NOTICE, "Load Res-Speech-UniMRCP module\n");

	if(uni_engine_load() == FALSE) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if(mrcp_client_start(uni_engine.client) != TRUE) {
		ast_log(LOG_ERROR, "Failed to start MRCP client\n");
		uni_engine_unload();
		return AST_MODULE_LOAD_FAILURE;
	}

#if AST_VERSION_AT_LEAST(10,0,0)
	ast_engine.formats = ast_format_cap_alloc_nolock();
	if(!ast_engine.formats) {
		ast_log(LOG_ERROR, "Failed to alloc media format capabilities\n");
		uni_engine_unload();
		return AST_MODULE_LOAD_FAILURE;
	}
	struct ast_format format;
	ast_format_set(&format, AST_FORMAT_SLINEAR, 0);
	ast_format_cap_add(ast_engine.formats, &format);
#else /* <= 1.8 */
	ast_engine.formats = AST_FORMAT_SLINEAR;
#endif

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
	ast_log(LOG_NOTICE, "Unload Res-Speech-UniMRCP module\n");
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
