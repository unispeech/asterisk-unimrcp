/* 
 * The implementation of Asterisk's Speech API via UniMRCP
 *
 * Copyright (C) 2009, Arsen Chaloyan  <achaloyan@gmail.com>
 *
 */

/*** MODULEINFO
	<depend>unimrcp</depend>
 ***/

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

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
#include <apr_tables.h>
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
	
	/* Is session management request in-progress or not */
	apt_bool_t             is_sm_request;
	/* Session management request sent to server */
	mrcp_sig_command_e     sm_request;
	/* Satus code of session management response */
	mrcp_sig_status_code_e sm_response;
	
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
	apr_pool_t           *pool;
	/* Client stack instance */
	mrcp_client_t        *client;
	/* Application instance */
	mrcp_application_t   *application;

	/* Profile name */
	const char           *profile;
	/* Log level */
	apt_log_priority_e    log_level;
	/* Log output */
	apt_log_output_e      log_output;

	/* Grammars to be preloaded with each MRCP session, if anything specified in config [grammars] */
	apr_table_t          *grammars;
	/* MRCPv2 properties (header fields) loaded from config */
	mrcp_message_header_t v2_properties;
	/* MRCPv1 properties (header fields) loaded from config */
	mrcp_message_header_t v1_properties;
};

static struct uni_engine_t uni_engine;

static apt_bool_t uni_recog_channel_create(uni_speech_t *uni_speech, int format);
static apt_bool_t uni_recog_properties_set(uni_speech_t *uni_speech);
static apt_bool_t uni_recog_grammars_preload(uni_speech_t *uni_speech);
static apt_bool_t uni_recog_sm_request_send(uni_speech_t *uni_speech, mrcp_sig_command_e sm_request);
static apt_bool_t uni_recog_mrcp_request_send(uni_speech_t *uni_speech, mrcp_message_t *message);
static void uni_recog_cleanup(uni_speech_t *uni_speech);

static const char* uni_speech_id_get(uni_speech_t *uni_speech)
{
	const apt_str_t *id = mrcp_application_session_id_get(uni_speech->session);
	if(id && id->buf) {
		return id->buf;
	}
	return "none";
}

/** \brief Set up the speech structure within the engine */
#if defined(ASTERISK14)
static int uni_recog_create(struct ast_speech *speech)
#else
static int uni_recog_create(struct ast_speech *speech, int format)
#endif
{
	uni_speech_t *uni_speech;
	mrcp_session_t *session;
	apr_pool_t *pool;
	const mpf_codec_descriptor_t *descriptor;
#if defined(ASTERISK14)
	int format = 0;
#endif

	/* Create session instance */
	session = mrcp_application_session_create(uni_engine.application,uni_engine.profile,speech);
	if(!session) {
		ast_log(LOG_ERROR, "Failed to create session\n");
		return -1;
	}
	pool = mrcp_application_session_pool_get(session);
	uni_speech = apr_palloc(pool,sizeof(uni_speech_t));
	uni_speech->session = session;
	uni_speech->channel = NULL;
	uni_speech->wait_object = NULL;
	uni_speech->mutex = NULL;
	uni_speech->media_buffer = NULL;
	uni_speech->active_grammar_name = NULL;
	uni_speech->is_sm_request = FALSE;
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

	/* Create recognition channel instance */
	if(uni_recog_channel_create(uni_speech,format) != TRUE) {
		ast_log(LOG_ERROR, "Failed to create channel\n");
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	/* Send add channel request and wait for response */
	if(uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_CHANNEL_ADD) != TRUE) {
		ast_log(LOG_WARNING, "Failed to send add channel request\n");
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	/* Check received response */
	if(uni_speech->sm_response != MRCP_SIG_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to add channel\n");
		uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	descriptor = mrcp_application_source_descriptor_get(uni_speech->channel);
	if(descriptor) {
		mpf_frame_buffer_t *media_buffer;
		apr_size_t frame_size = mpf_codec_linear_frame_size_calculate(descriptor->sampling_rate,descriptor->channel_count);
		/* Create media buffer */
		ast_log(LOG_DEBUG, "Create media buffer frame_size:%d\n",frame_size);
		media_buffer = mpf_frame_buffer_create(frame_size,20,pool);
		uni_speech->media_buffer = media_buffer;
	}
	
	if(!uni_speech->media_buffer) {
		ast_log(LOG_WARNING, "Failed to create media buffer\n");
		uni_recog_sm_request_send(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE);
		uni_recog_cleanup(uni_speech);
		return -1;
	}

	ast_log(LOG_NOTICE, "Created speech instance '%s'\n",uni_speech_id_get(uni_speech));

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
	ast_log(LOG_NOTICE, "Destroy speech instance '%s'\n",uni_speech_id_get(uni_speech));

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

/*! \brief Load a local grammar on the speech structure */
static int uni_recog_load_grammar(struct ast_speech *speech, char *grammar_name, char *grammar_path)
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
		ast_log(LOG_WARNING, "Failed to create MRCP message\n");
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
		const char *builtin_token = "builtin";
		const char *uri_token = "uri";
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
					ast_log(LOG_WARNING, "Failed to read the content of grammar file: %s\n",grammar_path);
				}
				body->buf[body->length] = '\0';
			}
			apr_file_close(file);
		}
		else {
			ast_log(LOG_WARNING, "No such grammar file available: %s\n",grammar_path);
			return -1;
		}
	}

	if(!body || !body->buf) {
		ast_log(LOG_WARNING, "No content available: %s\n",grammar_path);
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

	ast_log(LOG_NOTICE, "Load grammar name:%s type:%s path:%s '%s'\n",
				grammar_name,
				content_type,
				grammar_path,
				uni_speech_id_get(uni_speech));
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
		ast_log(LOG_WARNING, "Failed to send MRCP message\n");
		return -1;
	}

	/* Check received response */
	if(!uni_speech->mrcp_response || uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "Received failure response\n");
		return -1;
	}
	return 0;
}

