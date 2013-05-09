/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Molo Afrika Speech Technologies (Pty) Ltd
 *
 * J.W.F. Thirion <derik@molo.co.za>
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

/* By Molo Afrika Speech Technologies (Pty) Ltd
 *     See: http://www.molo.co.za
 *
 * Ideas, concepts and code borrowed from UniMRCP's example programs
 * and the FreeSWITCH mod_unimrcp ASR/TTS module.
 *
 * Authors of these are:
 *     UniMRCP:
 *         Arsen Chaloyan <achaloyan@gmail.com>
 *     FreeSWITCH: mod_unimrcp
 *         Christopher M. Rienzo <chris@rienzo.net>
 *
 * See:
 *     http://www.unimrcp.org
 *     http://www.freeswitch.org
 */

/*! \file
 *
 * \brief MRCPRecog application
 *
 * \author\verbatim J.W.F. Thirion <derik@molo.co.za> \endverbatim
 * 
 * MRCPRecog application
 * \ingroup applications
 */

/* Asterisk includes. */
#include "ast_compat_defs.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"

#include "audio_queue.h"
#include "speech_channel.h"

/*** DOCUMENTATION
	<application name="MRCPRecog" language="en_US">
		<synopsis>
			MRCP recognition application.
		</synopsis>
		<syntax>
			<parameter name="grammar" required="true"/>
			<parameter name="options" required="true"/>
		</syntax>
		<description>
		<para>MRCP recognition application.
		Supports version 1 and 2 of MRCP, using UniMRCP. First parameter is grammar /
		text of speech. Second paramater contains more options: p=profile, i=interrupt
		key, t=auto speech timeout, f=filename of prompt to play, b=bargein value (no
		barge-in=0, ASR engine barge-in=1, Asterisk barge-in=2, ct=confidence
		threshold (0.0 - 1.0), sl=sensitivity level (0.0 - 1.0), sva=speed vs accuracy
		(0.0 - 1.0), nb=n-best list length (1 - 19 digits), nit=no input timeout (1 -
		19 digits), sit=start input timers (true/false), sct=speech complete timeout
		(1 - 19 digits), sint=speech incomplete timeout (1 - 19 digits), dit=DTMF
		interdigit timeout (1 - 19 digits), dtt=DTMF terminate timout (1 - 19 digits),
		dttc=DTMF terminate characters, sw=save waveform (true/false), nac=new audio
		channel (true/false), spl=speech language (en-US/en-GB/etc.), rm=recognition
		mode, hmaxd=hotword max duration (1 - 19 digits), hmind=hotword min duration
		(1 - 19 digits), cdb=clear DTMF buffer (true/false), enm=early no match
		(true/false), iwu=input waveform URI, mt=media type.</para>
		</description>
	</application>
 ***/

/* The name of the application. */
static const char *app_recog = "MRCPRecog";

#if !AST_VERSION_AT_LEAST(1,6,2)
static char *recogsynopsis = "MRCP recognition application.";
static char *recogdescrip =
"Supports version 1 and 2 of MRCP, using UniMRCP. First parameter is grammar /\n"
"text of speech. Second paramater contains more options: p=profile, i=interrupt\n"
"key, t=auto speech timeout, f=filename of prompt to play, b=bargein value (no\n"
"barge-in=0, ASR engine barge-in=1, Asterisk barge-in=2, ct=confidence\n"
"threshold (0.0 - 1.0), sl=sensitivity level (0.0 - 1.0), sva=speed vs accuracy\n"
"(0.0 - 1.0), nb=n-best list length (1 - 19 digits), nit=no input timeout (1 -\n"
"19 digits), sit=start input timers (true/false), sct=speech complete timeout\n"
"(1 - 19 digits), sint=speech incomplete timeout (1 - 19 digits), dit=DTMF\n"
"interdigit timeout (1 - 19 digits), dtt=DTMF terminate timout (1 - 19 digits),\n"
"dttc=DTMF terminate characters, sw=save waveform (true/false), nac=new audio\n"
"channel (true/false), spl=speech language (en-US/en-GB/etc.), rm=recognition\n"
"mode, hmaxd=hotword max duration (1 - 19 digits), hmind=hotword min duration\n"
"(1 - 19 digits), cdb=clear DTMF buffer (true/false), enm=early no match\n"
"(true/false), iwu=input waveform URI, mt=media type.\n";
#endif

static ast_mrcp_application_t *mrcprecog = NULL;

/* The enumeration of application options (excluding the MRCP params). */
enum mrcprecog_option_flags {
	MRCPRECOG_PROFILE             = (1 << 0),
	MRCPRECOG_INTERRUPT           = (2 << 0),
	MRCPRECOG_FILENAME            = (3 << 0),
	MRCPRECOG_BARGEIN             = (4 << 0),
	MRCPRECOG_GRAMMAR_DELIMITERS  = (5 << 0),
	MRCPRECOG_EXIT_ON_PLAYERROR   = (6 << 0),
	MRCPRECOG_URI_ENCODED_RESULTS = (7 << 0)
};

/* The enumeration of option arguments. */
enum mrcprecog_option_args {
	OPT_ARG_PROFILE              = 0,
	OPT_ARG_INTERRUPT            = 1,
	OPT_ARG_FILENAME             = 2,
	OPT_ARG_BARGEIN              = 3,
	OPT_ARG_GRAMMAR_DELIMITERS   = 4,
	OPT_ARG_EXIT_ON_PLAYERROR    = 5,
	OPT_ARG_URI_ENCODED_RESULTS  = 6,

	/* This MUST be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE           = 7
};

/* The structure which holds the application options (including the MRCP params). */
struct mrcprecog_options_t {
	apr_hash_t *recog_hfs;

	int         flags;
	const char *params[OPT_ARG_ARRAY_SIZE];
};

typedef struct mrcprecog_options_t mrcprecog_options_t;

#define DSP_FRAME_ARRAY_SIZE					1024

/* --- MRCP SPEECH CHANNEL INTERFACE TO UNIMRCP --- */

/* Handle the UniMRCP responses sent to session terminate requests. */
static apt_bool_t speech_on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel;

	if (session != NULL)
		schannel = (speech_channel_t *)mrcp_application_session_object_get(session);
	else
		schannel = NULL;

	ast_log(LOG_DEBUG, "speech_on_session_terminate\n");

