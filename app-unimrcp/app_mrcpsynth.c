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
 * \brief MRCPSynth application
 *
 * \author\verbatim J.W.F. Thirion <derik@molo.co.za> \endverbatim
 * 
 * MRCPSynth application
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

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"

#include "audio_queue.h"
#include "speech_channel.h"

/*** DOCUMENTATION
	<application name="MRCPSynth" language="en_US">
		<synopsis>
			MRCP synthesis application.
		</synopsis>
		<syntax>
			<parameter name="text" required="true"/>
			<parameter name="options" />
		</syntax>
		<description>
		<para>MRCP synthesis application.
		Supports version 1 and 2 of MRCP, using UniMRCP. The options can be one or
		more of the following: i=interrupt keys or <literal>any</literal> for any DTMF
		key, p=profile to use, f=filename to save audio to, l=language to use in the
		synthesis header (en-US/en-GB/etc.), v=voice name, g=gender (male/female),
		a=voice age (1-19 digits), pv=prosody volume
		(silent/x-soft/soft/medium/load/x-loud/default), pr=prosody rate
		(x-slow/slow/medium/fast/x-fast/default), ll=load lexicon (true/false),
		vv=voice variant (1-19 digits). If the audio file name is empty or the
		parameter not given, no audio will be stored to disk. If the interrupt keys
		are set, then kill-on-bargein will be enabled, otherwise if it is empty or not
		given, then kill-on-bargein will be disabled.</para>
		</description>
	</application>
 ***/

/* The name of the application. */
static const char *app_synth = "MRCPSynth";

#if !AST_VERSION_AT_LEAST(1,6,2)
static char *synthsynopsis = "MRCP synthesis application.";
static char *synthdescrip =
"Supports version 1 and 2 of MRCP, using UniMRCP. The options can be one or\n"
"more of the following: i=interrupt keys or <literal>any</literal> for any DTMF\n"
"key, p=profile to use, f=filename to save audio to, l=language to use in the\n"
"synthesis header (en-US/en-GB/etc.), v=voice name, g=gender (male/female),\n"
"a=voice age (1-19 digits), pv=prosody volume\n"
"(silent/x-soft/soft/medium/load/x-loud/default), pr=prosody rate\n"
"(x-slow/slow/medium/fast/x-fast/default), ll=load lexicon (true/false),\n"
"vv=voice variant (1-19 digits). If the audio file name is empty or the\n"
"parameter not given, no audio will be stored to disk. If the interrupt keys\n"
"are set, then kill-on-bargein will be enabled, otherwise if it is empty or not\n"
"given, then kill-on-bargein will be disabled.\n";
#endif

static ast_mrcp_application_t *mrcpsynth = NULL;

/* Maps MRCP header to unimrcp header handler function. */
static apr_hash_t *param_id_map;

/* UniMRCP parameter ID container. */
struct unimrcp_param_id {
	/* The parameter ID. */
	int id;
};
typedef struct unimrcp_param_id unimrcp_param_id_t;

/* Default frame size:
 *
 * 8000 samples/sec * 20ms = 160 * 2 bytes/sample = 320 bytes
 * 16000 samples/sec * 20ms = 320 * 2 bytes/sample = 640 bytes
 */
#define DEFAULT_FRAMESIZE						320


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

/* --- MRCP TTS --- */

/* Process UniMRCP messages for the synthesizer application.  All MRCP synthesizer callbacks start here first. */
static apt_bool_t synth_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message != NULL)
		return mrcp_application_message_dispatch(&mrcpsynth->dispatcher, app_message);
	else {
		ast_log(LOG_ERROR, "(unknown) app_message error!\n");
		return TRUE;
	}
}