/** \brief Unload a local grammar */
static int uni_recog_unload_grammar(struct ast_speech *speech, char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;
	mrcp_generic_header_t *generic_header;

	ast_log(LOG_NOTICE, "Unload grammar name:%s '%s'\n",
				grammar_name,
				uni_speech_id_get(uni_speech));
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_DEFINE_GRAMMAR);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "Failed to create MRCP message\n");
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
		ast_log(LOG_WARNING, "Failed to send MRCP message\n");
		return -1;
	}

	/* Check received response */
	if(!uni_speech->mrcp_response || uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "Received failure response\n");
		return -1;
	}
	return 0;
}

/** \brief Activate a loaded grammar */
static int uni_recog_activate_grammar(struct ast_speech *speech, char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;
	apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);

	ast_log(LOG_NOTICE, "Activate grammar name:%s '%s'\n",
						grammar_name,
						uni_speech_id_get(uni_speech));
	uni_speech->active_grammar_name = apr_pstrdup(pool,grammar_name);
	return 0;
}

/** \brief Deactivate a loaded grammar */
static int uni_recog_deactivate_grammar(struct ast_speech *speech, char *grammar_name)
{
	uni_speech_t *uni_speech = speech->data;

	ast_log(LOG_NOTICE, "Deactivate grammar name:%s '%s'\n",
						grammar_name,
						uni_speech_id_get(uni_speech));
	uni_speech->active_grammar_name = NULL;
	return 0;
}

/** \brief Write audio to the speech engine */
static int uni_recog_write(struct ast_speech *speech, void *data, int len)
{
	uni_speech_t *uni_speech = speech->data;
	mpf_frame_t frame;

#if 0
	ast_log(LOG_DEBUG, "Write audio '%s' len:%d\n",uni_speech_id_get(uni_speech),len);
#endif
	frame.type = MEDIA_FRAME_TYPE_AUDIO;
	frame.marker = MPF_MARKER_NONE;
	frame.codec_frame.buffer = data;
	frame.codec_frame.size = len;

	if(mpf_frame_buffer_write(uni_speech->media_buffer,&frame) != TRUE) {
		ast_log(LOG_DEBUG, "Failed to write audio len:%d\n",len);
	}
	return 0;
}

/** \brief Signal DTMF was received */
static int uni_recog_dtmf(struct ast_speech *speech, const char *dtmf)
{
	uni_speech_t *uni_speech = speech->data;
	ast_log(LOG_NOTICE, "Signal DTMF '%s'\n",uni_speech_id_get(uni_speech));
	return 0;
}

