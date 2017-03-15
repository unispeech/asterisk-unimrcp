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
			<parameter name="prompt" required="true">
				<para>A prompt specified as a plain text, an SSML content, or by means of a file or URI reference.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="p"> <para>Profile to use in mrcp.conf.</para> </option>
					<option name="i"> <para>Digits to allow the TTS to be interrupted with.</para> </option>
					<option name="f"> <para>Filename on disk to store audio to (audio not stored if not specified or empty).</para> </option>
					<option name="l"> <para>Language to use (e.g. "en-GB", "en-US", "en-AU", etc.).</para> </option>
					<option name="ll"> <para>Load lexicon (true/false).</para> </option>
					<option name="pv"> <para>Prosody volume (silent/x-soft/soft/medium/load/x-loud/default).</para> </option>
					<option name="pr"> <para>Prosody rate (x-slow/slow/medium/fast/x-fast/default).</para> </option>
					<option name="v"> <para>Voice name to use (e.g. "Daniel", "Karin", etc.).</para> </option>
					<option name="g"> <para>Voice gender to use (e.g. "male", "female").</para> </option>
					<option name="vv"> <para>Voice variant.</para> </option>
					<option name="a"> <para>Voice age.</para> </option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application establishes an MRCP session for speech synthesis.</para>
			<para>If synthesis completed, the variable ${SYNTHSTATUS} is set to "OK"; otherwise, if an error occurred, 
			the variable ${SYNTHSTATUS} is set to "ERROR". If the caller hung up while the synthesis was in-progress, 
			the variable ${SYNTHSTATUS} is set to "INTERRUPTED".</para>
			<para>The variable ${SYNTH_COMPLETION_CAUSE} indicates whether synthesis completed normally or with an error.
			("000" - normal, "001" - barge-in, "002" - parse-failure, ...) </para>
		</description>
		<see-also>
			<ref type="application">MRCPRecog</ref>
			<ref type="application">SynthAndRecog</ref>
		</see-also>
	</application>
 ***/

/* The name of the application. */
static const char *app_synth = "MRCPSynth";

/* The application instance. */
static ast_mrcp_application_t *mrcpsynth = NULL;

/* The enumeration of application options (excluding the MRCP params). */
enum mrcpsynth_option_flags {
	MRCPSYNTH_PROFILE        = (1 << 0),
	MRCPSYNTH_INTERRUPT      = (1 << 1),
	MRCPSYNTH_FILENAME       = (1 << 2)
};

/* The enumeration of option arguments. */
enum mrcpsynth_option_args {
	OPT_ARG_PROFILE    = 0,
	OPT_ARG_INTERRUPT  = 1,
	OPT_ARG_FILENAME   = 2,

	/* This MUST be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE = 3
};

/* The structure which holds the application options (including the MRCP params). */
struct mrcpsynth_options_t {
	apr_hash_t *synth_hfs;

	int         flags;
	const char *params[OPT_ARG_ARRAY_SIZE];
};

typedef struct mrcpsynth_options_t mrcpsynth_options_t;

/* The application session. */
struct mrcpsynth_session_t {
	apr_pool_t         *pool;
	speech_channel_t   *schannel;
	ast_format_compat  *writeformat;
};

typedef struct mrcpsynth_session_t mrcpsynth_session_t;

/* --- MRCP SPEECH CHANNEL INTERFACE TO UNIMRCP --- */

/* Get speech channel associated with provided MRCP session. */
static APR_INLINE speech_channel_t * get_speech_channel(mrcp_session_t *session)
{
	if (session)
		return (speech_channel_t *)mrcp_application_session_object_get(session);

	return NULL;
}

/* Handle the UniMRCP responses sent to session terminate requests. */
static apt_bool_t speech_on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel) {
		ast_log(LOG_ERROR, "speech_on_session_terminate: unknown channel error!\n");
		return FALSE;
	}

	ast_log(LOG_DEBUG, "(%s) speech_on_session_terminate\n", schannel->name);

	ast_log(LOG_DEBUG, "(%s) Destroying MRCP session\n", schannel->name);
	if (!mrcp_application_session_destroy(session))
		ast_log(LOG_WARNING, "(%s) Unable to destroy application session\n", schannel->name);

	speech_channel_set_state(schannel, SPEECH_CHANNEL_CLOSED);
	return TRUE;
}

