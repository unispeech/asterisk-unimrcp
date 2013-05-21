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
#include "recog_datastore.h"
#include "audio_queue.h"
#include "speech_channel.h"

/*** DOCUMENTATION
	<application name="MRCPRecog" language="en_US">
		<synopsis>
			MRCP recognition application.
		</synopsis>
		<syntax>
			<parameter name="grammar" required="true">
				<para>An inline or URI grammar to be used for recognition.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="p"> <para>Profile to use in mrcp.conf.</para> </option>
					<option name="i"> <para>Digits to allow recognition to be interrupted with
						(set to "none" for DTMF grammars to allow DTMFs to be sent to the MRCP server;
						otherwise, if "any" or other digits specified, recognition will be interrupted
						and the digit will be returned to dialplan).</para>
					</option>
					<option name="f"> <para>Filename to play (if empty or not specified, no file is played).</para> </option>
					<option name="t"> <para>Recognition timeout (msec).</para> </option>
					<option name="b"> <para>Bargein value (0: no barge-in, 1: enable barge-in).</para> </option>
					<option name="dt"> <para>Grammar delimiters.</para> </option>
					<option name="ct"> <para>Confidence threshold (0.0 - 1.0).</para> </option>
					<option name="sl"> <para>Sensitivity level (0.0 - 1.0).</para> </option>
					<option name="sva"> <para>Speed vs accuracy (0.0 - 1.0).</para> </option>
					<option name="nb"> <para>N-best list length.</para> </option>
					<option name="nit"> <para>No input timeout (msec).</para> </option>
					<option name="sit"> <para>Start input timers (true/false).</para> </option>
					<option name="sct"> <para>Speech complete timeout (msec).</para> </option>
					<option name="sint"> <para>Speech incomplete timeout (msec).</para> </option>
					<option name="dit"> <para>DTMF interdigit timeout (msec).</para> </option>
					<option name="dtt"> <para>DTMF terminate timeout (msec).</para> </option>
					<option name="dttc"> <para>DTMF terminate characters.</para> </option>
					<option name="sw"> <para>Save waveform (true/false).</para> </option>
					<option name="nac"> <para>New audio channel (true/false).</para> </option>
					<option name="spl"> <para>Speech language (e.g. "en-GB", "en-US", "en-AU", etc.).</para> </option>
					<option name="rm"> <para>Recognition mode (normal/hotword).</para> </option>
					<option name="hmaxd"> <para>Hotword max duration (msec).</para> </option>
					<option name="hmind"> <para>Hotword min duration (msec).</para> </option>
					<option name="cdb"> <para>Clear DTMF buffer (true/false).</para> </option>
					<option name="enm"> <para>Early nomatch (true/false).</para> </option>
					<option name="iwu"> <para>Input waveform URI.</para> </option>
					<option name="mt"> <para>Media type.</para> </option>
					<option name="epe"> <para>Exit on play error 
						(1: terminate recognition on file play error, 0: continue even if file play fails).</para>
					</option>
					<option name="uer"> <para>URI-encoded results 
						(1: URI-encode NLMSL results, 0: do not encode).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para> This application establishes an MRCP recognition session and optionally plays a prompt file.
			Once recognition completes, the application exits and returns results to the dialplan.</para>
			<para>If recognition successfully started, the variable ${RECOGSTATUS} is set to "OK"; otherwise, if recognition
			terminated prematurely, the variable ${RECOGSTATUS} is set to "ERROR".</para>
			<para>If recognition completed successfully, the variable ${RECOG_RESULT} is set to an NLSML result received from
			the MRCP server.</para>
		</description>
		<see-also>
			<ref type="application">MRCPSynth</ref>
			<ref type="application">SynthAndRecog</ref>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</application>
 ***/

/* The name of the application. */
static const char *app_recog = "MRCPRecog";