/** brief Prepare engine to accept audio */
static int uni_recog_start(struct ast_speech *speech)
{
	uni_speech_t *uni_speech = speech->data;
	mrcp_message_t *mrcp_message;
	mrcp_generic_header_t *generic_header;
	mrcp_recog_header_t *recog_header;

	ast_log(LOG_NOTICE, "Start audio '%s'\n",uni_speech_id_get(uni_speech));
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_RECOGNIZE);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "Failed to create MRCP message\n");
		return -1;
	}
	
	/* Get/allocate generic header */
	generic_header = mrcp_generic_header_prepare(mrcp_message);
	if(generic_header) {
		const char *content;
		/* Set generic header fields */
		apt_string_assign(&generic_header->content_type,"text/uri-list",mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message,GENERIC_HEADER_CONTENT_TYPE);

		/* Set message body */
		content = apr_pstrcat(mrcp_message->pool,"session:",uni_speech->active_grammar_name,NULL);
		apt_string_set(&mrcp_message->body,content);
	}

	/* Get/allocate recognizer header */
	recog_header = (mrcp_recog_header_t*) mrcp_resource_header_prepare(mrcp_message);
	if(recog_header) {
		/* Set recognizer header fields */
		if(mrcp_message->start_line.version == MRCP_VERSION_2) {
			recog_header->cancel_if_queue = FALSE;
			mrcp_resource_header_property_add(mrcp_message,RECOGNIZER_HEADER_CANCEL_IF_QUEUE);
		}
	}

	/* Reset last event (if any) */
	uni_speech->mrcp_event = NULL;

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "Failed to send MRCP message\n");
		return -1;
	}

	/* Check received response */
	if(!uni_speech->mrcp_response || uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "Received failure response\n");
		return -1;
	}
	
	/* Reset media buffer */
	mpf_frame_buffer_restart(uni_speech->media_buffer);
	
	ast_speech_change_state(speech, AST_SPEECH_STATE_READY);
	return 0;
}

/** \brief Change an engine specific setting */
static int uni_recog_change(struct ast_speech *speech, char *name, const char *value)
{
	uni_speech_t *uni_speech = speech->data;
	ast_log(LOG_NOTICE, "Change setting '%s'\n",uni_speech_id_get(uni_speech));
	return 0;
}

/** \brief Change the type of results we want back */
static int uni_recog_change_results_type(struct ast_speech *speech,enum ast_speech_results_type results_type)
{
	uni_speech_t *uni_speech = speech->data;
	ast_log(LOG_NOTICE, "Change result type '%s'\n",uni_speech_id_get(uni_speech));
	return -1;
}

/** \brief Try to get result */
struct ast_speech_result* uni_recog_get(struct ast_speech *speech)
{
	apt_str_t *nlsml_result;
	apr_xml_elem *interpret;
	apr_xml_doc *doc;
	mrcp_recog_header_t *recog_header;

	uni_speech_t *uni_speech = speech->data;
	apr_pool_t *pool = mrcp_application_session_pool_get(uni_speech->session);

	ast_log(LOG_NOTICE, "Get result '%s'\n",uni_speech_id_get(uni_speech));
	if(!uni_speech->mrcp_event) {
		ast_log(LOG_WARNING, "No RECOGNITION-COMPLETE message received\n");
		return NULL;
	}

	/* Get recognizer header */
	recog_header = mrcp_resource_header_get(uni_speech->mrcp_event);
	if(!recog_header || mrcp_resource_header_property_check(uni_speech->mrcp_event,RECOGNIZER_HEADER_COMPLETION_CAUSE) != TRUE) {
		ast_log(LOG_WARNING, "Missing Completion-Cause in RECOGNITION-COMPLETE message\n");
		return NULL;
	}

	if(recog_header->completion_cause != RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
		ast_log(LOG_WARNING, "Unsuccessful completion cause:%d reason:%s\n",
		    recog_header->completion_cause,
		    recog_header->completion_reason.buf ? recog_header->completion_reason.buf : "none");
		return NULL;
	}

	nlsml_result = &uni_speech->mrcp_event->body;

	doc = nlsml_doc_load(nlsml_result,pool);
	if(!doc) {
		ast_log(LOG_WARNING, "Failed to load NLSML document\n");
		return NULL;
	}

	if(speech->results) {
		ast_speech_results_free(speech->results);
		speech->results = NULL;
	}