/* Handle the UniMRCP responses sent to channel add requests. */
static apt_bool_t speech_on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !channel) {
		ast_log(LOG_ERROR, "speech_on_channel_add: unknown channel error!\n");
		return FALSE;
	}

	ast_log(LOG_DEBUG, "(%s) speech_on_channel_add\n", schannel->name);

	if (status == MRCP_SIG_STATUS_CODE_SUCCESS) {
		const mpf_codec_descriptor_t *descriptor = mrcp_application_sink_descriptor_get(channel);
		if (!descriptor) {
			ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			return FALSE;
		}

		schannel->rate = descriptor->sampling_rate;
		const char *codec_name = NULL;
		if (descriptor->name.length > 0)
			codec_name = descriptor->name.buf;
		else
			codec_name = "unknown";

		ast_log(LOG_NOTICE, "(%s) Channel ready, codec=%s, sample rate=%d\n",
			schannel->name,
			codec_name,
			schannel->rate);
		speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
	} else {
		int rc = mrcp_application_session_response_code_get(session);
		ast_log(LOG_ERROR, "(%s) Channel error status=%d, response code=%d!\n", schannel->name, status, rc);
		speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
	}

	return TRUE;
}

/* --- MRCP TTS --- */

/* Process UniMRCP messages for the synthesizer application.  All MRCP synthesizer callbacks start here first. */
static apt_bool_t synth_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message)
		return mrcp_application_message_dispatch(&mrcpsynth->dispatcher, app_message);

	ast_log(LOG_ERROR, "(unknown) app_message error!\n");
	return TRUE;
}

/* Handle the MRCP synthesizer responses/events from UniMRCP. */
static apt_bool_t synth_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !message) {
		ast_log(LOG_ERROR, "synth_on_message_receive: unknown channel error!\n");
		return FALSE;
	}

	mrcp_synth_header_t *synth_header = (mrcp_synth_header_t *)mrcp_resource_header_get(message);

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
				ast_log(LOG_DEBUG, "(%s) Unexpected SPEAK response, request_state = %d\n", schannel->name, message->start_line.request_state);
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
				ast_log(LOG_DEBUG, "(%s) Unexpected STOP response, request_state = %d\n", schannel->name, message->start_line.request_state);
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
				ast_log(LOG_DEBUG, "(%s) Unexpected BARGE-IN-OCCURRED response, request_state = %d\n", schannel->name, message->start_line.request_state);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else {
			/* Received unexpected response. */
			ast_log(LOG_DEBUG, "(%s) Unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR); 
		}
	} else if (message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
		/* Received MRCP event. */
		if (message->start_line.method_id == SYNTHESIZER_SPEAK_COMPLETE) {
			/* Got SPEAK-COMPLETE. */
			const char *completion_cause = apr_psprintf(schannel->pool, "%03d", synth_header->completion_cause);
			pbx_builtin_setvar_helper(schannel->chan, "SYNTH_COMPLETION_CAUSE", completion_cause);
			ast_log(LOG_DEBUG, "(%s) SPEAK-COMPLETE\n", schannel->name);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
		} else {
			ast_log(LOG_DEBUG, "(%s) Unexpected event, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
		}
	} else {
		ast_log(LOG_DEBUG, "(%s) Unexpected message type, message_type = %d\n", schannel->name, message->start_line.message_type);
		speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
	}

	return TRUE;
}

