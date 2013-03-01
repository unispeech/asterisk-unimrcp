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

/* Maps MRCP header to unimrcp header handler function. */
static apr_hash_t *param_id_map;

/* UniMRCP parameter ID container. */
struct unimrcp_param_id {
	/* The parameter ID. */
	int id;
};
typedef struct unimrcp_param_id unimrcp_param_id_t;

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
			if (schannel->stream != NULL) {
				schannel->dtmf_generator = mpf_dtmf_generator_create(schannel->stream, schannel->pool);
				/* schannel->dtmf_generator = mpf_dtmf_generator_create_ex(schannel->stream, MPF_DTMF_GENERATOR_OUTBAND, 70, 50, schannel->pool); */

				if (schannel->dtmf_generator != NULL)
					ast_log(LOG_NOTICE, "(%s) DTMF generator created\n", schannel->name);
				else
					ast_log(LOG_NOTICE, "(%s) Unable to create DTMF generator\n", schannel->name);
			}

#if UNI_VERSION_AT_LEAST(0,8,0)
			char codec_name[60] = { 0 };
			const mpf_codec_descriptor_t *descriptor = NULL;

			/* What sample rate did we negotiate? */
			if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
				descriptor = mrcp_application_sink_descriptor_get(channel);
			else
				descriptor = mrcp_application_source_descriptor_get(channel);

			if (descriptor != NULL)
				schannel->rate = descriptor->sampling_rate;
			else {
				ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
				return FALSE;
			}

			if (descriptor->name.length > 0) {
				strncpy(codec_name, descriptor->name.buf, sizeof(codec_name) - 1);
				codec_name[sizeof(codec_name) - 1] = '\0';
			} else
				codec_name[0] = '\0';

			ast_log(LOG_DEBUG, "(%s) %s channel is ready, codec = %s, sample rate = %d\n", schannel->name, speech_channel_type_to_string(schannel->type), codec_name, schannel->rate);
#else
			ast_log(LOG_NOTICE, "(%s) %s channel is ready\n", schannel->name, speech_channel_type_to_string(schannel->type));
#endif
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

	ast_log(LOG_DEBUG, "speech_on_channel_add\n");

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
static int recog_channel_get_results(speech_channel_t *schannel, char **result)
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
		*result = strdup(r->result);
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