#if !AST_VERSION_AT_LEAST(1,6,2)
static char *recogsynopsis = "MRCP recognition application.";
static char *recogdescrip =
"Supports version 1 and 2 of MRCP, using UniMRCP. First parameter is grammar /\n"
"text of speech. Second paramater contains more options: p=profile, i=interrupt\n"
"key, t=speech recognition timeout, f=filename of prompt to play, b=bargein value\n"
"(no barge-in=0, allow barge-in=1), ct=confidence\n"
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

/* The application session. */
struct mrcprecog_session_t {
	apr_pool_t         *pool;
	speech_channel_t   *schannel;
	ast_format_compat  *readformat;
};

typedef struct mrcprecog_session_t mrcprecog_session_t;

/* --- MRCP SPEECH CHANNEL INTERFACE TO UNIMRCP --- */

/* Handle the UniMRCP responses sent to session terminate requests. */
static apt_bool_t speech_on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel;

	if (session != NULL)
		schannel = (speech_channel_t *)mrcp_application_session_object_get(session);
	else
		schannel = NULL;

	ast_log(LOG_DEBUG, "(%s) speech_on_session_terminate\n", schannel->name);

	if (schannel != NULL) {
		if (schannel->dtmf_generator != NULL) {
			ast_log(LOG_DEBUG, "(%s) DTMF generator destroyed\n", schannel->name);
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

	ast_log(LOG_DEBUG, "(%s) speech_on_channel_add\n", schannel->name);

	if ((schannel != NULL) && (application != NULL) && (session != NULL) && (channel != NULL)) {
		if ((session != NULL) && (status == MRCP_SIG_STATUS_CODE_SUCCESS)) {
			const mpf_codec_descriptor_t *descriptor = descriptor = mrcp_application_source_descriptor_get(channel);
			if (!descriptor) {
				ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				ast_log(LOG_DEBUG, "(%s) Terminating MRCP session\n", schannel->name);
				if (!mrcp_application_session_terminate(session))
					ast_log(LOG_WARNING, "(%s) Unable to terminate application session\n", schannel->name);
				return FALSE;
			}

			if (schannel->stream != NULL) {
				schannel->dtmf_generator = mpf_dtmf_generator_create(schannel->stream, schannel->pool);
				/* schannel->dtmf_generator = mpf_dtmf_generator_create_ex(schannel->stream, MPF_DTMF_GENERATOR_OUTBAND, 70, 50, schannel->pool); */

				if (schannel->dtmf_generator != NULL)
					ast_log(LOG_DEBUG, "(%s) DTMF generator created\n", schannel->name);
				else
					ast_log(LOG_WARNING, "(%s) Unable to create DTMF generator\n", schannel->name);
			}

			schannel->rate = descriptor->sampling_rate;
			const char *codec_name = NULL;
			if (descriptor->name.length > 0)
				codec_name = descriptor->name.buf;
			else
				codec_name = "unknown";

			ast_log(LOG_NOTICE, "(%s) Channel ready codec=%s, sample rate=%d\n",
				schannel->name,
				codec_name,
				schannel->rate);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
		} else {
			ast_log(LOG_ERROR, "(%s) Channel error!\n", schannel->name);

			if (session != NULL) {
				ast_log(LOG_DEBUG, "(%s) Terminating MRCP session\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);

				if (!mrcp_application_session_terminate(session))
					ast_log(LOG_WARNING, "(%s) Unable to terminate application session\n", schannel->name);
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

	ast_log(LOG_DEBUG, "(%s) speech_on_channel_remove\n", schannel->name);

	if (schannel != NULL) {
		ast_log(LOG_NOTICE, "(%s) Channel removed\n", schannel->name);
		schannel->unimrcp_channel = NULL;

		if (session != NULL) {
			ast_log(LOG_DEBUG, "(%s) Terminating MRCP session\n", schannel->name);

			if (!mrcp_application_session_terminate(session))
				ast_log(LOG_WARNING, "(%s) Unable to terminate application session\n", schannel->name);
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
		ast_log(LOG_DEBUG, "(%s) Sending START-INPUT-TIMERS request\n", schannel->name);

		/* Send START-INPUT-TIMERS to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_START_INPUT_TIMERS);

		if (mrcp_message) {
			mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message);
		} else {
			ast_log(LOG_ERROR, "(%s) Failed to create START-INPUT-TIMERS message\n", schannel->name);
			status = -1;
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
		ast_log(LOG_DEBUG, "(%s) Result is already set\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if ((result == NULL) || (strlen(result) == 0)) {
		ast_log(LOG_DEBUG, "(%s) Result is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	ast_log(LOG_DEBUG, "(%s) Set result:\n\n%s\n", schannel->name, result);
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
		/* Store the results for further reference from the dialplan. */
		recog_datastore_result_set(schannel->chan, r->result);

		if (uri_encoded == 0) {
			*result = apr_pstrdup(schannel->pool, r->result);
		}
		else {
			apr_size_t len = strlen(r->result) * 2;
			char *res = apr_palloc(schannel->pool, len);
			*result = ast_uri_encode_http(r->result, res, len);
		}
		ast_log(LOG_NOTICE, "(%s) Result:\n\n%s\n", schannel->name, *result);
		r->result = NULL;
		r->start_of_input = 0;
	} else if (r->start_of_input) {
		ast_log(LOG_DEBUG, "(%s) Start of input\n", schannel->name);
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
		const char *mime_type;
		if (((mime_type = grammar_type_to_mime(type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
			ast_log(LOG_WARNING, "(%s) Unable to get MIME type: %i\n", schannel->name, type);
			return -1;
		}
		ast_log(LOG_DEBUG, "(%s) Loading grammar name=%s, type=%s, data=%s\n", schannel->name, name, mime_type, data);

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
	if (app_message)
		return mrcp_application_message_dispatch(&mrcprecog->dispatcher, app_message);

	ast_log(LOG_ERROR, "(unknown) app_message error!\n");
	return TRUE;
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
					ast_log(LOG_DEBUG, "(%s) Unexpected RECOGNIZE request state: %d\n", schannel->name, message->start_line.request_state);
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
					ast_log(LOG_DEBUG, "(%s) Unexpected STOP request state: %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == RECOGNIZER_START_INPUT_TIMERS) {
				/* Received response to START-INPUT-TIMERS request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) Timers started\n", schannel->name);
						recog_channel_set_timers_started(schannel);
					} else
						ast_log(LOG_DEBUG, "(%s) Timers failed to start, status code = %d\n", schannel->name, message->start_line.status_code);
				}
			} else if (message->start_line.method_id == RECOGNIZER_DEFINE_GRAMMAR) {
				/* Received response to DEFINE-GRAMMAR request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) Grammar loaded\n", schannel->name);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
					} else {
						ast_log(LOG_DEBUG, "(%s) Grammar failed to load, status code = %d\n", schannel->name, message->start_line.status_code);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
					}
				}
			} else {
				/* Received unexpected response. */
				ast_log(LOG_DEBUG, "(%s) Unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
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
				ast_log(LOG_DEBUG, "(%s) Unexpected event, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else {
			ast_log(LOG_DEBUG, "(%s) Unexpected message type, message_type = %d\n", schannel->name, message->start_line.message_type);
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

	if (!str)
		return 0;
	if ((options->recog_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	while ((s = strsep(&str, "&"))) {
		value = s;
		if ((name = strsep(&value, "=")) && value) {
			ast_log(LOG_DEBUG, "Apply option %s: %s\n", name, value);
			mrcprecog_option_apply(options, name, value);
		}
	}
	return 0;
}

/* Playback the specified sound file. */
static int mrcprecog_streamfile(struct ast_channel *chan, const char *filename)
{
	struct ast_filestream* fs = ast_openstream(chan, filename, ast_channel_language(chan));
	if (!fs) {
		ast_log(LOG_WARNING, "ast_openstream failed on %s for %s\n", ast_channel_name(chan), filename);
		return -1;
	}

#if 0 /* get and log file length */
	if (ast_seekstream(fs, -1, SEEK_END) == 0) {
		off_t filelength = ast_tellstream(fs);
		ast_log(LOG_NOTICE, "Stream file on %s length:%"APR_OFF_T_FMT"\n", ast_channel_name(chan), filelength);
		
		if (ast_seekstream(fs, 0, SEEK_SET) != 0) {
			ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), filename);
		}
	}
	else
		ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), filename);
	}
#endif

	if (ast_applystream(chan, fs) != 0) {
		ast_log(LOG_WARNING, "ast_applystream failed on %s for %s\n", ast_channel_name(chan), filename);
		ast_closestream(fs);
		return -1;
	}

	if (ast_playstream(fs) != 0) {
		ast_log(LOG_WARNING, "ast_playstream failed on %s for %s\n", ast_channel_name(chan), filename);
		ast_closestream(fs);
		return -1;
	}

	return 0;
}

/* Exit the application. */
static int mrcprecog_exit(struct ast_channel *chan, mrcprecog_session_t *mrcprecog_session, speech_channel_status_t status)
{
	if (mrcprecog_session) {
		if (mrcprecog_session->readformat)
			ast_channel_set_readformat(chan, mrcprecog_session->readformat);

		if (mrcprecog_session->schannel)
			speech_channel_destroy(mrcprecog_session->schannel);

		if (mrcprecog_session->pool)
			apr_pool_destroy(mrcprecog_session->pool);
	}

	const char *status_str = speech_channel_status_to_string(status);
	pbx_builtin_setvar_helper(chan, "RECOGSTATUS", status_str);
	ast_log(LOG_NOTICE, "%s() exiting status: %s on %s\n", app_recog, status_str, ast_channel_name(chan));

	return status != SPEECH_CHANNEL_STATUS_ERROR ? 0 : -1;
}

/* The entry point of the application. */
static int app_recog_exec(struct ast_channel *chan, ast_app_data data)
{
	int samplerate = 8000;
	int dtmf_enable;
	struct ast_frame *f = NULL;
	apr_size_t len;
	ast_mrcp_profile_t *profile = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	const char *name;
	speech_channel_status_t status = SPEECH_CHANNEL_STATUS_OK;
	char *parse;
	int i;
	mrcprecog_options_t mrcprecog_options;
	mrcprecog_session_t mrcprecog_session;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(grammar);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s() requires an argument (grammar[,options])\n", app_recog);
		return mrcprecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.grammar)) {
		ast_log(LOG_WARNING, "%s() requires a grammar argument (grammar[,options])\n", app_recog);
		return mrcprecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	ast_log(LOG_NOTICE, "%s() grammar: %s\n", app_recog, args.grammar);

	if ((mrcprecog_session.pool = apt_pool_create()) == NULL) {
		ast_log(LOG_ERROR, "Unable to create memory pool for speech channel\n");
		return mrcprecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	mrcprecog_session.schannel = NULL;
	mrcprecog_session.readformat = NULL;

	mrcprecog_options.recog_hfs = NULL;
	mrcprecog_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		mrcprecog_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		ast_log(LOG_NOTICE, "%s() options: %s\n", app_recog, args.options);
		char *options_buf = apr_pstrdup(mrcprecog_session.pool, args.options);
		mrcprecog_options_parse(options_buf, &mrcprecog_options, mrcprecog_session.pool);
	}

	int bargein = 1;
	if ((mrcprecog_options.flags & MRCPRECOG_BARGEIN) == MRCPRECOG_BARGEIN) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_BARGEIN])) {
			bargein = (atoi(mrcprecog_options.params[OPT_ARG_BARGEIN]) == 0) ? 0 : 1;
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

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	ast_format_compat nreadformat;
	ast_format_clear(&nreadformat);
	get_recog_format(chan, &nreadformat);

	name = apr_psprintf(mrcprecog_session.pool, "ASR-%lu", (unsigned long int)speech_channel_number);

	/* Create speech channel for recognition. */
	mrcprecog_session.schannel = speech_channel_create(mrcprecog_session.pool, name, SPEECH_CHANNEL_RECOGNIZER, mrcprecog, format_to_str(&nreadformat), samplerate, chan);
	if (mrcprecog_session.schannel == NULL) {
		return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
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
		return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	/* Open recognition channel. */
	if (speech_channel_open(mrcprecog_session.schannel, profile) != 0) {
		return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	ast_format_compat oreadformat;
	ast_format_clear(&oreadformat);
	ast_channel_get_readformat(chan, &oreadformat);

	if (ast_channel_set_readformat(chan, &nreadformat) < 0) {
		ast_log(LOG_ERROR, "(%s) Unable to set read format to signed linear\n", name);
		return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	mrcprecog_session.readformat = &oreadformat;

	const char *grammar_delimiters = ",";
	/* Get grammar delimiters. */
	if ((mrcprecog_options.flags & MRCPRECOG_GRAMMAR_DELIMITERS) == MRCPRECOG_GRAMMAR_DELIMITERS) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_GRAMMAR_DELIMITERS])) {
			grammar_delimiters = mrcprecog_options.params[OPT_ARG_GRAMMAR_DELIMITERS];
			ast_log(LOG_DEBUG, "(%s) Grammar delimiters: %s\n", name, grammar_delimiters);
		}
	}
	/* Parse the grammar argument into a sequence of grammars. */
	char *grammar_arg = apr_pstrdup(mrcprecog_session.pool, args.grammar);
	char *last;
	char *grammar_str;
	char grammar_name[32];
	int grammar_id = 0;
	grammar_str = apr_strtok(grammar_arg, grammar_delimiters, &last);
	while (grammar_str) {
		const char *grammar_content = NULL;
		grammar_type_t grammar_type = GRAMMAR_TYPE_UNKNOWN;
		ast_log(LOG_DEBUG, "(%s) Determine grammar type: %s\n", name, grammar_str);
		if (determine_grammar_type(mrcprecog_session.schannel, grammar_str, &grammar_content, &grammar_type) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to determine grammar type: %s\n", name, grammar_str);
			return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		apr_snprintf(grammar_name, sizeof(grammar_name) - 1, "grammar-%d", grammar_id++);
		grammar_name[sizeof(grammar_name) - 1] = '\0';
		/* Load grammar. */
		if (recog_channel_load_grammar(mrcprecog_session.schannel, grammar_name, grammar_type, grammar_content) != 0) {
			ast_log(LOG_ERROR, "(%s) Unable to load grammar\n", name);
			return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		grammar_str = apr_strtok(NULL, grammar_delimiters, &last);
	}

	ast_stopstream(chan);

	const char *filename = NULL;
	if ((mrcprecog_options.flags & MRCPRECOG_FILENAME) == MRCPRECOG_FILENAME) {
		if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_FILENAME])) {
			filename = mrcprecog_options.params[OPT_ARG_FILENAME];
		}
	}

	if (filename) {
		/* Play file. */
		if (mrcprecog_streamfile(chan, filename) == 0) {
			if (bargein == 0) {
				/* Barge-in is not allowed, wait for stream to end. */
				if (ast_waitstream(chan, "") != 0) {
					ast_log(LOG_WARNING, "(%s) ast_waitstream failed on %s\n", name, ast_channel_name(chan));
					return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
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
				ast_log(LOG_ERROR, "(%s) Couldn't play file %s\n", name, filename);
				return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
			}
		}
	}

	ast_log(LOG_NOTICE, "(%s) Recognizing, enable DTMFs: %d\n", name, dtmf_enable);

	if (recog_channel_start(mrcprecog_session.schannel, name, mrcprecog_options.recog_hfs) != 0) {
		ast_log(LOG_ERROR, "(%s) Unable to start recognition\n", name);
		return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	int waitres;

	/* Continue with recognition. */
	while ((waitres = ast_waitfor(chan, 100)) >= 0) {
		int processing = 1;

		if ((mrcprecog_session.schannel != NULL) && (mrcprecog_session.schannel->mutex != NULL)) {
			if (mrcprecog_session.schannel->mutex != NULL) {
				apr_thread_mutex_lock(mrcprecog_session.schannel->mutex);
			}

			if (mrcprecog_session.schannel->state != SPEECH_CHANNEL_PROCESSING) {
				processing = 0;
			}

			if (mrcprecog_session.schannel->mutex != NULL) {
				apr_thread_mutex_unlock(mrcprecog_session.schannel->mutex);
			}
		}

		if (processing == 0)
			break;

		if (waitres == 0)
			continue;

		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_DEBUG, "(%s) Null frame. Hangup detected\n", name);
			status = SPEECH_CHANNEL_STATUS_INTERRUPTED;
			break;
		}

		if (f->frametype == AST_FRAME_VOICE) {
			len = f->datalen;
			if (speech_channel_write(mrcprecog_session.schannel, ast_frame_get_data(f), &len) != 0) {
				ast_frfree(f);
				break;
			}
		} else if (f->frametype == AST_FRAME_VIDEO) {
			/* Ignore. */
		} else if ((dtmf_enable != 0) && (f->frametype == AST_FRAME_DTMF)) {
			int dtmfkey = ast_frame_get_dtmfkey(f);
			ast_log(LOG_DEBUG, "(%s) User pressed DTMF key (%d)\n", name, dtmfkey);
			if (dtmf_enable == 2) {
				/* Send DTMF frame to ASR engine. */
				if (mrcprecog_session.schannel->dtmf_generator != NULL) {
					char digits[2];
					digits[0] = (char)dtmfkey;
					digits[1] = '\0';

					ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", mrcprecog_session.schannel->name, digits);
					mpf_dtmf_generator_enqueue(mrcprecog_session.schannel->dtmf_generator, digits);
				}
			} else if (dtmf_enable == 1) {
				/* Stop streaming if within i chars. */
				if (strchr(mrcprecog_options.params[OPT_ARG_INTERRUPT], dtmfkey) || (strcmp(mrcprecog_options.params[OPT_ARG_INTERRUPT],"any"))) {
					ast_frfree(f);
					break;
				}

				/* Continue if not an i-key. */
			}
		}

		ast_frfree(f);
	}

	if (status != SPEECH_CHANNEL_STATUS_INTERRUPTED && bargein != 0) {
		if (ast_waitstream(chan, "") != 0)
			status = SPEECH_CHANNEL_STATUS_ERROR;
	}

	const char *result = NULL;

	if (recog_channel_check_results(mrcprecog_session.schannel) == 0) {
		int uri_encoded_results = 0;
		/* Check if the results should be URI-encoded */
		if ((mrcprecog_options.flags & MRCPRECOG_URI_ENCODED_RESULTS) == MRCPRECOG_URI_ENCODED_RESULTS) {
			if (!ast_strlen_zero(mrcprecog_options.params[OPT_ARG_URI_ENCODED_RESULTS])) {
				uri_encoded_results = (atoi(mrcprecog_options.params[OPT_ARG_URI_ENCODED_RESULTS]) == 0) ? 0 : 1;
			}
		}

		if (recog_channel_get_results(mrcprecog_session.schannel, uri_encoded_results, &result) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", name);
			return mrcprecog_exit(chan, &mrcprecog_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

	pbx_builtin_setvar_helper(chan, "RECOG_RESULT", result ? result : "");

	speech_channel_stop(mrcprecog_session.schannel);
	ast_stopstream(chan);

	return mrcprecog_exit(chan, &mrcprecog_session, status);
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
		ast_log(LOG_ERROR, "Unable to create recognizer MRCP application %s\n", app_recog);
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
		ast_log(LOG_ERROR, "Unable to register recognizer MRCP application %s\n", app_recog);
		if (!mrcp_application_destroy(mrcprecog->app))
			ast_log(LOG_WARNING, "Unable to destroy recognizer MRCP application %s\n", app_recog);
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