	if (schannel != NULL) {
		if (schannel->dtmf_generator != NULL) {
			ast_log(LOG_NOTICE, "(%s) DTMF generator destroyed\n", schannel->name);
			mpf_dtmf_generator_destroy(schannel->dtmf_generator);
			schannel->dtmf_generator = NULL;
		}

		ast_log(LOG_DEBUG, "(%s) Destroying MRCP session\n", schannel->name);

		if (!mrcp_application_session_destroy(session))
			ast_log(LOG_WARNING, "(%s) Unable to destroy application session\n", schannel->name);

		speech_channel_set_state(schannel, SPEECH_CHANNEL_CLOSED);
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* Handle the UniMRCP responses sent to channel add requests. */
static apt_bool_t speech_on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel;

	if (channel != NULL)
		schannel = (speech_channel_t *)mrcp_application_channel_object_get(channel);
	else
		schannel = NULL;

	ast_log(LOG_DEBUG, "speech_on_channel_add\n");

	if ((schannel != NULL) && (application != NULL) && (session != NULL) && (channel != NULL)) {
		if ((session != NULL) && (status == MRCP_SIG_STATUS_CODE_SUCCESS)) {
			const mpf_codec_descriptor_t *descriptor = descriptor = mrcp_application_source_descriptor_get(channel);
			if (!descriptor) {
				ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				ast_log(LOG_DEBUG, "Terminating MRCP session\n");
				if (!mrcp_application_session_terminate(session))
					ast_log(LOG_WARNING, "(%s) %s unable to terminate application session\n", schannel->name, speech_channel_type_to_string(schannel->type));
				return FALSE;
			}

			if (schannel->stream != NULL) {
				schannel->dtmf_generator = mpf_dtmf_generator_create(schannel->stream, schannel->pool);
				/* schannel->dtmf_generator = mpf_dtmf_generator_create_ex(schannel->stream, MPF_DTMF_GENERATOR_OUTBAND, 70, 50, schannel->pool); */

				if (schannel->dtmf_generator != NULL)
					ast_log(LOG_NOTICE, "(%s) DTMF generator created\n", schannel->name);
				else
					ast_log(LOG_WARNING, "(%s) Unable to create DTMF generator\n", schannel->name);
			}

			schannel->rate = descriptor->sampling_rate;
			const char *codec_name = NULL;
			if (descriptor->name.length > 0)
				codec_name = descriptor->name.buf;
			else
				codec_name = "unknown";

			ast_log(LOG_DEBUG, "(%s) %s channel is ready, codec = %s, sample rate = %d\n",
				schannel->name,
				speech_channel_type_to_string(schannel->type),
				codec_name,
				schannel->rate);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
		} else {
			ast_log(LOG_ERROR, "(%s) %s channel error!\n", schannel->name, speech_channel_type_to_string(schannel->type));

			if (session != NULL) {
				ast_log(LOG_DEBUG, "Terminating MRCP session\n");
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);

				if (!mrcp_application_session_terminate(session))
					ast_log(LOG_WARNING, "(%s) %s unable to terminate application session\n", schannel->name, speech_channel_type_to_string(schannel->type));
			}
		}
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* Handle the UniMRCP responses sent to channel remove requests. */
static apt_bool_t speech_on_channel_remove(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel;

	if (channel != NULL)
		schannel = (speech_channel_t *)mrcp_application_channel_object_get(channel);
	else
		schannel = NULL;

	ast_log(LOG_DEBUG, "speech_on_channel_remove\n");

	if (schannel != NULL) {
		ast_log(LOG_NOTICE, "(%s) %s channel is removed\n", schannel->name, speech_channel_type_to_string(schannel->type));
		schannel->unimrcp_channel = NULL;

		if (session != NULL) {
			ast_log(LOG_DEBUG, "(%s) Terminating MRCP session\n", schannel->name);

			if (!mrcp_application_session_terminate(session))
				ast_log(LOG_WARNING, "(%s) %s unable to terminate application session\n", schannel->name, speech_channel_type_to_string(schannel->type));
		}
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* --- MRCP ASR --- */

/* Check if recognition is complete. */
static int recog_channel_check_results(speech_channel_t *schannel)
{
	int status = 0;
	recognizer_data_t *r;

	if (schannel != NULL) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if ((r = (recognizer_data_t *)schannel->data) != NULL) {
			if ((r->result != NULL) && (strlen(r->result) > 0))
				ast_log(LOG_DEBUG, "(%s) SUCCESS, have result\n", schannel->name);
			else if (r->start_of_input)
				ast_log(LOG_DEBUG, "(%s) SUCCESS, start of input\n", schannel->name);
			else
				status = -1;
		} else {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);
			status = -1;
		}
	} else {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		status = -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}
#if 0
/* Start recognizer's input timers. */
static int recog_channel_start_input_timers(speech_channel_t *schannel)
{   
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if ((schannel->state == SPEECH_CHANNEL_PROCESSING) && (!r->timers_started)) {
		mrcp_message_t *mrcp_message;
		ast_log(LOG_DEBUG, "(%s) Starting input timers\n", schannel->name);

		/* Send START-INPUT-TIMERS to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_START_INPUT_TIMERS);

		if (mrcp_message == NULL) {
			ast_log(LOG_ERROR, "(%s) Failed to create START-INPUT-TIMERS message\n", schannel->name);
			status = -1;
		} else {
			/* Set it and forget it. */
			mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message);
		}
	}
 
	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}
#endif

/* Flag that input has started. */
static int recog_channel_set_start_of_input(speech_channel_t *schannel)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	r->start_of_input = 1;
	ast_log(LOG_DEBUG, "(%s) start of input\n", schannel->name);

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Set the recognition results. */
static int recog_channel_set_results(speech_channel_t *schannel, const char *result)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if (r->result && (strlen(r->result) > 0)) {
		ast_log(LOG_DEBUG, "(%s) result is already set\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if ((result == NULL) || (strlen(result) == 0)) {
		ast_log(LOG_DEBUG, "(%s) result is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	ast_log(LOG_DEBUG, "(%s) result:\n\n%s\n", schannel->name, result);
	r->result = apr_pstrdup(schannel->pool, result);

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Get the recognition results. */
static int recog_channel_get_results(speech_channel_t *schannel, int uri_encoded, const char **result)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if (r->result && (strlen(r->result) > 0)) {
		if (uri_encoded == 0) {
			*result = apr_pstrdup(schannel->pool, r->result);
		}
		else {
			apr_size_t len = strlen(r->result) * 2;
			char *res = apr_palloc(schannel->pool, len);
			*result = ast_uri_encode_http(r->result, res, len);
		}
		ast_log(LOG_DEBUG, "(%s) result:\n\n%s\n", schannel->name, *result);
		r->result = NULL;
		r->start_of_input = 0;
	} else if (r->start_of_input) {
		ast_log(LOG_DEBUG, "(%s) start of input\n", schannel->name);
		status = 1;
		r->start_of_input = 0;
	} else
		status = -1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Flag that the recognizer channel timers are started. */
static int recog_channel_set_timers_started(speech_channel_t *schannel)
{
	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	r->timers_started = 1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return 0;
}

/* Start RECOGNIZE request. */
static int recog_channel_start(speech_channel_t *schannel, const char *name, apr_hash_t *header_fields)
{
	int status = 0;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_recog_header_t *recog_header = NULL;
	recognizer_data_t *r = NULL;
	char *start_input_timers = NULL;
	const char *mime_type = NULL;
	grammar_t *grammar = NULL;

	if ((schannel != NULL) && (name != NULL)) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if (schannel->data == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if ((r = (recognizer_data_t *)schannel->data) == NULL) {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		r->result = NULL;
		r->start_of_input = 0;

		/* Input timers are started by default unless the start-input-timers=false param is set. */
		start_input_timers = (char *)apr_hash_get(header_fields, "Start-Input-Timers", APR_HASH_KEY_STRING);
		r->timers_started = (start_input_timers == NULL) || (strlen(start_input_timers) == 0) || (strcasecmp(start_input_timers, "false"));

		apr_hash_index_t *hi;
		void *val;
		int length = 0;
		char grammar_refs[4096];
		for (hi = apr_hash_first(schannel->pool, r->grammars); hi; hi = apr_hash_next(hi)) {
			apr_hash_this(hi, NULL, NULL, &val);
			grammar = val;
			if (!grammar) 	continue;

			int grammar_len = strlen(grammar->data);
			if (length + grammar_len + 2 > sizeof(grammar_refs) - 1) {
				break;
			}

			if (length) {
				grammar_refs[length++] = '\r';
				grammar_refs[length++] = '\n';
			}
			memcpy(grammar_refs + length, grammar->data, grammar_len);
			length += grammar_len;
		}
		if (length == 0) {
			ast_log(LOG_ERROR, "(%s) No grammars specified\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}
		grammar_refs[length] = '\0';

		/* Create MRCP message. */
		if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_RECOGNIZE)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Allocate generic header. */
		if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Set Content-Type. */
		apt_string_assign(&generic_header->content_type, "text/uri-list", mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

		/* Allocate recognizer-specific header. */
		if ((recog_header = (mrcp_recog_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Set Cancel-If-Queue. */
		if (mrcp_message->start_line.version == MRCP_VERSION_2) {
			recog_header->cancel_if_queue = FALSE;
			mrcp_resource_header_property_add(mrcp_message, RECOGNIZER_HEADER_CANCEL_IF_QUEUE);
		}

		/* Set parameters. */
		speech_channel_set_params(schannel, mrcp_message, header_fields);

		/* Set message body. */
		apt_string_assign_n(&mrcp_message->body, grammar_refs, length, mrcp_message->pool);

		/* Empty audio queue and send RECOGNIZE to MRCP server. */
		audio_queue_clear(schannel->audio_queue);

		if (mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message) == FALSE) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Wait for IN PROGRESS. */
		if ((schannel->mutex != NULL) && (schannel->cond != NULL))
			apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

		if (schannel->state != SPEECH_CHANNEL_PROCESSING) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return status;
}

/* Load speech recognition grammar. */
static int recog_channel_load_grammar(speech_channel_t *schannel, const char *name, grammar_type_t type, const char *data)
{
	int status = 0;
	grammar_t *g = NULL;
	char ldata[256];

	if ((schannel != NULL) && (name != NULL) && (data != NULL)) {
		ast_log(LOG_DEBUG, "(%s) Loading grammar %s, data = %s\n", schannel->name, name, data);

		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* If inline, use DEFINE-GRAMMAR to cache it on the server. */
		if (type != GRAMMAR_TYPE_URI) {
			mrcp_message_t *mrcp_message;
			mrcp_generic_header_t *generic_header;
			const char *mime_type;

			/* Create MRCP message. */
			if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_DEFINE_GRAMMAR)) == NULL) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			/* Set Content-Type and Content-ID in message. */
			if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			if (((mime_type = grammar_type_to_mime(type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			apt_string_assign(&generic_header->content_type, mime_type, mrcp_message->pool);
			mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);
			apt_string_assign(&generic_header->content_id, name, mrcp_message->pool);
			mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_ID);

			/* Put grammar in message body. */
			apt_string_assign(&mrcp_message->body, data, mrcp_message->pool);

			/* Send message and wait for response. */
			speech_channel_set_state_unlocked(schannel, SPEECH_CHANNEL_PROCESSING);

			if (mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message) == FALSE) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			if ((schannel->mutex != NULL) && (schannel->cond != NULL))
				apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

			if (schannel->state != SPEECH_CHANNEL_READY) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			/* Set up name, type for future RECOGNIZE requests.  We'll reference this cached grammar by name. */
			apr_snprintf(ldata, sizeof(ldata) - 1, "session:%s", name);
			ldata[sizeof(ldata) - 1] = '\0';

			data = ldata;
			type = GRAMMAR_TYPE_URI;
		}

		/* Create the grammar and save it. */
		if ((status = grammar_create(&g, name, type, data, schannel->pool)) == 0) {
			recognizer_data_t *r = (recognizer_data_t *)schannel->data;
	
			if (r != NULL)
				apr_hash_set(r->grammars, apr_pstrdup(schannel->pool, g->name), APR_HASH_KEY_STRING, g);
		}

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return status;
}

/* Process messages from UniMRCP for the recognizer application. */
static apt_bool_t recog_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message != NULL)
		return mrcp_application_message_dispatch(&mrcprecog->dispatcher, app_message);
	else {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return TRUE;
	}
}

/* Handle the MRCP responses/events. */
static apt_bool_t recog_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel;

	if (channel != NULL)
		schannel = (speech_channel_t *)mrcp_application_channel_object_get(channel);
	else
		schannel = NULL;

	if ((schannel != NULL) && (application != NULL) && (session != NULL) && (channel != NULL) && (message != NULL)) {
		mrcp_recog_header_t *recog_hdr = (mrcp_recog_header_t *)mrcp_resource_header_get(message);
		if (message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
			/* Received MRCP response. */
			if (message->start_line.method_id == RECOGNIZER_RECOGNIZE) {
				/* Received the response to RECOGNIZE request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_INPROGRESS) {
					/* RECOGNIZE in progress. */
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE IN PROGRESS\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_PROCESSING);
				} else if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					/* RECOGNIZE failed to start. */
					if (recog_hdr->completion_cause == RECOGNIZER_COMPLETION_CAUSE_UNKNOWN)
						ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d\n", schannel->name,	 message->start_line.status_code);
					else
						ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d, completion-cause = %03d\n", schannel->name, message->start_line.status_code, recog_hdr->completion_cause);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				} else if (message->start_line.request_state == MRCP_REQUEST_STATE_PENDING)
					/* RECOGNIZE is queued. */
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE PENDING\n", schannel->name);
				else {
					/* Received unexpected request_state. */
					ast_log(LOG_DEBUG, "(%s) unexpected RECOGNIZE request state: %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == RECOGNIZER_STOP) {
				/* Received response to the STOP request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					/* Got COMPLETE. */
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE STOPPED\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
				} else {
					/* Received unexpected request state. */
					ast_log(LOG_DEBUG, "(%s) unexpected STOP request state: %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == RECOGNIZER_START_INPUT_TIMERS) {
				/* Received response to START-INPUT-TIMERS request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) timers started\n", schannel->name);
						recog_channel_set_timers_started(schannel);
					} else
						ast_log(LOG_DEBUG, "(%s) timers failed to start, status code = %d\n", schannel->name, message->start_line.status_code);
				}
			} else if (message->start_line.method_id == RECOGNIZER_DEFINE_GRAMMAR) {
				/* Received response to DEFINE-GRAMMAR request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) grammar loaded\n", schannel->name);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
					} else {
						ast_log(LOG_DEBUG, "(%s) grammar failed to load, status code = %d\n", schannel->name, message->start_line.status_code);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
					}
				}
			} else {
				/* Received unexpected response. */
				ast_log(LOG_DEBUG, "(%s) unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else if (message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
			/* Received MRCP event. */
			if (message->start_line.method_id == RECOGNIZER_RECOGNITION_COMPLETE) {
				ast_log(LOG_DEBUG, "(%s) RECOGNITION COMPLETE, Completion-Cause: %03d\n", schannel->name, recog_hdr->completion_cause);
				if (message->body.length > 0) {
					if (message->body.buf[message->body.length - 1] == '\0') {
						recog_channel_set_results(schannel, message->body.buf);
					} else {
						/* string is not null terminated */
						char *result = (char *)apr_palloc(schannel->pool, message->body.length + 1);
						ast_log(LOG_DEBUG, "(%s) Recognition result is not null-terminated.  Appending null terminator\n", schannel->name);
						strncpy(result, message->body.buf, message->body.length);
						result[message->body.length] = '\0';
						recog_channel_set_results(schannel, result);
					}
				} else {
					char completion_cause[512];
					char waveform_uri[256];
					apr_snprintf(completion_cause, sizeof(completion_cause) - 1, "Completion-Cause: %03d", recog_hdr->completion_cause);
					completion_cause[sizeof(completion_cause) - 1] = '\0';
					apr_snprintf(waveform_uri, sizeof(waveform_uri) - 1, "Waveform-URI: %s", recog_hdr->waveform_uri.buf);
					waveform_uri[sizeof(waveform_uri) - 1] = '\0';
					if (recog_hdr->waveform_uri.length > 0) {
#if AST_VERSION_AT_LEAST(1,6,2)
						strncat(completion_cause,",", 1);
#else
						strncat(completion_cause,"|", 1);
#endif
						strncat(completion_cause, waveform_uri, strlen(waveform_uri) );
					}
					recog_channel_set_results(schannel, completion_cause);
					ast_log(LOG_DEBUG, "(%s) No result\n", schannel->name);
				}
				speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
			} else if (message->start_line.method_id == RECOGNIZER_START_OF_INPUT) {
				ast_log(LOG_DEBUG, "(%s) START OF INPUT\n", schannel->name);
				if (schannel->chan != NULL) {
					ast_log(LOG_DEBUG, "(%s) Stopping playback due to start of input\n", schannel->name);
					ast_stopstream(schannel->chan);
				}
				recog_channel_set_start_of_input(schannel);
			} else {
				ast_log(LOG_DEBUG, "(%s) unexpected event, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else {
			ast_log(LOG_DEBUG, "(%s) unexpected message type, message_type = %d\n", schannel->name, message->start_line.message_type);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
		}
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* UniMRCP callback requesting stream to be opened. */
static apt_bool_t recog_stream_open(mpf_audio_stream_t* stream, mpf_codec_t *codec)
{
	speech_channel_t* schannel;

	if (stream != NULL)
		schannel = (speech_channel_t*)stream->obj;
	else
		schannel = NULL;

	schannel->stream = stream;

	if ((schannel == NULL) || (stream == NULL))
		ast_log(LOG_ERROR, "(unknown) channel error opening stream!\n");

	return TRUE;
}

/* UniMRCP callback requesting next frame for speech recognition. */
static apt_bool_t recog_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
	speech_channel_t *schannel;

	if (stream != NULL)
		schannel = (speech_channel_t *)stream->obj;
	else
		schannel = NULL;

	if ((schannel != NULL) && (stream != NULL) && (frame != NULL)) {
		if (schannel->dtmf_generator != NULL) {
			if (mpf_dtmf_generator_sending(schannel->dtmf_generator)) {
				ast_log(LOG_DEBUG, "(%s) DTMF frame written\n", schannel->name);
				mpf_dtmf_generator_put_frame(schannel->dtmf_generator, frame);
				return TRUE;
			}
		}

		apr_size_t to_read = frame->codec_frame.size;

		/* Grab the data. Pad it if there isn't enough. */
		if (speech_channel_read(schannel, frame->codec_frame.buffer, &to_read, 0) == 0) {
			if (to_read < frame->codec_frame.size)
				memset((apr_byte_t *)frame->codec_frame.buffer + to_read, schannel->silence, frame->codec_frame.size - to_read);

			frame->type |= MEDIA_FRAME_TYPE_AUDIO;
		}
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* Apply application options. */
static int mrcprecog_option_apply(mrcprecog_options_t *options, const char *key, const char *value)
{
	if (strcasecmp(key, "ct") == 0) {
		apr_hash_set(options->recog_hfs, "Confidence-Threshold", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sva") == 0) {
		apr_hash_set(options->recog_hfs, "Speed-vs-Accuracy", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nb") == 0) {
		apr_hash_set(options->recog_hfs, "N-Best-List-Length", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nit") == 0) {
		apr_hash_set(options->recog_hfs, "No-Input-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sit") == 0) {
		apr_hash_set(options->recog_hfs, "Start-Input-Timers", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sct") == 0) {
		apr_hash_set(options->recog_hfs, "Speech-Complete-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sint") == 0) {
		apr_hash_set(options->recog_hfs, "Speech-Incomplete-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "dit") == 0) {
		apr_hash_set(options->recog_hfs, "Dtmf-Interdigit-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "dtt") == 0) {
		apr_hash_set(options->recog_hfs, "Dtmf-Term-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "dttc") == 0) {
		apr_hash_set(options->recog_hfs, "Dtmf-Term-Char", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sw") == 0) {
		apr_hash_set(options->recog_hfs, "Save-Waveform", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nac") == 0) {
		apr_hash_set(options->recog_hfs, "New-Audio-Channel", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "rm") == 0) {
		apr_hash_set(options->recog_hfs, "Recognition-Mode", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "hmaxd") == 0) {
		apr_hash_set(options->recog_hfs, "Hotword-Max-Duration", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "hmind") == 0) {
		apr_hash_set(options->recog_hfs, "Hotword-Min-Duration", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "cdb") == 0) {
		apr_hash_set(options->recog_hfs, "Clear-Dtmf-Buffer", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "enm") == 0) {
		apr_hash_set(options->recog_hfs, "Early-No-Match", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "iwu") == 0) {
		apr_hash_set(options->recog_hfs, "Input-Waveform-URI", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sl") == 0) {
		apr_hash_set(options->recog_hfs, "Sensitivity-Level", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "spl") == 0) {
		apr_hash_set(options->recog_hfs, "Speech-Language", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "mt") == 0) {
		apr_hash_set(options->recog_hfs, "Media-Type", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "p") == 0) {
		options->flags |= MRCPRECOG_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
	} else if (strcasecmp(key, "i") == 0) {
		options->flags |= MRCPRECOG_INTERRUPT;
		options->params[OPT_ARG_INTERRUPT] = value;
	} else if (strcasecmp(key, "f") == 0) {
		options->flags |= MRCPRECOG_FILENAME;
		options->params[OPT_ARG_FILENAME] = value;
	} else if (strcasecmp(key, "t") == 0) {
		apr_hash_set(options->recog_hfs, "Recognition-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "b") == 0) {
		options->flags |= MRCPRECOG_BARGEIN;
		options->params[OPT_ARG_BARGEIN] = value;
	} else if (strcasecmp(key, "gd") == 0) {
		options->flags |= MRCPRECOG_GRAMMAR_DELIMITERS;
		options->params[OPT_ARG_GRAMMAR_DELIMITERS] = value;
	} else if (strcasecmp(key, "epe") == 0) {
		options->flags |= MRCPRECOG_EXIT_ON_PLAYERROR;
		options->params[OPT_ARG_EXIT_ON_PLAYERROR] = value;
	} else if (strcasecmp(key, "uer") == 0) {
		options->flags |= MRCPRECOG_URI_ENCODED_RESULTS;
		options->params[OPT_ARG_URI_ENCODED_RESULTS] = value;
	}
	else {
		ast_log(LOG_WARNING, "Unknown option: %s\n", key);
	}
	return 0;
}

/* Parse application options. */
static int mrcprecog_options_parse(char *str, mrcprecog_options_t *options, apr_pool_t *pool)
{
	char *s;
	char *name, *value;
	
	if (!str) {
		return 0;
	}

	ast_log(LOG_NOTICE, "Parse options: %s\n", str);
	if ((options->recog_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	while ((s = strsep(&str, "&"))) {
		value = s;
		if ((name = strsep(&value, "=")) && value) {
			ast_log(LOG_NOTICE, "Apply option: %s: %s\n", name, value);
			mrcprecog_option_apply(options, name, value);
		}
	}
	return 0;
}

/* Exit the application. */
static int mrcprecog_exit(struct ast_channel *chan, speech_channel_t *schannel, apr_pool_t *pool, int res)
{
	if (!pool)
		return -1;

	if (schannel) {
		speech_channel_destroy(schannel);
		schannel = NULL;
	}

	if (res < 0)
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "ERROR");
	else
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "OK");

	if ((res < 0) && (!ast_check_hangup(chan)))
		res = 0;

	apr_pool_destroy(pool);
	return res;
}

/* The entry point of the application. */
static int app_recog_exec(struct ast_channel *chan, ast_app_data data)
{
	int samplerate = 8000;
	int dtmf_enable;
	struct ast_frame *f = NULL;
	apr_size_t len;
	int rres = 0;
	speech_channel_t *schannel = NULL;
	ast_mrcp_profile_t *profile = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	const char *name;
	int waitres = 0;
	int res = 0;
	char *parse;
	apr_pool_t *pool;
	int i;
	mrcprecog_options_t mrcprecog_options;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(grammar);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (grammar[,options])\n", app_recog);
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "ERROR");
		return -1;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.grammar)) {
		ast_log(LOG_WARNING, "%s requires a grammar argument (grammar[,options])\n", app_recog);
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "ERROR");
		return -1;
	}

	if ((pool = apt_pool_create()) == NULL) {
		ast_log(LOG_ERROR, "Unable to create memory pool for channel\n");
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "ERROR");
		return -1;
	}

	mrcprecog_options.recog_hfs = NULL;
	mrcprecog_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		mrcprecog_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		char *options_buf = apr_pstrdup(pool, args.options);
		mrcprecog_options_parse(options_buf, &mrcprecog_options, pool);
	}

	int speech = 0;
	struct timeval start = { 0, 0 };
	struct timeval detection_start = { 0, 0 };
	int min = 100;
	struct ast_dsp *dsp = NULL;

	int bargein = 1;
	if ((mrcprecog_options.flags & MRCPRECOG_BARGEIN) == MRCPRECOG_BARGEIN) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_BARGEIN])) {
			bargein = atoi(mrcprecog_options.params[OPT_ARG_BARGEIN]);
			if ((bargein < 0) || (bargein > 2))
				bargein = 1;
		}
	}

	dtmf_enable = 2;
	if ((mrcprecog_options.flags & MRCPRECOG_INTERRUPT) == MRCPRECOG_INTERRUPT) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_INTERRUPT])) {
			dtmf_enable = 1;
			if (strcasecmp(mrcprecog_options.params[OPT_ARG_INTERRUPT], "any") == 0)
				mrcprecog_options.params[OPT_ARG_INTERRUPT] = AST_DIGIT_ANY;
			else if (strcasecmp(mrcprecog_options.params[OPT_ARG_INTERRUPT], "none") == 0)
				dtmf_enable = 2;
			else if (strcasecmp(mrcprecog_options.params[OPT_ARG_INTERRUPT], "disable") == 0)
				dtmf_enable = 0;
		}
	}
	ast_log(LOG_NOTICE, "DTMF enable: %d\n", dtmf_enable);

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	ast_format_compat nreadformat;
	ast_format_clear(&nreadformat);
	get_recog_format(chan, &nreadformat);

	name = apr_psprintf(pool, "ASR-%lu", (unsigned long int)speech_channel_number);

	/* Create speech channel for recognition. */
	schannel = speech_channel_create(pool, name, SPEECH_CHANNEL_RECOGNIZER, mrcprecog, format_to_str(&nreadformat), samplerate, chan);
	if (schannel == NULL) {
		res = -1;
		return mrcprecog_exit(chan, schannel, pool, res);
	}

	const char *profile_name = NULL;
	if ((mrcprecog_options.flags & MRCPRECOG_PROFILE) == MRCPRECOG_PROFILE) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_PROFILE])) {
			profile_name = mrcprecog_options.params[OPT_ARG_PROFILE];
		}
	}

	/* Get recognition profile. */
	profile = get_recog_profile(profile_name);
	if (!profile) {
		ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", name, profile_name);
		res = -1;
		return mrcprecog_exit(chan, schannel, pool, res);
	}

	/* Open recognition channel. */
	if (speech_channel_open(schannel, profile) != 0) {
		res = -1;
		return mrcprecog_exit(chan, schannel, pool, res);
	}

	ast_format_compat oreadformat;
	ast_format_clear(&oreadformat);
	ast_channel_get_readformat(chan, &oreadformat);

	if (ast_channel_set_readformat(chan, &nreadformat) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format to signed linear\n");
		res = -1;
		speech_channel_stop(schannel);
		return mrcprecog_exit(chan, schannel, pool, res);
	}
	const char *grammar_delimiters = ",";
	/* Get grammar delimiters. */
	if ((mrcprecog_options.flags & MRCPRECOG_GRAMMAR_DELIMITERS) == MRCPRECOG_GRAMMAR_DELIMITERS) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_GRAMMAR_DELIMITERS])) {
			grammar_delimiters = mrcprecog_options.params[OPT_ARG_GRAMMAR_DELIMITERS];
			ast_log(LOG_DEBUG, "Grammar delimiters are: %s\n", grammar_delimiters);
		}
	}
	/* Parse the grammar argument into a sequence of grammars. */
	char *grammar_arg = apr_pstrdup(pool, args.grammar);
	char *last;
	char *grammar_str;
	char grammar_name[32];
	int grammar_id = 0;
	ast_log(LOG_DEBUG, "Tokenize grammar argument: %s\n", grammar_arg);
	grammar_str = apr_strtok(grammar_arg, grammar_delimiters, &last);
	while (grammar_str) {
		const char *grammar_content = NULL;
		grammar_type_t grammar_type = GRAMMAR_TYPE_UNKNOWN;
		ast_log(LOG_DEBUG, "Determine grammar type: %s\n", grammar_str);
		if (determine_grammar_type(schannel, grammar_str, &grammar_content, &grammar_type) != 0) {
			ast_log(LOG_WARNING, "Unable to determine grammar type\n");
			res = -1;
			speech_channel_stop(schannel);
			return mrcprecog_exit(chan, schannel, pool, res);
		}
		ast_log(LOG_DEBUG, "Grammar type is: %i\n", grammar_type);

		apr_snprintf(grammar_name, sizeof(grammar_name) - 1, "grammar-%d", grammar_id++);
		grammar_name[sizeof(grammar_name) - 1] = '\0';
		/* Load grammar. */
		if (recog_channel_load_grammar(schannel, grammar_name, grammar_type, grammar_content) != 0) {
			ast_log(LOG_ERROR, "Unable to load grammar\n");
			res = -1;
			speech_channel_stop(schannel);
			return mrcprecog_exit(chan, schannel, pool, res);
		}

		grammar_str = apr_strtok(NULL, grammar_delimiters, &last);
	}

	struct ast_filestream* fs = NULL;
	off_t filelength = 0;

	ast_stopstream(chan);

	/* Open file, get file length, seek to begin, apply and play. */ 
	if ((mrcprecog_options.flags & MRCPRECOG_FILENAME) == MRCPRECOG_FILENAME) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_FILENAME])) {
			int fileplay_started = 0;
			const char *filename = mrcprecog_options.params[OPT_ARG_FILENAME];
			if ((fs = ast_openstream(chan, filename, ast_channel_language(chan))) == NULL) {
				ast_log(LOG_WARNING, "ast_openstream failed on %s for %s\n", ast_channel_name(chan), filename);
			} else {
				if (ast_seekstream(fs, -1, SEEK_END) == -1) {
					ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), filename);
				} else {
					filelength = ast_tellstream(fs);
					ast_log(LOG_NOTICE, "file length:%"APR_OFF_T_FMT"\n", filelength);
				}