/* Incoming TTS data from UniMRCP. */
static apt_bool_t synth_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	speech_channel_t *schannel;

	if (stream)
		schannel = (speech_channel_t *)stream->obj;
	else
		schannel = NULL;

	if(!schannel || !frame) {
		ast_log(LOG_ERROR, "synth_stream_write: unknown channel error!\n");
		return FALSE;
	}

	if (frame->codec_frame.size > 0 && (frame->type & MEDIA_FRAME_TYPE_AUDIO) == MEDIA_FRAME_TYPE_AUDIO) {
		speech_channel_ast_write(schannel, frame->codec_frame.buffer, frame->codec_frame.size);
	}

	return TRUE;
}

/* Send SPEAK request to synthesizer. */
static int synth_channel_speak(speech_channel_t *schannel, const char *content, const char *content_type, apr_hash_t *header_fields)
{
	int status = 0;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_synth_header_t *synth_header = NULL;

	if (!schannel || !content || !content_type) {
		ast_log(LOG_ERROR, "synth_channel_speak: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state != SPEECH_CHANNEL_READY) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, SYNTHESIZER_SPEAK)) == NULL) {
		ast_log(LOG_ERROR, "(%s) Failed to create SPEAK message\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Set generic header fields (content-type). */
	if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {	
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	apt_string_assign(&generic_header->content_type, content_type, mrcp_message->pool);
	mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

	/* Set synthesizer header fields (voice, rate, etc.). */
	if ((synth_header = (mrcp_synth_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Add params to MRCP message. */
	speech_channel_set_params(schannel, mrcp_message, header_fields);

	/* Set body (plain text or SSML). */
	apt_string_assign(&mrcp_message->body, content, schannel->pool);

	/* Empty audio queue and send SPEAK to MRCP server. */
	audio_queue_clear(schannel->audio_queue);

	if (!mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message)) {
		ast_log(LOG_ERROR,"(%s) Failed to send SPEAK message", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Wait for IN PROGRESS. */
	apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech-channel-timeout-usec);

	if (schannel->state != SPEECH_CHANNEL_PROCESSING) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Apply application options. */
static int mrcpsynth_option_apply(mrcpsynth_options_t *options, const char *key, const char *value)
{
	if (strcasecmp(key, "p") == 0) {
		options->flags |= MRCPSYNTH_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
	} else if (strcasecmp(key, "i") == 0) {
		options->flags |= MRCPSYNTH_INTERRUPT;
		options->params[OPT_ARG_INTERRUPT] = value;
	} else if (strcasecmp(key, "f") == 0) {
		options->flags |= MRCPSYNTH_FILENAME;
		options->params[OPT_ARG_FILENAME] = value;
	} else if (strcasecmp(key, "l") == 0) {
		apr_hash_set(options->synth_hfs, "Speech-Language", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "ll") == 0) {
		apr_hash_set(options->synth_hfs, "Load-Lexicon", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "pv") == 0) {
		apr_hash_set(options->synth_hfs, "Prosody-Volume", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "pr") == 0) {
		apr_hash_set(options->synth_hfs, "Prosody-Rate", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "v") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Name", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vv") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Variant", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "g") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Gender", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "a") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Age", APR_HASH_KEY_STRING, value);
	}
	else {
		ast_log(LOG_WARNING, "Unknown option: %s\n", key);
	}
	return 0;
}

/* Parse application options. */
static int mrcpsynth_options_parse(char *str, mrcpsynth_options_t *options, apr_pool_t *pool)
{
	char *s;
	char *name, *value;
	
	if (!str)
		return 0;

	if ((options->synth_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	while ((s = strsep(&str, "&"))) {
		value = s;
		if ((name = strsep(&value, "=")) && value) {
			ast_log(LOG_DEBUG, "Apply option %s: %s\n", name, value);
			mrcpsynth_option_apply(options, name, value);
		}
	}
	return 0;
}

/* Exit the application. */
static int mrcpsynth_exit(struct ast_channel *chan, mrcpsynth_session_t *mrcpsynth_session, speech_channel_status_t status)
{
	if (mrcpsynth_session) {
		if (mrcpsynth_session->writeformat)
			ast_channel_set_writeformat(chan, mrcpsynth_session->writeformat);

		if (mrcpsynth_session->schannel)
			speech_channel_destroy(mrcpsynth_session->schannel);

		if (mrcpsynth_session->pool)
			apr_pool_destroy(mrcpsynth_session->pool);
	}

	const char *status_str = speech_channel_status_to_string(status);
	pbx_builtin_setvar_helper(chan, "SYNTHSTATUS", status_str);
	ast_log(LOG_NOTICE, "%s() exiting status: %s on %s\n", app_synth, status_str, ast_channel_name(chan));

	return status != SPEECH_CHANNEL_STATUS_ERROR ? 0 : -1;
}

/* The entry point of the application. */
static int app_synth_exec(struct ast_channel *chan, ast_app_data data)
{
	struct ast_frame *f;
	ast_mrcp_profile_t *profile;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	const char *name;
	speech_channel_status_t status;
	char *parse;
	int i;
	mrcpsynth_options_t mrcpsynth_options;
	mrcpsynth_session_t mrcpsynth_session;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(prompt);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s() requires an argument (prompt[,options])\n", app_synth);
		return mrcpsynth_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.prompt)) {
		ast_log(LOG_WARNING, "%s() requires a prompt argument (prompt[,options])\n", app_synth);
		return mrcpsynth_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	args.prompt = normalize_input_string(args.prompt);
	ast_log(LOG_NOTICE, "%s() prompt: %s\n", app_synth, args.prompt);

	if ((mrcpsynth_session.pool = apt_pool_create()) == NULL) {
		ast_log(LOG_ERROR, "Unable to create memory pool for speech channel\n");
		return mrcpsynth_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	mrcpsynth_session.schannel = NULL;
	mrcpsynth_session.writeformat = NULL;

	mrcpsynth_options.synth_hfs = NULL;
	mrcpsynth_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		mrcpsynth_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		args.options = normalize_input_string(args.options);
		ast_log(LOG_NOTICE, "%s() options: %s\n", app_synth, args.options);
		char *options_buf = apr_pstrdup(mrcpsynth_session.pool, args.options);
		mrcpsynth_options_parse(options_buf, &mrcpsynth_options, mrcpsynth_session.pool);
	}

	int dtmf_enable = 0;
	if ((mrcpsynth_options.flags & MRCPSYNTH_INTERRUPT) == MRCPSYNTH_INTERRUPT) {
		if (!ast_strlen_zero(mrcpsynth_options.params[OPT_ARG_INTERRUPT])) {
			dtmf_enable = 1;

			if (strcasecmp(mrcpsynth_options.params[OPT_ARG_INTERRUPT], "any") == 0) {
				mrcpsynth_options.params[OPT_ARG_INTERRUPT] = AST_DIGIT_ANY;
			} else if (strcasecmp(mrcpsynth_options.params[OPT_ARG_INTERRUPT], "none") == 0)
				dtmf_enable = 0;
		}
	}

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	const char *filename = NULL;
	if ((mrcpsynth_options.flags & MRCPSYNTH_FILENAME) == MRCPSYNTH_FILENAME) {
		filename = mrcpsynth_options.params[OPT_ARG_FILENAME];
	}

	ast_format_compat *nwriteformat = ast_channel_get_speechwriteformat(chan, mrcpsynth_session.pool);
	int samplerate = 8000;

	name = apr_psprintf(mrcpsynth_session.pool, "TTS-%lu", (unsigned long int)speech_channel_number);

	mrcpsynth_session.schannel = speech_channel_create(
									mrcpsynth_session.pool,
									name,
									SPEECH_CHANNEL_SYNTHESIZER,
									mrcpsynth,
									nwriteformat,
									samplerate,
									filename,
									chan);
	if (!mrcpsynth_session.schannel) {
		return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	const char *profile_name = NULL;
	if ((mrcpsynth_options.flags & MRCPSYNTH_PROFILE) == MRCPSYNTH_PROFILE) {
		if (!ast_strlen_zero(mrcpsynth_options.params[OPT_ARG_PROFILE])) {
			profile_name = mrcpsynth_options.params[OPT_ARG_PROFILE];
		}
	}

	profile = get_synth_profile(profile_name);
	if (!profile) {
		ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", name, profile_name);
		return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	if (speech_channel_open(mrcpsynth_session.schannel, profile) != 0) {
		return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	ast_format_compat *owriteformat = ast_channel_get_writeformat(chan, mrcpsynth_session.pool);
	ast_channel_set_writeformat(chan, nwriteformat);
	mrcpsynth_session.writeformat = owriteformat;

	const char *content = NULL;
	const char *content_type = NULL;
	if (determine_synth_content_type(mrcpsynth_session.schannel, args.prompt, &content, &content_type) != 0) {
		ast_log(LOG_WARNING, "(%s) Unable to determine synthesis content type\n", name);
		return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	ast_log(LOG_NOTICE, "(%s) Synthesizing, enable DTMFs: %d\n", name, dtmf_enable);

	if (synth_channel_speak(mrcpsynth_session.schannel, content, content_type, mrcpsynth_options.synth_hfs) != 0) {
		ast_log(LOG_WARNING, "(%s) Unable to start synthesis\n", name);
		return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	int ms;
	int running;
	status = SPEECH_CHANNEL_STATUS_OK;
	do {
		ms = ast_waitfor(chan, 100);
		if (ms < 0) {
			ast_log(LOG_DEBUG, "(%s) Hangup detected\n", name);
			return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
		}

		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_DEBUG, "(%s) Null frame == hangup() detected\n", name);
			return mrcpsynth_exit(chan, &mrcpsynth_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
		}

		running = 1;
		if (dtmf_enable && f->frametype == AST_FRAME_DTMF) {
			int dtmfkey = ast_frame_get_dtmfkey(f);

			ast_log(LOG_DEBUG, "(%s) User pressed a key (%d)\n", name, dtmfkey);
			if (mrcpsynth_options.params[OPT_ARG_INTERRUPT] && strchr(mrcpsynth_options.params[OPT_ARG_INTERRUPT], dtmfkey)) {
				status = SPEECH_CHANNEL_STATUS_INTERRUPTED;
				running = 0;

				ast_log(LOG_DEBUG, "(%s) Sending BARGE-IN-OCCURRED\n", mrcpsynth_session.schannel->name);
				if (speech_channel_bargeinoccurred(mrcpsynth_session.schannel) != 0) {
					ast_log(LOG_ERROR, "(%s) Failed to send BARGE-IN-OCCURRED\n", mrcpsynth_session.schannel->name);
				}
			}
		}

		ast_frfree(f);

		if (mrcpsynth_session.schannel->state != SPEECH_CHANNEL_PROCESSING) {
			/* end of prompt */
			running = 0;
		}
	}
	while (running);

	return mrcpsynth_exit(chan, &mrcpsynth_session, status);
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
	mrcpsynth->synopsis = NULL;
	mrcpsynth->description = NULL;
#endif

	/* Create the synthesizer application and link its callbacks */
	if ((mrcpsynth->app = mrcp_application_create(synth_message_handler, (void *)0, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create synthesizer MRCP application %s\n", app_synth);
		mrcpsynth = NULL;
		return -1;
	}

	mrcpsynth->dispatcher.on_session_update = NULL;
	mrcpsynth->dispatcher.on_session_terminate = speech_on_session_terminate;
	mrcpsynth->dispatcher.on_channel_add = speech_on_channel_add;
	mrcpsynth->dispatcher.on_channel_remove = NULL;
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
		ast_log(LOG_ERROR, "Unable to register synthesizer MRCP application %s\n", app_synth);
		if (!mrcp_application_destroy(mrcpsynth->app))
			ast_log(LOG_WARNING, "Unable to destroy synthesizer MRCP application %s\n", app_synth);
		mrcpsynth = NULL;
		return -1;
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

	apr_hash_set(globals.apps, app_synth, APR_HASH_KEY_STRING, NULL);
	mrcpsynth = NULL;

	return 0;
}