/* Set parameter in a recognizer MRCP header. */
static int recog_channel_set_header(speech_channel_t *schannel, int id, char *val, mrcp_message_t *msg, mrcp_recog_header_t *recog_hdr)
{
	if ((schannel == NULL) || (msg == NULL) || (recog_hdr == NULL))
		return -1;

	switch (id) {
		case RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD:
			recog_hdr->confidence_threshold = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD);
			break;

		case RECOGNIZER_HEADER_SENSITIVITY_LEVEL:
			recog_hdr->sensitivity_level = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SENSITIVITY_LEVEL);
			break;

		case RECOGNIZER_HEADER_SPEED_VS_ACCURACY:
			recog_hdr->speed_vs_accuracy = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEED_VS_ACCURACY);
			break;

		case RECOGNIZER_HEADER_N_BEST_LIST_LENGTH: {
			int n_best_list_length = atoi(val);
			if (n_best_list_length > 0) {
				recog_hdr->n_best_list_length = n_best_list_length;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_N_BEST_LIST_LENGTH);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid n best list length, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_NO_INPUT_TIMEOUT: {
			int no_input_timeout = atoi(val);
			if (no_input_timeout >= 0) {
				recog_hdr->no_input_timeout = no_input_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_NO_INPUT_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid no input timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_RECOGNITION_TIMEOUT: {
			int recognition_timeout = atoi(val);
			if (recognition_timeout >= 0) {
				recog_hdr->recognition_timeout = recognition_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_RECOGNITION_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid recognition timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_START_INPUT_TIMERS:
			recog_hdr->start_input_timers = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_START_INPUT_TIMERS);
			break;

		case RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT: {
			int speech_complete_timeout = atoi(val);
			if (speech_complete_timeout >= 0) {
				recog_hdr->speech_complete_timeout = speech_complete_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid speech complete timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT: {
			int speech_incomplete_timeout = atoi(val);
			if (speech_incomplete_timeout >= 0) {
				recog_hdr->speech_incomplete_timeout = speech_incomplete_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid speech incomplete timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT: {
			int dtmf_interdigit_timeout = atoi(val);
			if (dtmf_interdigit_timeout >= 0) {
				recog_hdr->dtmf_interdigit_timeout = dtmf_interdigit_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf interdigit timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT: {
			int dtmf_term_timeout = atoi(val);
			if (dtmf_term_timeout >= 0) {
				recog_hdr->dtmf_term_timeout = dtmf_term_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf term timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_TERM_CHAR:
			if (strlen(val) == 1) {
				recog_hdr->dtmf_term_char = *val;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_TERM_CHAR);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf term char, \"%s\"\n", schannel->name, val);
			break;

		case RECOGNIZER_HEADER_SAVE_WAVEFORM:
			recog_hdr->save_waveform = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SAVE_WAVEFORM);
			break;

		case RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL:
			recog_hdr->new_audio_channel = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL);
			break;

		case RECOGNIZER_HEADER_SPEECH_LANGUAGE:
			apt_string_assign(&recog_hdr->speech_language, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_LANGUAGE);
			break;

		case RECOGNIZER_HEADER_RECOGNITION_MODE:
			apt_string_assign(&recog_hdr->recognition_mode, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_RECOGNITION_MODE);
			break;

		case RECOGNIZER_HEADER_HOTWORD_MAX_DURATION: {
			int hotword_max_duration = atoi(val);
			if (hotword_max_duration >= 0) {
				recog_hdr->hotword_max_duration = hotword_max_duration;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_HOTWORD_MAX_DURATION);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid hotword max duration, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_HOTWORD_MIN_DURATION: {
			int hotword_min_duration = atoi(val);
			if (hotword_min_duration >= 0) {
				recog_hdr->hotword_min_duration = hotword_min_duration;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_HOTWORD_MIN_DURATION);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid hotword min duration, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER:
			recog_hdr->clear_dtmf_buffer = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER);
			break;

		case RECOGNIZER_HEADER_EARLY_NO_MATCH:
			recog_hdr->early_no_match = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_EARLY_NO_MATCH);
			break;

		case RECOGNIZER_HEADER_INPUT_WAVEFORM_URI:
			apt_string_assign(&recog_hdr->input_waveform_uri, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_INPUT_WAVEFORM_URI);
			break;

		case RECOGNIZER_HEADER_MEDIA_TYPE:
			apt_string_assign(&recog_hdr->media_type, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_MEDIA_TYPE);
			break;

		/* Unsupported headers. */

		/* MRCP server headers. */
		case RECOGNIZER_HEADER_WAVEFORM_URI:
		case RECOGNIZER_HEADER_COMPLETION_CAUSE:
		case RECOGNIZER_HEADER_FAILED_URI:
		case RECOGNIZER_HEADER_FAILED_URI_CAUSE:
		case RECOGNIZER_HEADER_INPUT_TYPE:
		case RECOGNIZER_HEADER_COMPLETION_REASON:

		/* Module handles this automatically. */
		case RECOGNIZER_HEADER_CANCEL_IF_QUEUE:

		/* GET-PARAMS method only. */
		case RECOGNIZER_HEADER_RECOGNIZER_CONTEXT_BLOCK:
		case RECOGNIZER_HEADER_DTMF_BUFFER_TIME:

		/* INTERPRET method only. */
		case RECOGNIZER_HEADER_INTERPRET_TEXT:

		/* Unknown. */
		case RECOGNIZER_HEADER_VER_BUFFER_UTTERANCE:
		default:
			ast_log(LOG_WARNING, "(%s) unsupported RECOGNIZER header( in this module )\n", schannel->name);
			break;
	}

	return 0;
}

/* Set parameters in a recognizer MRCP header. */
static int recog_channel_set_params(speech_channel_t *schannel, mrcp_message_t *msg, mrcp_generic_header_t *gen_hdr, mrcp_recog_header_t *recog_hdr)
{
	if ((schannel != NULL) && (msg != NULL) && (gen_hdr != NULL) && (recog_hdr != NULL)) {
		/* Loop through each param and add to recog header or vendor-specific-params. */
		apr_hash_index_t *hi = NULL;

		for (hi = apr_hash_first(NULL, schannel->params); hi; hi = apr_hash_next(hi)) {
			char *param_name = NULL;
			char *param_val = NULL;
			const void *key;
			void *val;

			apr_hash_this(hi, &key, NULL, &val);

			param_name = (char *)key;
			param_val = (char *)val;

			if (param_name && (strlen(param_name) > 0) && param_val && (strlen(param_val) > 0)) {
				unimrcp_param_id_t *id = NULL;

				if (param_id_map != NULL)
					id = (unimrcp_param_id_t *)apr_hash_get(param_id_map, param_name, APR_HASH_KEY_STRING);

				if (id) {
					ast_log(LOG_DEBUG, "(%s) %s: %s\n", schannel->name, param_name, param_val);
					recog_channel_set_header(schannel, id->id, param_val, msg, recog_hdr);
				} else {
					apt_str_t apt_param_name = { 0 };
					apt_str_t apt_param_val = { 0 };

					/* This is probably a vendor-specific MRCP param. */
					ast_log(LOG_DEBUG, "(%s) (vendor-specific value) %s: %s\n", schannel->name, param_name, param_val);
					apt_string_set(&apt_param_name, param_name); /* Copy isn't necessary since apt_pair_array_append will do it. */
					apt_string_set(&apt_param_val, param_val);

					if (!gen_hdr->vendor_specific_params) {
						ast_log(LOG_DEBUG, "(%s) creating vendor specific pair array\n", schannel->name);
						gen_hdr->vendor_specific_params = apt_pair_array_create(10, msg->pool);
					}

					apt_pair_array_append(gen_hdr->vendor_specific_params, &apt_param_name, &apt_param_val, msg->pool);
				}
			}
		}
	
		if (gen_hdr->vendor_specific_params) {
			mrcp_generic_header_property_add(msg, GENERIC_HEADER_VENDOR_SPECIFIC_PARAMS);
		}
	} else
		ast_log(LOG_ERROR, "(unknown) [recog_channel_set_params] channel error!\n");

	return 0;
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
static int recog_channel_start(speech_channel_t *schannel, const char *name)
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
		start_input_timers = (char *)apr_hash_get(schannel->params, "start-input-timers", APR_HASH_KEY_STRING);
		r->timers_started = (start_input_timers == NULL) || (strlen(start_input_timers) == 0) || (strcasecmp(start_input_timers, "false"));

		/* Get the cached grammar. */
		if ((name == NULL) || (strlen(name) == 0))
			grammar = r->last_grammar;
		else {
			grammar = (grammar_t *)apr_hash_get(r->grammars, name, APR_HASH_KEY_STRING);
			r->last_grammar = grammar;
		}

		if (grammar == NULL) {
			if (name)
				ast_log(LOG_ERROR, "(%s) Undefined grammar, %s\n", schannel->name, name);
			else
				ast_log(LOG_ERROR, "(%s) No grammar specified\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

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
		if (((mime_type = grammar_type_to_mime(grammar->type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		apt_string_assign(&generic_header->content_type, mime_type, mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

		/* Set Content-ID for inline grammars. */
		if (grammar->type != GRAMMAR_TYPE_URI) {
			apt_string_assign(&generic_header->content_id, grammar->name, mrcp_message->pool);
			mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_ID);
		}

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
		recog_channel_set_params(schannel, mrcp_message, generic_header, recog_header);

		/* Set message body. */
		apt_string_assign(&mrcp_message->body, grammar->data, mrcp_message->pool);

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

/* Unload speech recognition grammar. */
static int recog_channel_unload_grammar(speech_channel_t *schannel, const char *grammar_name)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if ((grammar_name == NULL) || (strlen(grammar_name) == 0))
		status = -1;
	else {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		recognizer_data_t *r = (recognizer_data_t *)schannel->data;

		if (r == NULL) {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		ast_log(LOG_DEBUG, "(%s) Unloading grammar %s\n", schannel->name, grammar_name);
		apr_hash_set(r->grammars, grammar_name, APR_HASH_KEY_STRING, NULL);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	}

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
				ast_log(LOG_NOTICE, "(%s) DTMF frame written\n", schannel->name);
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

static int app_recog_exec(struct ast_channel *chan, ast_app_data data)
{
	int samplerate = 8000;
	int dtmf_enable = 0;
	struct ast_frame *f = NULL;
	apr_size_t len;
	int rres = 0;
	speech_channel_t *schannel = NULL;
	const char *profile_name = NULL;
	ast_mrcp_profile_t *profile = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	char name[200] = { 0 };
	int waitres = 0;
	int res = 0;
	char *parse;

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
	char option_confidencethresh[64] = { 0 };
	char option_senselevel[64] = { 0 };
	char option_speechlanguage[64] = { 0 };
	char option_profile[256] = { 0 };
	char option_interrupt[64] = { 0 };
	char option_filename[384] = { 0 };
	char option_timeout[64] = { 0 };
	char option_bargein[32] = { 0 };
	char option_inputwaveuri[384] = { 0 };
	char option_earlynomatch[16] = { 0 };
	char option_cleardtmfbuf[16] = { 0 };
	char option_hotwordmin[64] = { 0 };
	char option_hotwordmax[64] = { 0 };
	char option_recogmode[128] = { 0 };
	char option_newaudioc[16] = { 0 };
	char option_savewave[16] = { 0 };
	char option_dtmftermc[16] = { 0 };
	char option_dtmftermt[64] = { 0 };
	char option_dtmfdigitt[64] = { 0 };
	char option_speechit[64] = { 0 };
	char option_speechct[64] = { 0 };
	char option_startinputt[16] = { 0 };
	char option_noinputt[64] = { 0 };
	char option_nbest[64] = { 0 };
	char option_speedvsa[64] = { 0 };
	char option_mediatype[64] = { 0 };
	int speech = 0;
	struct timeval start = { 0, 0 };
	struct timeval detection_start = { 0, 0 };
	int min = 100;
	struct ast_dsp *dsp = NULL;

	if (!ast_strlen_zero(args.options)) {
		char tempstr[1024];
		char* token;

		strncpy(tempstr, args.options, sizeof(tempstr) - 1);
		tempstr[sizeof(tempstr) - 1] = '\0';

		trimstr(tempstr);

		do {
			token = strstr(tempstr, "&");

			if (token != NULL)
				tempstr[token - tempstr] = '\0';

			char* pos;

			char tempstr2[1024];
			strncpy(tempstr2, tempstr, sizeof(tempstr2) - 1);
			tempstr2[sizeof(tempstr2) - 1] = '\0';			
			trimstr(tempstr2);

			ast_log(LOG_NOTICE, "Option=|%s|\n", tempstr2);

			if ((pos = strstr(tempstr2, "=")) != NULL) {
				*pos = '\0';

				char* key = tempstr2;
				char* value = pos + 1;

				char tempstr3[1024];
				char tempstr4[1024];

				strncpy(tempstr3, key, sizeof(tempstr3) - 1);
				tempstr3[sizeof(tempstr3) - 1] = '\0';			
				trimstr(tempstr3);

				strncpy(tempstr4, value, sizeof(tempstr4) - 1);
				tempstr4[sizeof(tempstr4) - 1] = '\0';
				trimstr(tempstr4);

				key = tempstr3;
				value = tempstr4;
				if (strcasecmp(key, "ct") == 0) {
					strncpy(option_confidencethresh, value, sizeof(option_confidencethresh) - 1);
					option_confidencethresh[sizeof(option_confidencethresh) - 1] = '\0';
				} else if (strcasecmp(key, "sva") == 0) {
					strncpy(option_speedvsa, value, sizeof(option_speedvsa) - 1);
					option_speedvsa[sizeof(option_speedvsa) - 1] = '\0';
				} else if (strcasecmp(key, "nb") == 0) {
					strncpy(option_nbest, value, sizeof(option_nbest) - 1);
					option_nbest[sizeof(option_nbest) - 1] = '\0';
				} else if (strcasecmp(key, "nit") == 0) {
					strncpy(option_noinputt, value, sizeof(option_noinputt) - 1);
					option_noinputt[sizeof(option_noinputt) - 1] = '\0';
				} else if (strcasecmp(key, "sit") == 0) {
					strncpy(option_startinputt, value, sizeof(option_startinputt) - 1);
					option_startinputt[sizeof(option_startinputt) - 1] = '\0';
				} else if (strcasecmp(key, "sct") == 0) {
					strncpy(option_speechct, value, sizeof(option_speechct) - 1);
					option_speechct[sizeof(option_speechct) - 1] = '\0';
				} else if (strcasecmp(key, "sint") == 0) {
					strncpy(option_speechit, value, sizeof(option_speechit) - 1);
					option_speechit[sizeof(option_speechit) - 1] = '\0';
				} else if (strcasecmp(key, "dit") == 0) {
					strncpy(option_dtmfdigitt, value, sizeof(option_dtmfdigitt) - 1);
					option_dtmfdigitt[sizeof(option_dtmfdigitt) - 1] = '\0';
				} else if (strcasecmp(key, "dtt") == 0) {
					strncpy(option_dtmftermt, value, sizeof(option_dtmftermt) - 1);
					option_dtmftermt[sizeof(option_dtmftermt) - 1] = '\0';
				} else if (strcasecmp(key, "dttc") == 0) {
					strncpy(option_dtmftermc, value, sizeof(option_dtmftermc) - 1);
					option_dtmftermc[sizeof(option_dtmftermc) - 1] = '\0';
				} else if (strcasecmp(key, "sw") == 0) {
					strncpy(option_savewave, value, sizeof(option_savewave) - 1);
					option_savewave[sizeof(option_savewave) - 1] = '\0';
				} else if (strcasecmp(key, "nac") == 0) {
					strncpy(option_newaudioc, value, sizeof(option_newaudioc) - 1);
					option_newaudioc[sizeof(option_newaudioc) - 1] = '\0';
				} else if (strcasecmp(key, "rm") == 0) {
					strncpy(option_recogmode, value, sizeof(option_recogmode) - 1);
					option_recogmode[sizeof(option_recogmode) - 1] = '\0';
				} else if (strcasecmp(key, "hmaxd") == 0) {
					strncpy(option_hotwordmax, value, sizeof(option_hotwordmax) - 1);
					option_hotwordmax[sizeof(option_hotwordmax) - 1] = '\0';
				} else if (strcasecmp(key, "hmind") == 0) {
					strncpy(option_hotwordmin, value, sizeof(option_hotwordmin) - 1);
					option_hotwordmin[sizeof(option_hotwordmin) - 1] = '\0';
				} else if (strcasecmp(key, "cdb") == 0) {
					strncpy(option_cleardtmfbuf, value, sizeof(option_cleardtmfbuf) - 1);
					option_cleardtmfbuf[sizeof(option_cleardtmfbuf) - 1] = '\0';
				} else if (strcasecmp(key, "enm") == 0) {
					strncpy(option_earlynomatch, value, sizeof(option_earlynomatch) - 1);
					option_earlynomatch[sizeof(option_earlynomatch) - 1] = '\0';
				} else if (strcasecmp(key, "iwu") == 0) {
					strncpy(option_inputwaveuri, value, sizeof(option_inputwaveuri) - 1);
					option_inputwaveuri[sizeof(option_inputwaveuri) - 1] = '\0';
				} else if (strcasecmp(key, "sl") == 0) {
					strncpy(option_senselevel, value, sizeof(option_senselevel) - 1);
					option_senselevel[sizeof(option_senselevel) - 1] = '\0';
				} else if (strcasecmp(key, "spl") == 0) {
					strncpy(option_speechlanguage, value, sizeof(option_speechlanguage) - 1);
					option_speechlanguage[sizeof(option_speechlanguage) - 1] = '\0';
				} else if (strcasecmp(key, "mt") == 0) {
					strncpy(option_mediatype, value, sizeof(option_mediatype) - 1);
					option_mediatype[sizeof(option_mediatype) - 1] = '\0';
				} else if (strcasecmp(key, "p") == 0) {
					strncpy(option_profile, value, sizeof(option_profile) - 1);
					option_profile[sizeof(option_profile) - 1] = '\0';
				} else if (strcasecmp(key, "i") == 0) {
					strncpy(option_interrupt, value, sizeof(option_interrupt) - 1);
					option_interrupt[sizeof(option_interrupt) - 1] = '\0';
				} else if (strcasecmp(key, "f") == 0) {
					strncpy(option_filename, value, sizeof(option_filename) - 1);
					option_filename[sizeof(option_filename) - 1] = '\0';
				} else if (strcasecmp(key, "t") == 0) {
					strncpy(option_timeout, value, sizeof(option_timeout) - 1);
					option_timeout[sizeof(option_timeout) - 1] = '\0';
				} else if (strcasecmp(key, "b") == 0) {
					strncpy(option_bargein, value, sizeof(option_bargein) - 1);
					option_bargein[sizeof(option_bargein) - 1] = '\0';
				}
			}
			if (token != NULL) {
				strncpy(tempstr, token + 1, sizeof(tempstr) - 1);
				tempstr[sizeof(tempstr) - 1] = '\0';
			}
		} while (token != NULL);
	}

	int bargein = 1;
	if (!ast_strlen_zero(option_bargein)) {
		bargein = atoi(option_bargein);
		if ((bargein < 0) || (bargein > 2))
			bargein = 1;
	}

	if (!ast_strlen_zero(option_profile)) {
		ast_log(LOG_NOTICE, "Profile to use: %s\n", option_profile);
	}
	if (!ast_strlen_zero(args.grammar)) {
		ast_log(LOG_NOTICE, "Grammar to recognize with: %s\n", args.grammar);
	}
	if (!ast_strlen_zero(option_filename)) {
		ast_log(LOG_NOTICE, "Filename to play: %s\n", option_filename);
	}
	if (!ast_strlen_zero(option_timeout)) {
		ast_log(LOG_NOTICE, "Recognition timeout: %s\n", option_timeout);
	}
	if (!ast_strlen_zero(option_bargein)) {
		ast_log(LOG_NOTICE, "Barge-in: %s\n", option_bargein);
	}
	if (!ast_strlen_zero(option_confidencethresh)) {
		ast_log(LOG_NOTICE, "Confidence threshold: %s\n", option_confidencethresh);
	}
	if (!ast_strlen_zero(option_senselevel)) {
		ast_log(LOG_NOTICE, "Sensitivity-level: %s\n", option_senselevel);
	}
	if (!ast_strlen_zero(option_speechlanguage)) {
		ast_log(LOG_NOTICE, "Speech-language: %s\n", option_speechlanguage);
	}
	if (!ast_strlen_zero(option_inputwaveuri)) {
		ast_log(LOG_NOTICE, "Input wave URI: %s\n", option_inputwaveuri);
	}
	if (!ast_strlen_zero(option_earlynomatch)) {
		ast_log(LOG_NOTICE, "Early-no-match: %s\n", option_earlynomatch);
	}
	if (!ast_strlen_zero(option_cleardtmfbuf)) {
		ast_log(LOG_NOTICE, "Clear DTMF buffer: %s\n", option_cleardtmfbuf);
	}
	if (!ast_strlen_zero(option_hotwordmin)) {
		ast_log(LOG_NOTICE, "Hotword min delay: %s\n", option_hotwordmin);
	}
	if (!ast_strlen_zero(option_hotwordmax)) {
		ast_log(LOG_NOTICE, "Hotword max delay: %s\n", option_hotwordmax);
	}
	if (!ast_strlen_zero(option_recogmode)) {
		ast_log(LOG_NOTICE, "Recognition Mode: %s\n", option_recogmode);
	}
	if (!ast_strlen_zero(option_newaudioc)) {
		ast_log(LOG_NOTICE, "New-audio-channel: %s\n", option_newaudioc);
	}
	if (!ast_strlen_zero(option_savewave)) {
		ast_log(LOG_NOTICE, "Save waveform: %s\n", option_savewave);
	}
	if (!ast_strlen_zero(option_dtmftermc)) {
		ast_log(LOG_NOTICE, "DTMF term char: %s\n", option_dtmftermc);
	}
	if (!ast_strlen_zero(option_dtmftermt)) {
		ast_log(LOG_NOTICE, "DTMF terminate timeout: %s\n", option_dtmftermt);
	}
	if (!ast_strlen_zero(option_dtmfdigitt)) {
		ast_log(LOG_NOTICE, "DTMF digit terminate timeout: %s\n", option_dtmfdigitt);
	}
	if (!ast_strlen_zero(option_speechit)) {
		ast_log(LOG_NOTICE, "Speech incomplete timeout : %s\n", option_speechit);
	}
	if (!ast_strlen_zero(option_speechct)) {
		ast_log(LOG_NOTICE, "Speech complete timeout: %s\n", option_speechct);
	}
	if (!ast_strlen_zero(option_startinputt)) {
		ast_log(LOG_NOTICE, "Start-input timeout: %s\n", option_startinputt);
	}
	if (!ast_strlen_zero(option_noinputt)) {
		ast_log(LOG_NOTICE, "No-input timeout: %s\n", option_noinputt);
	}
	if (!ast_strlen_zero(option_nbest)) {
		ast_log(LOG_NOTICE, "N-best list length: %s\n", option_nbest);
	}
	if (!ast_strlen_zero(option_speedvsa)) {
		ast_log(LOG_NOTICE, "Speed vs accuracy: %s\n", option_speedvsa);
	}
	if (!ast_strlen_zero(option_mediatype)) {
		ast_log(LOG_NOTICE, "Media Type: %s\n", option_mediatype);
	}

	if (strlen(option_interrupt) > 0) {
		dtmf_enable = 1;

		if (strcasecmp(option_interrupt, "any") == 0) {
			strncpy(option_interrupt, AST_DIGIT_ANY, sizeof(option_interrupt) - 1);
			option_interrupt[sizeof(option_interrupt) - 1] = '\0';
		} else if (strcasecmp(option_interrupt, "none") == 0)
			dtmf_enable = 2;
	}
	ast_log(LOG_NOTICE, "DTMF enable: %d\n", dtmf_enable);

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	apr_snprintf(name, sizeof(name) - 1, "ASR-%lu", (unsigned long int)speech_channel_number);
	name[sizeof(name) - 1] = '\0';

	ast_format_compat nreadformat;
	ast_format_clear(&nreadformat);
	get_recog_format(chan, &nreadformat);

	/* if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_RECOGNIZER, &globals.recog, "L16", samplerate, chan) != 0) { */
	if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_RECOGNIZER, mrcprecog, format_to_str(&nreadformat), samplerate, chan) != 0) {
		res = -1;
		goto done;
	}

	profile = get_recog_profile(option_profile);
	if (!profile) {
		ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", name, profile_name);
		res = -1;
		speech_channel_destroy(schannel);
		goto done;
	}

	if (speech_channel_open(schannel, profile) != 0) {
		res = -1;
		speech_channel_destroy(schannel);
		goto done;
	}

	if (!ast_strlen_zero(option_timeout))
		speech_channel_set_param(schannel, "recognition-timeout", option_timeout);
	if (!ast_strlen_zero(option_confidencethresh))
		speech_channel_set_param(schannel, "confidence-threshold", option_confidencethresh);
	if (!ast_strlen_zero(option_inputwaveuri))
		speech_channel_set_param(schannel, "input-waveform-uri", option_inputwaveuri);
	if (!ast_strlen_zero(option_earlynomatch))
		speech_channel_set_param(schannel, "earlynomatch",option_earlynomatch);
	if (!ast_strlen_zero(option_cleardtmfbuf))
		speech_channel_set_param(schannel, "clear-dtmf-buffer", option_cleardtmfbuf);
	if (!ast_strlen_zero(option_hotwordmin))
		speech_channel_set_param(schannel, "hotword-min-duration", option_hotwordmin);
	if (!ast_strlen_zero(option_hotwordmax))
		speech_channel_set_param(schannel, "hotword-max-duration", option_hotwordmax);
	if (!ast_strlen_zero(option_recogmode))
		speech_channel_set_param(schannel, "recognition-mode", option_recogmode);
	if (!ast_strlen_zero(option_newaudioc))
		speech_channel_set_param(schannel, "new-audio-channel", option_newaudioc);
	if (!ast_strlen_zero(option_savewave))
		speech_channel_set_param(schannel, "save-waveform", option_savewave);
	if (!ast_strlen_zero(option_dtmftermc))
		speech_channel_set_param(schannel, "dtmf-term-char", option_dtmftermc);
	if (!ast_strlen_zero(option_dtmftermt))
		speech_channel_set_param(schannel, "dtmf-term-timeout", option_dtmftermt);
	if (!ast_strlen_zero(option_dtmfdigitt))
		speech_channel_set_param(schannel, "dtmf-interdigit-timeout", option_dtmfdigitt);
	if (!ast_strlen_zero(option_speechit))
		speech_channel_set_param(schannel, "speech-incomplete-timeout", option_speechit);
	if (!ast_strlen_zero(option_speechct))
		speech_channel_set_param(schannel, "speech-complete-timeout", option_speechct);
	if (!ast_strlen_zero(option_startinputt))
		speech_channel_set_param(schannel, "start-input-timers", option_startinputt);
	if (!ast_strlen_zero(option_noinputt))
		speech_channel_set_param(schannel, "no-input-timeout", option_noinputt);
	if (!ast_strlen_zero(option_nbest))
		speech_channel_set_param(schannel, "n-best-list-length", option_nbest);
	if (!ast_strlen_zero(option_speedvsa))
		speech_channel_set_param(schannel, "speed-vs-accuracy", option_speedvsa);
	if (!ast_strlen_zero(option_mediatype))
		speech_channel_set_param(schannel, "media-type", option_mediatype);
	if (!ast_strlen_zero(option_senselevel))
		speech_channel_set_param(schannel, "sensitivity-level", option_senselevel);
	if (!ast_strlen_zero(option_speechlanguage))
		speech_channel_set_param(schannel, "speech-language", option_speechlanguage);

	ast_format_compat oreadformat;
	ast_format_clear(&oreadformat);
	ast_channel_get_readformat(chan, &oreadformat);

	if (ast_channel_set_readformat(chan, &nreadformat) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format to signed linear\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}
	
	grammar_type_t tmp_grammar = GRAMMAR_TYPE_UNKNOWN;
	const char *grammar_data = args.grammar;
	grammar_data = get_grammar_type(schannel,grammar_data,&tmp_grammar);
	ast_log(LOG_DEBUG, "Grammar type is: %i\n", tmp_grammar);

	if (recog_channel_load_grammar(schannel, name, tmp_grammar, grammar_data) != 0) {
		ast_log(LOG_ERROR, "Unable to load grammar\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}

	struct ast_filestream* fs = NULL;
	off_t filelength = 0;

	ast_stopstream(chan);

	/* Open file, get file length, seek to begin, apply and play. */ 
	if (!ast_strlen_zero(option_filename)) {
		if ((fs = ast_openstream(chan, option_filename, ast_channel_language(chan))) == NULL) {
			ast_log(LOG_WARNING, "ast_openstream failed on %s for %s\n", ast_channel_name(chan), option_filename);
		} else {
			if (ast_seekstream(fs, -1, SEEK_END) == -1) {
				ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), option_filename);
			} else {
				filelength = ast_tellstream(fs);
				ast_log(LOG_NOTICE, "file length:%"APR_OFF_T_FMT"\n", filelength);
			}

			if (ast_seekstream(fs, 0, SEEK_SET) == -1) {
				ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", ast_channel_name(chan), option_filename);
			} else if (ast_applystream(chan, fs) == -1) {
				ast_log(LOG_WARNING, "ast_applystream failed on %s for %s\n", ast_channel_name(chan), option_filename);
			} else if (ast_playstream(fs) == -1) {
				ast_log(LOG_WARNING, "ast_playstream failed on %s for %s\n", ast_channel_name(chan), option_filename);
			}
		}

		if (bargein == 0) {
			res = ast_waitstream(chan, "");
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
					ast_log(LOG_NOTICE, "prompt has finished playing, moving on.\n");
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

	int i = 0;
	if (((dtmf_enable == 1) && (dtmfkey != -1)) || (waitres < 0)) {
		/* Skip as we have to return to specific dialplan extension, or a error occurred on the channel */
	} else {
		ast_log(LOG_NOTICE, "Recognizing\n");

		if (recog_channel_start(schannel, name) == 0) {
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
						if (strchr(option_interrupt, dtmfkey) || (strcmp(option_interrupt,"any")))
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

		char* result = NULL;

		if (recog_channel_check_results(schannel) == 0) {
			if (recog_channel_get_results(schannel, &result) == 0) {
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

	if (recog_channel_unload_grammar(schannel, name) != 0) {
		ast_log(LOG_ERROR, "Unable to unload grammar\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}

	ast_channel_set_readformat(chan, &oreadformat);

	speech_channel_stop(schannel);
	speech_channel_destroy(schannel);
	ast_stopstream(chan);
	
done:
	if (res < 0)
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "ERROR");
	else
		pbx_builtin_setvar_helper(chan, "RECOGSTATUS", "OK");

	if ((res < 0) && (!ast_check_hangup(chan)))
		res = 0;

	return res;
}

/* Create a parameter ID. */
static unimrcp_param_id_t *unimrcp_param_id_create(int id, apr_pool_t *pool)
{   
	unimrcp_param_id_t *param = (unimrcp_param_id_t *)apr_palloc(pool, sizeof(unimrcp_param_id_t));

	if (param != NULL)
		param->id = id;

	return param;
}

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
		mrcprecog->app = NULL;
		return -1;
	}

	/* Create a hash for the recognizer parameter map. */
	param_id_map = apr_hash_make(pool);

	if (param_id_map != NULL) {
		apr_hash_set(param_id_map, apr_pstrdup(pool, "confidence-threshold"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "sensitivity-level"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SENSITIVITY_LEVEL, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speed-vs-accuracy"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEED_VS_ACCURACY, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "n-best-list-length"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_N_BEST_LIST_LENGTH, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "no-input-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_NO_INPUT_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "recognition-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNITION_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "waveform-url"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_WAVEFORM_URI, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "completion-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_COMPLETION_CAUSE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "recognizer-context-block"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNIZER_CONTEXT_BLOCK, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "start-input-timers"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_START_INPUT_TIMERS, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speech-complete-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speech-incomplete-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "dtmf-interdigit-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "dtmf-term-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "dtmf-term-char"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_TERM_CHAR, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "failed-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_FAILED_URI, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "failed-uri-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_FAILED_URI_CAUSE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "save-waveform"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SAVE_WAVEFORM, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "new-audio-channel"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speech-language"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_LANGUAGE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "input-type"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INPUT_TYPE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "input-waveform-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INPUT_WAVEFORM_URI, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "completion-reason"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_COMPLETION_REASON, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "media-type"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_MEDIA_TYPE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "ver-buffer-utterance"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_VER_BUFFER_UTTERANCE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "recognition-mode"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNITION_MODE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "cancel-if-queue"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CANCEL_IF_QUEUE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "hotword-max-duration"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_HOTWORD_MAX_DURATION, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "hotword-min-duration"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_HOTWORD_MIN_DURATION, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "interpret-text"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INTERPRET_TEXT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "dtmf-buffer-time"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_BUFFER_TIME, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "clear-dtmf-buffer"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "early-no-match"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_EARLY_NO_MATCH, pool));
	}

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, mrcprecog);

	return 0;
}

int unload_mrcprecog_app()
{
	if(!mrcprecog) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", app_recog);
		return -1;
	}

	/* Clear parameter ID map. */
	if (param_id_map != NULL)
		apr_hash_clear(param_id_map);

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, NULL);
	mrcprecog = NULL;

	return 0;
}