	interpret = nlsml_first_interpret_get(doc);
	if(interpret) {
		apr_xml_elem *instance;
		apr_xml_elem *input;
		/* Get instance and input */
		nlsml_interpret_results_get(interpret,&instance,&input);
		if(input) {
			const char *confidence;
			const char *grammar;
			speech->results = ast_calloc(sizeof(struct ast_speech_result), 1);
			speech->results->text = NULL;
			speech->results->score = 0;
			if(input->first_cdata.first) {
				speech->results->text = strdup(input->first_cdata.first->text);
			}
			confidence = nlsml_input_attrib_get(input,"confidence",TRUE);
			if(confidence) {
				if(uni_speech->mrcp_event->start_line.version == MRCP_VERSION_2) {
					speech->results->score = (int)(atof(confidence) * 100);
				}
				else {
					speech->results->score = atoi(confidence);
				}
			}
			grammar = nlsml_input_attrib_get(input,"grammar",TRUE);
			if(grammar) {
				grammar = strchr(grammar,':');
				if(grammar && *grammar != '\0') {
					grammar++;
					speech->results->grammar = strdup(grammar);
				}
			}
			ast_log(LOG_NOTICE, "Interpreted input:%s score:%d grammar:%s\n",
				speech->results->text ? speech->results->text : "none",
				speech->results->score,
				speech->results->grammar ? speech->results->grammar : "none");
			ast_set_flag(speech,AST_SPEECH_HAVE_RESULTS);
		}
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
		ast_log(LOG_WARNING, "Received unexpected response :%d, while waiting for :%d\n",
			request, uni_speech->sm_request);
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
		ast_log(LOG_WARNING, "Received unexpected MRCP response\n");
	}

	apr_thread_mutex_unlock(uni_speech->mutex);
	return TRUE;
}

/** \brief Received session update response */
static apt_bool_t on_session_update(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	struct ast_speech *speech = mrcp_application_session_object_get(session);
	uni_speech_t *uni_speech = speech->data;

	ast_log(LOG_DEBUG, "On session update\n");
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_SESSION_UPDATE,status);
}

/** \brief Received session termination response */
static apt_bool_t on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	struct ast_speech *speech = mrcp_application_session_object_get(session);
	uni_speech_t *uni_speech = speech->data;

	ast_log(LOG_DEBUG, "On session terminate\n");
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_SESSION_TERMINATE,status);
}

/** \brief Received channel add response */
static apt_bool_t on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "On channel add\n");
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_CHANNEL_ADD,status);
}

/** \brief Received channel remove response */
static apt_bool_t on_channel_remove(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "On channel remove\n");
	return uni_recog_sm_response_signal(uni_speech,MRCP_SIG_COMMAND_CHANNEL_REMOVE,status);
}