				if (ast_seekstream(fs, 0, SEEK_SET) == -1) {
					ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), filename);
				} else if (ast_applystream(chan, fs) == -1) {
					ast_log(LOG_WARNING, "ast_applystream failed on %s for %s\n", ast_channel_name(chan), filename);
				} else if (ast_playstream(fs) == -1) {
					ast_log(LOG_WARNING, "ast_playstream failed on %s for %s\n", ast_channel_name(chan), filename);
				}
				else {
					fileplay_started = 1;
				}
			}

			if (fileplay_started) {
				if (bargein == 0) {
					res = ast_waitstream(chan, "");
				}
			}
			else {
				int exit_on_playerror = 0;
				if ((mrcprecog_options.flags & MRCPRECOG_EXIT_ON_PLAYERROR) == MRCPRECOG_EXIT_ON_PLAYERROR) {
					if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_EXIT_ON_PLAYERROR])) {
						exit_on_playerror = atoi(mrcprecog_options.params[OPT_ARG_EXIT_ON_PLAYERROR]);
						if ((exit_on_playerror < 0) || (exit_on_playerror > 2))
							exit_on_playerror = 1;
					}
				}

				if (exit_on_playerror) {
					ast_log(LOG_ERROR, "Couldn't play file\n");
					res = -1;
					speech_channel_stop(schannel);
					return mrcprecog_exit(chan, schannel, pool, res);
				}
			}
		}
	}

	unsigned long int marka = 0;
	unsigned long int markb = 0;

	typedef struct _DSP_FRAME_DATA_T_ {
		struct ast_frame* f;
		int speech;
	} dsp_frame_data_t;

	dsp_frame_data_t **dsp_frame_data = NULL;
	dsp_frame_data_t* myFrame;

	unsigned long int dsp_frame_data_len = DSP_FRAME_ARRAY_SIZE;
	unsigned long int dsp_frame_count = 0;
	int dtmfkey = -1;

	waitres = 0;

	/* Speech detection start. */
	if (bargein == 2) {	
		struct ast_frame *f1;
		dsp_frame_data = (dsp_frame_data_t**)malloc(sizeof(dsp_frame_data_t*) * DSP_FRAME_ARRAY_SIZE);

		if (dsp_frame_data != NULL)
			memset(dsp_frame_data,0,sizeof(dsp_frame_data_t*) * DSP_FRAME_ARRAY_SIZE);

		int freeframe = 0;

		if (dsp_frame_data == NULL) {
			ast_log(LOG_ERROR, "Unable to allocate frame array\n");
			res = -1;
		} else if ((dsp = (struct ast_dsp*)ast_dsp_new()) == NULL) {
			ast_log(LOG_ERROR, "Unable to allocate DSP!\n");
			res = -1;
		} else {
			ast_log(LOG_DEBUG,"Entering speech START detection\n");
			int ssdres = 1;	
			/* Only start to transmit frames as soon as voice has been detected. */
			detection_start = ast_tvnow();

			while (((waitres = ast_waitfor(chan, 100)) >= 0)) {
				if (waitres == 0)
					continue;

				freeframe = 0; /* By default free frame. */
				f1 = ast_read(chan);

				if (!f1 || !fs) {
					ssdres = -1;
					break;
				}

				/* Break if prompt has finished - don't care about analysis time which is in effect equal to prompt length. */
				if ((filelength != 0) && ( ast_tellstream(fs) >= filelength)) {
					ast_log(LOG_NOTICE, "Prompt has finished playing, moving on.\n");
					ast_frfree(f1);
					break;
				}

				if (f1->frametype == AST_FRAME_VOICE) {
					myFrame = (dsp_frame_data_t*)malloc(sizeof(dsp_frame_data_t));

					if (myFrame == NULL) {
						ast_log(LOG_ERROR, "Failed to allocate a frame\n");
						ast_frfree(f1);
						break;
					}

					/* Store voice frame. */
					if (dsp_frame_count + 1 > dsp_frame_data_len) {
						dsp_frame_data_t** tmpData;
						tmpData = (dsp_frame_data_t**)realloc(dsp_frame_data, sizeof(dsp_frame_data_t*) * (dsp_frame_count + 1));

						if (tmpData != NULL) {
							dsp_frame_data = tmpData;
							dsp_frame_data_len++;
						} else {
							ast_log(LOG_ERROR, "Failure in storing frames, streaming directly\n");
							ast_frfree(f1);
							free(myFrame);
							break; /* Break out of detection loop, so one can rather just stream. */
						}
					}

					myFrame->f = f1;
					myFrame->speech = 0;

					dsp_frame_count++;
					dsp_frame_data[dsp_frame_count - 1] = myFrame;
					freeframe = 1; /* Do not free the frame, as it is stored */

					/* Continue analysis. */	
					int totalsilence;
					ssdres = ast_dsp_silence(dsp, f1, &totalsilence);
					int ms = 0;

					if (ssdres) { /* Glitch detection or detection after a small word. */
						if (speech) {
							/* We've seen speech in a previous frame. */
							/* We had heard some talking. */
							ms = ast_tvdiff_ms(ast_tvnow(), start);

							if (ms > min) {
								ast_log(LOG_DEBUG, "Found qualified token of %d ms\n", ms);
								markb = dsp_frame_count;
								myFrame->speech = 1;
								ast_stopstream(schannel->chan);
								break;
							} else {
								markb =0; marka =0;
								ast_log(LOG_DEBUG, "Found unqualified token of %d ms\n", ms);
								speech = 0;
							}
						}

						myFrame->speech = 0;
					} else {
						if (speech) {
							ms = ast_tvdiff_ms(ast_tvnow(), start);
						} else {
							/* Heard some audio, mark the begining of the token. */
							start = ast_tvnow();
							ast_log(LOG_DEBUG, "Start of voice token!\n");
							marka = dsp_frame_count;
						}

						speech = 1;
						myFrame->speech = 1;

						if (ms > min) {
							ast_log(LOG_DEBUG, "Found qualified speech token of %d ms\n", ms);
							markb = dsp_frame_count;
							ast_stopstream(schannel->chan);
							break;
						}
					}
				} else if (f1->frametype == AST_FRAME_VIDEO) {
					/* Ignore. */
				} else if ((dtmf_enable != 0) && (f1->frametype == AST_FRAME_DTMF)) {
					dtmfkey = ast_frame_get_dtmfkey(f1);
					ast_log(LOG_DEBUG, "ssd: User pressed DTMF key (%d)\n", dtmfkey);
					break;
				}

				if (freeframe == 0)
					ast_frfree(f1); 
			}
		}
	}
	/* End of speech detection. */

	i = 0;
	if (((dtmf_enable == 1) && (dtmfkey != -1)) || (waitres < 0)) {
		/* Skip as we have to return to specific dialplan extension, or an error occurred on the channel */
	} else {
		ast_log(LOG_NOTICE, "Recognizing\n");

		if (recog_channel_start(schannel, name, mrcprecog_options.recog_hfs) == 0) {
			if ((dtmfkey != -1) && (schannel->dtmf_generator != NULL)) {
				char digits[2];

				digits[0] = (char)dtmfkey;
				digits[1] = '\0';

				ast_log(LOG_NOTICE, "(%s) DTMF barge-in digit queued (%s)\n", schannel->name, digits);
				mpf_dtmf_generator_enqueue(schannel->dtmf_generator, digits);
				dtmfkey = -1;
			}

			/* Playback buffer of frames captured during. */
			int pres = 0;

			if (dsp_frame_data != NULL) {
				if ((bargein == 2) && (markb != 0)) {
					for (i = marka; i < markb; ++i) {
						if ((dsp_frame_data[i] != NULL) && (dsp_frame_data[i]->speech == 1)) {
							myFrame = dsp_frame_data[i];
							len = myFrame->f->datalen;
							rres = speech_channel_write(schannel, ast_frame_get_data(f), &len);
						}

						if (rres != 0)
							break;
					}

					if (pres != 0)
						ast_log(LOG_ERROR,"Could not transmit the playback (of SPEECH START DETECT)\n");
				}

				for (i = 0; i < dsp_frame_count; i++) {
					myFrame = dsp_frame_data[i];
					if (myFrame != NULL) {
						ast_frfree(myFrame->f); 
				free(dsp_frame_data[i]); 
					}
				}
				free(dsp_frame_data);
			}

			/* Continue with recognition. */
			while (((waitres = ast_waitfor(chan, 100)) >= 0)) {
				int processing = 1;

				if ((schannel != NULL) && (schannel->mutex != NULL)) {
					if (schannel->mutex != NULL) {
						apr_thread_mutex_lock(schannel->mutex);
					}

					if (schannel->state != SPEECH_CHANNEL_PROCESSING) {
						processing = 0;
					}

					if (schannel->mutex != NULL) {
						apr_thread_mutex_unlock(schannel->mutex);
					}
				}

				if (processing == 0)
					break;

				if (waitres == 0)
					continue;

				f = ast_read(chan);

				if (!f) {
					res = -1;
					break;
				}

				if (f->frametype == AST_FRAME_VOICE) {
					len = f->datalen;
					rres = speech_channel_write(schannel, ast_frame_get_data(f), &len);
					if (rres != 0)
						break;
				} else if (f->frametype == AST_FRAME_VIDEO) {
					/* Ignore. */
				} else if ((dtmf_enable != 0) && (f->frametype == AST_FRAME_DTMF)) {
					dtmfkey = ast_frame_get_dtmfkey(f);

					ast_log(LOG_DEBUG, "User pressed DTMF key (%d)\n", dtmfkey);
					if (dtmf_enable == 2) { /* Send dtmf frame to ASR engine. */
						if (schannel->dtmf_generator != NULL) {
							char digits[2];

							digits[0] = (char)dtmfkey;
							digits[1] = '\0';

							ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", schannel->name, digits);
							mpf_dtmf_generator_enqueue(schannel->dtmf_generator, digits);
							dtmfkey = -1;
						}
					} else if (dtmf_enable == 1) { /* Stop streaming and return DTMF value to the dialplan if within i chars. */
						if (strchr(mrcprecog_options.params[OPT_ARG_INTERRUPT], dtmfkey) || (strcmp(mrcprecog_options.params[OPT_ARG_INTERRUPT],"any")))
							break ; 

						/* Continue if not an i-key. */
					}
				}

				if (f != NULL)
					ast_frfree(f);
			}
		} else {
			ast_log(LOG_ERROR, "Unable to start recognition\n");
			res = -1;
		}
		if (!f) {
			ast_log(LOG_NOTICE, "Got hangup\n");
			res = -1;
		} else {
			if (bargein != 0) {
				res = ast_waitstream(chan, "");
			}
		}

		const char* result = NULL;

		if (recog_channel_check_results(schannel) == 0) {
			int uri_encoded_results = 0;
			/* Check if the results should be URI-encoded */
			if ((mrcprecog_options.flags & MRCPRECOG_URI_ENCODED_RESULTS) == MRCPRECOG_URI_ENCODED_RESULTS) {
				if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_URI_ENCODED_RESULTS])) {
					uri_encoded_results = (atoi(mrcprecog_options.params[OPT_ARG_URI_ENCODED_RESULTS]) == 0) ? 0 : 1;
				}
			}

			if (recog_channel_get_results(schannel, uri_encoded_results, &result) == 0) {
				ast_log(LOG_NOTICE, "Result=|%s|\n", result);
			} else {
				ast_log(LOG_ERROR, "Unable to retrieve result\n");
			}
		}

		if (result != NULL)
			pbx_builtin_setvar_helper(chan, "RECOG_RESULT", result);
		else
			pbx_builtin_setvar_helper(chan, "RECOG_RESULT", "");
	}

	if ((dtmf_enable == 1) && (dtmfkey != -1) && (res != -1))
		res = dtmfkey;

	ast_channel_set_readformat(chan, &oreadformat);

	speech_channel_stop(schannel);
	ast_stopstream(chan);
	
	return mrcprecog_exit(chan, schannel, pool, res);
}