/* Handle the MRCP synthesizer responses/events from UniMRCP. */
static apt_bool_t synth_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel;

	if (channel != NULL)
		schannel = (speech_channel_t *)mrcp_application_channel_object_get(channel);
	else
		schannel = NULL;

	if ((schannel != NULL) && (application != NULL) && (session != NULL) && (channel != NULL) && (message != NULL)) {
		if (message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
			/* Received MRCP response. */
			if (message->start_line.method_id == SYNTHESIZER_SPEAK) {
				/* received the response to SPEAK request */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_INPROGRESS) {
					/* Waiting for SPEAK-COMPLETE event. */
					ast_log(LOG_DEBUG, "(%s) REQUEST IN PROGRESS\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_PROCESSING);
				} else {
					/* Received unexpected request_state. */
					ast_log(LOG_DEBUG, "(%s) unexpected SPEAK response, request_state = %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == SYNTHESIZER_STOP) {
				/* Received response to the STOP request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					/* Got COMPLETE. */
					ast_log(LOG_DEBUG, "(%s) COMPLETE\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
				} else {
					/* Received unexpected request state. */
					ast_log(LOG_DEBUG, "(%s) unexpected STOP response, request_state = %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == SYNTHESIZER_BARGE_IN_OCCURRED) {
				/* Received response to the BARGE_IN_OCCURRED request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					/* Got COMPLETE. */
					ast_log(LOG_DEBUG, "(%s) COMPLETE\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
				} else {
					/* Received unexpected request state. */
					ast_log(LOG_DEBUG, "(%s) unexpected BARGE_IN_OCCURRED response, request_state = %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else {
				/* Received unexpected response. */
				ast_log(LOG_DEBUG, "(%s) unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR); 
			}
		} else if (message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
			/* Received MRCP event. */
			if (message->start_line.method_id == SYNTHESIZER_SPEAK_COMPLETE) {
				/* Got SPEAK-COMPLETE. */
				ast_log(LOG_DEBUG, "(%s) SPEAK-COMPLETE\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
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

/* Incoming TTS data from UniMRCP. */
static apt_bool_t synth_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	speech_channel_t *schannel;

	if (stream != NULL)
		schannel = (speech_channel_t *)stream->obj;
	else
		schannel = NULL;

	if ((schannel != NULL) && (stream != NULL) && (frame != NULL)) {
		apr_size_t size = frame->codec_frame.size;
		speech_channel_write(schannel, frame->codec_frame.buffer, &size); 
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* Set parameter in a synthesizer MRCP header. */
static int synth_channel_set_header(speech_channel_t *schannel, int id, char *val, mrcp_message_t *msg, mrcp_synth_header_t *synth_hdr)
{
	if ((schannel == NULL) || (msg == NULL) || (synth_hdr == NULL))
		return -1;

	switch (id) {
		case SYNTHESIZER_HEADER_VOICE_GENDER:
			if (strcasecmp("male", val) == 0)
				synth_hdr->voice_param.gender = VOICE_GENDER_MALE;
			else if (strcasecmp("female", val) == 0)
				synth_hdr->voice_param.gender = VOICE_GENDER_FEMALE;
			else if (strcasecmp("neutral", val) == 0)
				synth_hdr->voice_param.gender = VOICE_GENDER_NEUTRAL;
			else {
				ast_log(LOG_WARNING, "(%s) ignoring invalid voice gender, %s\n", schannel->name, val);
				break;
			}
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_VOICE_GENDER);
			break;

		case SYNTHESIZER_HEADER_VOICE_AGE: {
			int age = atoi(val);
			if ((age > 0) && (age < 1000)) {
				synth_hdr->voice_param.age = age;
				mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_VOICE_AGE);
			} else
				ast_log(LOG_WARNING, "(%s) ignoring invalid voice age, %s\n", schannel->name, val);
			break;
		}

		case SYNTHESIZER_HEADER_VOICE_VARIANT: {
			int variant = atoi(val);
			if (variant > 0) {
				synth_hdr->voice_param.variant = variant;
				mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_VOICE_VARIANT);
			} else
				ast_log(LOG_WARNING, "(%s) ignoring invalid voice variant, %s\n", schannel->name, val);
			break;
		}

		case SYNTHESIZER_HEADER_VOICE_NAME:
			apt_string_assign(&synth_hdr->voice_param.name, val, msg->pool);
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_VOICE_NAME);
			break;

		case SYNTHESIZER_HEADER_KILL_ON_BARGE_IN:
			synth_hdr->kill_on_barge_in = (strcasecmp("true", val) == 0);
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_KILL_ON_BARGE_IN);
			break;

		case SYNTHESIZER_HEADER_PROSODY_VOLUME:
			if ((isdigit(*val)) || (*val == '.')) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_NUMERIC;
				synth_hdr->prosody_param.volume.value.numeric = (float)atof(val);
			} else if (*val == '+' || *val == '-') {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_RELATIVE_CHANGE;
				synth_hdr->prosody_param.volume.value.relative = (float)atof(val);
			} else if (strcasecmp("silent", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_SILENT;
			} else if (strcasecmp("x-soft", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_XSOFT;
			} else if (strcasecmp("soft", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_SOFT;
			} else if (strcasecmp("medium", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_MEDIUM;
			} else if (strcasecmp("loud", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_LOUD;
			} else if (strcasecmp("x-loud", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_XLOUD;
			} else if (strcasecmp("default", val) == 0) {
				synth_hdr->prosody_param.volume.type = PROSODY_VOLUME_TYPE_LABEL;
				synth_hdr->prosody_param.volume.value.label = PROSODY_VOLUME_DEFAULT;
			} else {
				ast_log(LOG_WARNING, "(%s) ignoring invalid prosody volume, %s\n", schannel->name, val);
				break;
			}
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_PROSODY_VOLUME);
			break;

		case SYNTHESIZER_HEADER_PROSODY_RATE:
			if ((isdigit(*val)) || (*val == '.')) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_RELATIVE_CHANGE;
				synth_hdr->prosody_param.rate.value.relative = (float)atof(val);
			} else if (strcasecmp("x-slow", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_XSLOW;
			} else if (strcasecmp("slow", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_SLOW;
			} else if (strcasecmp("medium", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_MEDIUM;
			} else if (strcasecmp("fast", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_FAST;
			} else if (strcasecmp("x-fast", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_XFAST;
			} else if (strcasecmp("default", val) == 0) {
				synth_hdr->prosody_param.rate.type = PROSODY_RATE_TYPE_LABEL;
				synth_hdr->prosody_param.rate.value.label = PROSODY_RATE_DEFAULT;
			} else {
				ast_log(LOG_WARNING, "(%s) ignoring invalid prosody rate, %s\n", schannel->name, val);
				break;
			}
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_PROSODY_RATE);
			break;

		case SYNTHESIZER_HEADER_SPEECH_LANGUAGE:
			apt_string_assign(&synth_hdr->speech_language, val, msg->pool);
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_SPEECH_LANGUAGE);
			break;
		
		case SYNTHESIZER_HEADER_LOAD_LEXICON:
			synth_hdr->load_lexicon = (strcasecmp("true", val) == 0);
			mrcp_resource_header_property_add(msg, SYNTHESIZER_HEADER_LOAD_LEXICON);
			break;

		/* Unsupported by this module. */
		case SYNTHESIZER_HEADER_JUMP_SIZE:
		case SYNTHESIZER_HEADER_SPEAKER_PROFILE:
		case SYNTHESIZER_HEADER_COMPLETION_CAUSE:
		case SYNTHESIZER_HEADER_COMPLETION_REASON:
		case SYNTHESIZER_HEADER_SPEECH_MARKER:
		case SYNTHESIZER_HEADER_FETCH_HINT:
		case SYNTHESIZER_HEADER_AUDIO_FETCH_HINT:
		case SYNTHESIZER_HEADER_FAILED_URI:
		case SYNTHESIZER_HEADER_FAILED_URI_CAUSE:
		case SYNTHESIZER_HEADER_SPEAK_RESTART:
		case SYNTHESIZER_HEADER_SPEAK_LENGTH:
		case SYNTHESIZER_HEADER_LEXICON_SEARCH_ORDER:
		default:
			ast_log(LOG_ERROR, "(%s) unsupported SYNTHESIZER_HEADER type (unsupported in this module)\n", schannel->name);
			break;
	}
	
	return 0;
}

/* Set parameters in a synthesizer MRCP header. */
static int synth_channel_set_params(speech_channel_t *schannel, mrcp_message_t *msg, mrcp_generic_header_t *gen_hdr, mrcp_synth_header_t *synth_hdr)
{
	if ((schannel != NULL) && (msg != NULL) && (gen_hdr != NULL) && (synth_hdr != NULL)) {
		/* Loop through each param and add to synth header or vendor-specific-params. */
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
					synth_channel_set_header(schannel, id->id, param_val, msg, synth_hdr);
				} else {
					apt_str_t apt_param_name = { 0 };
					apt_str_t apt_param_val = { 0 };

					/* This is probably a vendor-specific MRCP param. */
					ast_log(LOG_DEBUG, "(%s) (vendor-specific value) %s: %s\n", schannel->name, param_name, param_val);
					apt_string_set(&apt_param_name, param_name); /* Copy isn't necessary since apt_pair_array_append will do it. */
					apt_string_set(&apt_param_val, param_val);

					if (gen_hdr->vendor_specific_params == NULL) {
						ast_log(LOG_DEBUG, "(%s) creating vendor specific pair array\n", schannel->name);
						gen_hdr->vendor_specific_params = apt_pair_array_create(10, msg->pool);
					}

					apt_pair_array_append(gen_hdr->vendor_specific_params, &apt_param_name, &apt_param_val, msg->pool);
				}
			}
		}
	
		if (gen_hdr->vendor_specific_params != NULL)
			mrcp_generic_header_property_add(msg, GENERIC_HEADER_VENDOR_SPECIFIC_PARAMS);
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return 0;
}

/* Send SPEAK request to synthesizer. */
static int synth_channel_speak(speech_channel_t *schannel, const char *text)
{
	int status = 0;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_synth_header_t *synth_header = NULL;

	if ((schannel != NULL) && (text != NULL)) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, SYNTHESIZER_SPEAK)) == NULL) {
			ast_log(LOG_ERROR, "(%s) Failed to create SPEAK message\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Set generic header fields (content-type). */
		if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {	
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Good enough way of determining SSML or plain text body. */
		apt_string_assign(&generic_header->content_type, get_synth_content_type(schannel,text), mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

		/* Set synthesizer header fields (voice, rate, etc.). */
		if ((synth_header = (mrcp_synth_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Add params to MRCP message. */
		synth_channel_set_params(schannel, mrcp_message, generic_header, synth_header);

		/* Set body (plain text or SSML). */
		apt_string_assign(&mrcp_message->body, text, schannel->pool);

		/* Empty audio queue and send SPEAK to MRCP server. */
		audio_queue_clear(schannel->audio_queue);

		if (!mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message)) {
			ast_log(LOG_ERROR,"(%s) Failed to send SPEAK message", schannel->name);

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

static int app_synth_exec(struct ast_channel *chan, ast_app_data data)
{
	ast_format_compat nwriteformat;
	ast_format_clear(&nwriteformat);
	get_synth_format(chan, &nwriteformat);

	int samplerate = 8000;
	/* int framesize = DEFAULT_FRAMESIZE; */
	int framesize = format_to_bytes_per_sample(&nwriteformat) * (DEFAULT_FRAMESIZE / 2);
	int dtmf_enable = 0;
	struct ast_frame *f;
	struct ast_frame fr;
	struct timeval next;
	int dobreak = 1;
	int ms;
	apr_size_t len;
	int rres = 0;
	speech_channel_t *schannel = NULL;
	const char *profile_name = NULL;
	ast_mrcp_profile_t *profile = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	char name[200] = { 0 };
	FILE* fp = NULL;
	char buffer[framesize];
	int dtmfkey = -1;
	int res = 0;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(text);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (text[,options])\n", app_synth);
		pbx_builtin_setvar_helper(chan, "SYNTHSTATUS", "ERROR");
		return -1;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	char option_profile[256] = { 0 };
	char option_prate[32]= { 0 };
	char option_pvolume[32]= { 0 };
	char option_interrupt[64] = { 0 };
	char option_filename[384] = { 0 };
	char option_language[16] = { 0 }; 
	char option_loadlexicon[16] = { 0 }; 
	char option_voicename[128] = { 0 };
	char option_voicegender[16] = { 0 }; 
	char option_voiceage[32] = { 0 }; 
	char option_voicevariant[128] = { 0 }; 

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

				if (strcasecmp(key, "p") == 0) {
					strncpy(option_profile, value, sizeof(option_profile) - 1);
					option_profile[sizeof(option_profile) - 1] = '\0';
				} else if (strcasecmp(key, "i") == 0) {
					strncpy(option_interrupt, value, sizeof(option_interrupt) - 1);
					option_interrupt[sizeof(option_interrupt) - 1] = '\0';
				} else if (strcasecmp(key, "f") == 0) {
					strncpy(option_filename, value, sizeof(option_filename) - 1);
					option_filename[sizeof(option_filename) - 1] = '\0';
				} else if (strcasecmp(key, "l") == 0) {
					strncpy(option_language, value, sizeof(option_language) - 1);
					option_language[sizeof(option_language) - 1] = '\0';
				} else if (strcasecmp(key, "ll") == 0) {
					strncpy(option_loadlexicon, value, sizeof(option_loadlexicon) - 1);
					option_loadlexicon[sizeof(option_loadlexicon) - 1] = '\0';
				} else if (strcasecmp(key, "pv") == 0) {
					strncpy(option_pvolume, value, sizeof(option_pvolume) - 1);
					option_pvolume[sizeof(option_pvolume) - 1] = '\0';
				} else if (strcasecmp(key, "pr") == 0) {
					strncpy(option_prate, value, sizeof(option_prate) - 1);
					option_prate[sizeof(option_prate) - 1] = '\0';
				} else if (strcasecmp(key, "v") == 0) {
					strncpy(option_voicename, value, sizeof(option_voicename) - 1);
					option_voicename[sizeof(option_voicename) - 1] = '\0';
				} else if (strcasecmp(key, "vv") == 0) {
					strncpy(option_voicevariant, value, sizeof(option_voicevariant) - 1);
					option_voicevariant[sizeof(option_voicevariant) - 1] = '\0';
				} else if (strcasecmp(key, "g") == 0) {
					strncpy(option_voicegender, value, sizeof(option_voicegender) - 1);
					option_voicegender[sizeof(option_voicegender) - 1] = '\0';
				} else if (strcasecmp(key, "a") == 0) {
					strncpy(option_voiceage, value, sizeof(option_voiceage) - 1);
					option_voiceage[sizeof(option_voiceage) - 1] = '\0';
				}	
			}

			if (token != NULL) {
				strncpy(tempstr, token + 1, sizeof(tempstr) - 1);
				tempstr[sizeof(tempstr) - 1] = '\0';
			}
		} while (token != NULL);
	}
	
	if (!ast_strlen_zero(option_profile)) {
		ast_log(LOG_NOTICE, "Profile to use: %s\n", option_profile);
	}
	if (!ast_strlen_zero(args.text)) {
		ast_log(LOG_NOTICE, "Text to synthesize is: %s\n", args.text);
	}
	if (!ast_strlen_zero(option_filename)) {
		ast_log(LOG_NOTICE, "Filename to save to: %s\n", option_filename);
	}
	if (!ast_strlen_zero(option_language)) {
		ast_log(LOG_NOTICE, "Language to use: %s\n", option_language);
	}
	if (!ast_strlen_zero(option_loadlexicon)) {
		ast_log(LOG_NOTICE, "Load-Lexicon: %s\n", option_loadlexicon);
	}
	if (!ast_strlen_zero(option_voicename)) {
		ast_log(LOG_NOTICE, "Prosody volume use: %s\n", option_pvolume);
	}
	if (!ast_strlen_zero(option_voicename)) {
		ast_log(LOG_NOTICE, "Prosody rate use: %s\n", option_prate);
	}
	if (!ast_strlen_zero(option_voicename)) {
		ast_log(LOG_NOTICE, "Voice name to use: %s\n", option_voicename);
	}
	if (!ast_strlen_zero(option_voicegender)) {
		ast_log(LOG_NOTICE, "Voice gender to use: %s\n", option_voicegender);
	}
	if (!ast_strlen_zero(option_voiceage)) {
		ast_log(LOG_NOTICE, "Voice age to use: %s\n", option_voiceage);
	}
	if (!ast_strlen_zero(option_voicevariant)) {
		ast_log(LOG_NOTICE, "Voice variant to use: %s\n", option_voicevariant);
	}

	if (strlen(option_interrupt) > 0) {
		dtmf_enable = 1;

		if (strcasecmp(option_interrupt, "any") == 0) {
			strncpy(option_interrupt, AST_DIGIT_ANY, sizeof(option_interrupt) - 1);
			option_interrupt[sizeof(option_interrupt) - 1] = '\0';
		} else if (strcasecmp(option_interrupt, "none") == 0)
			dtmf_enable = 0;
	}
	ast_log(LOG_NOTICE, "DTMF enable: %d\n", dtmf_enable);

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	if (!ast_strlen_zero(option_filename))
		fp = fopen(option_filename, "wb");

	apr_snprintf(name, sizeof(name) - 1, "TTS-%lu", (unsigned long int)speech_channel_number);
	name[sizeof(name) - 1] = '\0';


	/* if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_SYNTHESIZER, &globals.synth, "L16", samplerate, chan) != 0) { */
	if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_SYNTHESIZER, mrcpsynth, format_to_str(&nwriteformat), samplerate, chan) != 0) {
		res = -1;
		goto done;
	}

	profile = get_synth_profile(option_profile);
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

	if (!ast_strlen_zero(option_language))
		speech_channel_set_param(schannel, "speech-language", option_language);
	if (!ast_strlen_zero(option_voicename))
		speech_channel_set_param(schannel, "voice-name", option_voicename);
	if (!ast_strlen_zero(option_voicegender))
		speech_channel_set_param(schannel, "voice-gender", option_voicegender);
	if (!ast_strlen_zero(option_voiceage))
		speech_channel_set_param(schannel, "voice-age", option_voiceage);
	if (!ast_strlen_zero(option_voicevariant))
		speech_channel_set_param(schannel, "voice-variant", option_voicevariant);
	if (!ast_strlen_zero(option_loadlexicon))
		speech_channel_set_param(schannel, "load-lexicon", option_loadlexicon);
	if (dtmf_enable)
		speech_channel_set_param(schannel, "kill-on-barge-in", "true");
	else
		speech_channel_set_param(schannel, "kill-on-barge-in", "false");
	if (!ast_strlen_zero(option_pvolume))
		speech_channel_set_param(schannel, "prosody-volume", option_pvolume);
	if (!ast_strlen_zero(option_prate))
		speech_channel_set_param(schannel, "prosody-rate", option_prate);

	ast_format_compat owriteformat;
	ast_format_clear(&owriteformat);
	ast_channel_get_writeformat(chan, &owriteformat);

	if (ast_channel_set_writeformat(chan, &nwriteformat) < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}

	if (synth_channel_speak(schannel, args.text) == 0) {
		rres = 0;
		res = 0;

		/* Wait 50 ms first for synthesis to start, to fill a frame with audio. */
		next = ast_tvadd(ast_tvnow(), ast_tv(0, 50000));

		do {
			ms = ast_tvdiff_ms(next, ast_tvnow());
			if (ms <= 0) {
				len = sizeof(buffer);
				rres = speech_channel_read(schannel, buffer, &len, 0);

				if ((rres == 0) && (len > 0)) {
					if (fp != NULL)
						fwrite(buffer, 1, len, fp);

					memset(&fr, 0, sizeof(fr));
					fr.frametype = AST_FRAME_VOICE;
					/* fr.subclass.codec = AST_FORMAT_SLINEAR; */
					ast_frame_set_format(&fr, &nwriteformat);
					fr.datalen = len;
					/* fr.samples = len / 2; */
					fr.samples = len / format_to_bytes_per_sample(&nwriteformat);
					ast_frame_set_data(&fr, buffer);
					fr.mallocd = 0;
					fr.offset = AST_FRIENDLY_OFFSET;
					fr.src = __PRETTY_FUNCTION__;
					fr.delivery.tv_sec = 0;
					fr.delivery.tv_usec = 0;

					if (ast_write(chan, &fr) < 0) {
						ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
						res = -1;
						break;
					}

					next = ast_tvadd(next, ast_samp2tv(fr.samples, samplerate));
				} else {
					if (rres == 0) {
						/* next = ast_tvadd(next, ast_samp2tv(framesize/2, samplerate)); */
						next = ast_tvadd(next, ast_samp2tv(framesize / format_to_bytes_per_sample(&nwriteformat), samplerate));
						ast_log(LOG_WARNING, "Writer starved for audio\n");
					}
				}
			} else {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_DEBUG, "Hangup detected\n");
					res = -1;
					break;
				} else if (ms) {
					f = ast_read(chan);

					if (!f) {
						ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
						res = -1;
						break;
					} else if ((dtmf_enable) && (f->frametype == AST_FRAME_DTMF)) {
						dobreak = 1;
						dtmfkey = ast_frame_get_dtmfkey(f);

						ast_log(LOG_DEBUG, "User pressed a key (%d)\n", dtmfkey);
						if (option_interrupt && strchr(option_interrupt, dtmfkey)) {
							res = dtmfkey;
							dobreak = 0;
						}

						ast_log(LOG_DEBUG, "(%s) sending BARGE-IN-OCCURRED\n", schannel->name);

						if (speech_channel_bargeinoccurred(schannel) != 0) {
							ast_log(LOG_ERROR, "(%s) Failed to send BARGE-IN-OCCURRED\n", schannel->name);
							dobreak = 0;
						}
					
						ast_frfree(f);

						if (dobreak == 0)
							break;
					} else /* Ignore other frametypes. */
						ast_frfree(f);
				}
			}
		} while (rres == 0);
	}

	ast_channel_set_writeformat(chan, &owriteformat);

	if (fp != NULL)
		fclose(fp);

	speech_channel_stop(schannel);
	speech_channel_destroy(schannel);

done:
	if (res < 0)
		pbx_builtin_setvar_helper(chan, "SYNTHSTATUS", "ERROR");
	else
		pbx_builtin_setvar_helper(chan, "SYNTHSTATUS", "OK");

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

int load_mrcpsynth_app()
{
	apr_pool_t *pool = globals.pool;

	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	if(mrcpsynth) {
		ast_log(LOG_ERROR, "Application %s is already loaded\n", app_synth);
		return -1;
	}

	mrcpsynth = (ast_mrcp_application_t*) apr_palloc(pool, sizeof(ast_mrcp_application_t));
	mrcpsynth->name = app_synth;
	mrcpsynth->exec = app_synth_exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	mrcpsynth->synopsis = synthsynopsis;
	mrcpsynth->description = synthdescrip;
#endif

	/* Create the recognizer application and link its callbacks */
	if ((mrcpsynth->app = mrcp_application_create(synth_message_handler, (void *)0, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create synthesizer MRCP application %s\n", app_synth);
		mrcpsynth = NULL;
		return -1;
	}

	mrcpsynth->dispatcher.on_session_update = NULL;
	mrcpsynth->dispatcher.on_session_terminate = speech_on_session_terminate;
	mrcpsynth->dispatcher.on_channel_add = speech_on_channel_add;
	mrcpsynth->dispatcher.on_channel_remove = speech_on_channel_remove;
	mrcpsynth->dispatcher.on_message_receive = synth_on_message_receive;
	mrcpsynth->audio_stream_vtable.destroy = NULL;
	mrcpsynth->audio_stream_vtable.open_rx = NULL;
	mrcpsynth->audio_stream_vtable.close_rx = NULL;
	mrcpsynth->audio_stream_vtable.read_frame = NULL;
	mrcpsynth->audio_stream_vtable.open_tx = NULL;
	mrcpsynth->audio_stream_vtable.close_tx =  NULL;
	mrcpsynth->audio_stream_vtable.write_frame = synth_stream_write;
	mrcpsynth->audio_stream_vtable.trace = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, mrcpsynth->app, app_synth)) {
		ast_log(LOG_ERROR, "Unable to register synthesizer MRCP application\n");
		if (!mrcp_application_destroy(mrcpsynth->app))
			ast_log(LOG_WARNING, "Unable to destroy synthesizer MRCP application\n");
		mrcpsynth = NULL;
		return -1;
	}

	/* Create a hash for the synthesizer parameter map. */
	param_id_map = apr_hash_make(pool);

	if (param_id_map != NULL) {
		apr_hash_set(param_id_map, apr_pstrdup(pool, "jump-size"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_JUMP_SIZE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "kill-on-barge-in"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_KILL_ON_BARGE_IN, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speaker-profile"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAKER_PROFILE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "completion-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_COMPLETION_CAUSE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "completion-reason"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_COMPLETION_REASON, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "voice-gender"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_GENDER, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "voice-age"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_AGE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "voice-variant"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_VARIANT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "voice-name"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_NAME, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "prosody-volume"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_PROSODY_VOLUME, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "prosody-rate"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_PROSODY_RATE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speech-marker"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEECH_MARKER, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speech-language"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEECH_LANGUAGE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "fetch-hint"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FETCH_HINT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "audio-fetch-hint"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_AUDIO_FETCH_HINT, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "failed-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FAILED_URI, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "failed-uri-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FAILED_URI_CAUSE, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speak-restart"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAK_RESTART, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "speak-length"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAK_LENGTH, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "load-lexicon"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_LOAD_LEXICON, pool));
		apr_hash_set(param_id_map, apr_pstrdup(pool, "lexicon-search-order"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_LEXICON_SEARCH_ORDER, pool));
	}

	apr_hash_set(globals.apps, app_synth, APR_HASH_KEY_STRING, mrcpsynth);

	return 0;
}

int unload_mrcpsynth_app()
{
	if(!mrcpsynth) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", app_synth);
		return -1;
	}

	/* Clear parameter ID map. */
	if (param_id_map != NULL)
		apr_hash_clear(param_id_map);

	apr_hash_set(globals.apps, app_synth, APR_HASH_KEY_STRING, NULL);
	mrcpsynth = NULL;

	return 0;
}