/** \brief Received MRCP message */
static apt_bool_t on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	uni_speech_t *uni_speech = mrcp_application_channel_object_get(channel);

	ast_log(LOG_DEBUG, "On message receive\n");
	if(message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
		return uni_recog_mrcp_response_signal(uni_speech,message);
	}
	
	if(message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
		if(message->start_line.method_id == RECOGNIZER_RECOGNITION_COMPLETE) {
			uni_speech->mrcp_event = message;
			ast_speech_change_state(uni_speech->speech_base,AST_SPEECH_STATE_DONE);
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
	ast_log(LOG_DEBUG, "Received message from client stack\n");
	return mrcp_application_message_dispatch(&uni_dispatcher,app_message);
}



/** \brief Process MPF frame */
static apt_bool_t uni_recog_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
	uni_speech_t *uni_speech = stream->obj;

	if(uni_speech->media_buffer) {
		mpf_frame_buffer_read(uni_speech->media_buffer,frame);
#if 0
		ast_log(LOG_DEBUG, "Read audio '%s' type:%d len:%d\n",
			uni_speech_id_get(uni_speech),
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
static apt_bool_t uni_recog_channel_create(uni_speech_t *uni_speech, int format)
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
	ast_log(LOG_DEBUG, "Set properties '%s'\n",uni_speech_id_get(uni_speech));
	mrcp_message = mrcp_application_message_create(
								uni_speech->session,
								uni_speech->channel,
								RECOGNIZER_SET_PARAMS);
	if(!mrcp_message) {
		ast_log(LOG_WARNING, "Failed to create MRCP message\n");
		return FALSE;
	}
	
	/* Inherit properties loaded from config */
	if(mrcp_message->start_line.version == MRCP_VERSION_2) {
		mrcp_message_header_inherit(&mrcp_message->header,&uni_engine.v2_properties,mrcp_message->pool);
	}
	else {
		mrcp_message_header_inherit(&mrcp_message->header,&uni_engine.v1_properties,mrcp_message->pool);
	}

	/* Send MRCP request and wait for response */
	if(uni_recog_mrcp_request_send(uni_speech,mrcp_message) != TRUE) {
		ast_log(LOG_WARNING, "Failed to send MRCP message\n");
		return FALSE;
	}

	/* Check received response */
	if(!uni_speech->mrcp_response || uni_speech->mrcp_response->start_line.status_code != MRCP_STATUS_CODE_SUCCESS) {
		ast_log(LOG_WARNING, "Received failure response\n");
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
	ast_log(LOG_DEBUG, "Send session request type:%d\n",sm_request);
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
		ast_log(LOG_DEBUG, "Wait for session response\n");
		if(apr_thread_cond_timedwait(uni_speech->wait_object,uni_speech->mutex,MRCP_APP_REQUEST_TIMEOUT) != APR_SUCCESS) {
		    ast_log(LOG_ERROR, "Failed to get response, request timed out\n");
		    uni_speech->sm_response = MRCP_SIG_STATUS_CODE_FAILURE;
		}
		ast_log(LOG_DEBUG, "Waked up, status code: %d\n",uni_speech->sm_response);
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
	ast_log(LOG_DEBUG, "Send MRCP request\n");
	res = mrcp_application_message_send(uni_speech->session,uni_speech->channel,message);

	if(res == TRUE) {
		/* Wait for MRCP response */
		ast_log(LOG_DEBUG, "Wait for MRCP response\n");
		if(apr_thread_cond_timedwait(uni_speech->wait_object,uni_speech->mutex,MRCP_APP_REQUEST_TIMEOUT) != APR_SUCCESS) {
		    ast_log(LOG_ERROR, "Failed to get response, request timed out\n");
		    uni_speech->mrcp_response = NULL;
		}
		ast_log(LOG_DEBUG, "Waked up\n");
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
    uni_recog_get,
    AST_FORMAT_SLINEAR
};

/** \brief Init properties */
static void uni_engine_properties_init(mrcp_message_header_t *properties, mrcp_version_e version)
{
	mrcp_message_header_init(properties);
	properties->generic_header_accessor.vtable = mrcp_generic_header_vtable_get(version);
	properties->resource_header_accessor.vtable = mrcp_recog_header_vtable_get(version);
}

/** \brief Load properties from config */
static void uni_engine_properties_load(mrcp_message_header_t *properties, struct ast_config *cfg, const char *category, apr_pool_t *pool)
{
	struct ast_variable *var;
	apt_pair_t pair;

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
#if defined(ASTERISK14)
	struct ast_config *cfg = ast_config_load(UNI_ENGINE_CONFIG);
#else
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(UNI_ENGINE_CONFIG, config_flags);
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

	uni_engine_properties_load(&uni_engine.v2_properties,cfg,"mrcpv2-properties",pool);
	uni_engine_properties_load(&uni_engine.v1_properties,cfg,"mrcpv1-properties",pool);

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

	uni_engine_properties_init(&uni_engine.v2_properties,MRCP_VERSION_2);
	uni_engine_properties_init(&uni_engine.v1_properties,MRCP_VERSION_1);

	pool = apt_pool_create();
	if(!pool) {
		ast_log(LOG_ERROR, "Failed to create APR pool\n");
		uni_engine_unload();
		return FALSE;
	}

	uni_engine.pool = pool;

	/* Load engine configuration */
	uni_engine_config_load(pool);

	if(!uni_engine.profile) {
		uni_engine.profile = "uni2";
	}

	dir_layout = apt_default_dir_layout_create(UNIMRCP_DIR_LOCATION,pool);
	/* Create singleton logger */
	apt_log_instance_create(uni_engine.log_output, uni_engine.log_level, pool);
	/* Open the log file */
	apt_log_file_open(dir_layout->log_dir_path,"astuni",MAX_LOG_FILE_SIZE,MAX_LOG_FILE_COUNT,pool);


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