/* Load MRCPRecog application. */
int load_mrcprecog_app()
{
	apr_pool_t *pool = globals.pool;

	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	if(mrcprecog) {
		ast_log(LOG_ERROR, "Application %s is already loaded\n", app_recog);
		return -1;
	}

	mrcprecog = (ast_mrcp_application_t*) apr_palloc(pool, sizeof(ast_mrcp_application_t));
	mrcprecog->name = app_recog;
	mrcprecog->exec = app_recog_exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	mrcprecog->synopsis = recogsynopsis;
	mrcprecog->description = recogdescrip;
#endif

	/* Create the recognizer application and link its callbacks */
	if ((mrcprecog->app = mrcp_application_create(recog_message_handler, (void *)0, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create recognizer MRCP application\n");
		mrcprecog = NULL;
		return -1;
	}

	mrcprecog->dispatcher.on_session_update = NULL;
	mrcprecog->dispatcher.on_session_terminate = speech_on_session_terminate;
	mrcprecog->dispatcher.on_channel_add = speech_on_channel_add;
	mrcprecog->dispatcher.on_channel_remove = speech_on_channel_remove;
	mrcprecog->dispatcher.on_message_receive = recog_on_message_receive;
	mrcprecog->audio_stream_vtable.destroy = NULL;
	mrcprecog->audio_stream_vtable.open_rx = recog_stream_open;
	mrcprecog->audio_stream_vtable.close_rx = NULL;
	mrcprecog->audio_stream_vtable.read_frame = recog_stream_read;
	mrcprecog->audio_stream_vtable.open_tx = NULL;
	mrcprecog->audio_stream_vtable.close_tx = NULL;
	mrcprecog->audio_stream_vtable.write_frame = NULL;
	mrcprecog->audio_stream_vtable.trace = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, mrcprecog->app, app_recog)) {
		ast_log(LOG_ERROR, "Unable to register recognizer MRCP application\n");
		if (!mrcp_application_destroy(mrcprecog->app))
			ast_log(LOG_WARNING, "Unable to destroy recognizer MRCP application\n");
		mrcprecog = NULL;
		return -1;
	}

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, mrcprecog);

	return 0;
}

/* Unload MRCPRecog application. */
int unload_mrcprecog_app()
{
	if(!mrcprecog) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", app_recog);
		return -1;
	}

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, NULL);
	mrcprecog = NULL;

	return 0;
}
