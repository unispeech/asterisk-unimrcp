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
 * \brief MRCP synthesis and recognition application
 *
 * \author\verbatim Arsen Chaloyan <arsen.chaloyan@unimrcp.org> \endverbatim
 * 
 * \ingroup applications
 */

/* Asterisk includes. */
#include "ast_compat_defs.h"

#include "asterisk/pbx.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/app.h"

/* UniMRCP includes. */
#include "app_datastore.h"
#include "app_msg_process_dispatcher.h"
#include "app_channel_methods.h"
#include "mrcp_client_session.h"

/*** DOCUMENTATION
	<application name="SynthAndRecog" language="en_US">
		<synopsis>
			Play a synthesized prompt and wait for speech to be recognized.
		</synopsis>
		<syntax>
			<parameter name="prompt" required="true">
				<para>A prompt specified as a plain text, an SSML content, or by means of a file or URI reference.</para>
			</parameter>
			<parameter name="grammar" required="true">
				<para>An inline or URI grammar to be used for recognition.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="p"> <para>Profile to use in mrcp.conf.</para> </option>
					<option name="t"> <para>Recognition timeout (msec).</para> </option>
					<option name="b"> <para>Bargein value (0: no barge-in, 1: enable barge-in).</para> </option>
					<option name="gd"> <para>Grammar delimiters.</para> </option>
					<option name="ct"> <para>Confidence threshold (0.0 - 1.0).</para> </option>
					<option name="sl"> <para>Sensitivity level (0.0 - 1.0).</para> </option>
					<option name="sva"> <para>Speed vs accuracy (0.0 - 1.0).</para> </option>
					<option name="nb"> <para>N-best list length.</para> </option>
					<option name="nit"> <para>No input timeout (msec).</para> </option>
					<option name="sct"> <para>Speech complete timeout (msec).</para> </option>
					<option name="sint"> <para>Speech incomplete timeout (msec).</para> </option>
					<option name="dit"> <para>DTMF interdigit timeout (msec).</para> </option>
					<option name="dtt"> <para>DTMF terminate timeout (msec).</para> </option>
					<option name="dttc"> <para>DTMF terminate characters.</para> </option>
					<option name="sw"> <para>Save waveform (true/false).</para> </option>
					<option name="nac"> <para>New audio channel (true/false).</para> </option>
					<option name="spl"> <para>Speech language (en-US/en-GB/etc).</para> </option>
					<option name="rm"> <para>Recognition mode (normal/hotword).</para> </option>
					<option name="hmaxd"> <para>Hotword max duration (msec).</para> </option>
					<option name="hmind"> <para>Hotword min duration (msec).</para> </option>
					<option name="cdb"> <para>Clear DTMF buffer (true/false).</para> </option>
					<option name="enm"> <para>Early nomatch (true/false).</para> </option>
					<option name="iwu"> <para>Input waveform URI.</para> </option>
					<option name="vbu"> <para>Verify Buffer Utterance (true/false).</para> </option>
					<option name="mt"> <para>Media type.</para> </option>
					<option name="pv"> <para>Prosody volume (silent/x-soft/soft/medium/loud/x-loud/default).</para> </option>
					<option name="pr"> <para>Prosody rate (x-slow/slow/medium/fast/x-fast/default).</para> </option>
					<option name="vn"> <para>Voice name to use (e.g. "Daniel", "Karin", etc.).</para> </option>
					<option name="vg"> <para>Voice gender to use (e.g. "male", "female").</para> </option>
					<option name="vv"> <para>Voice variant.</para> </option>
					<option name="a"> <para>Voice age.</para> </option>
					<option name="uer"> <para>URI-encoded results 
						(1: URI-encode NLMSL results, 0: do not encode).</para>
					</option>
					<option name="od"> <para>Output (prompt) delimiters.</para> </option>
					<option name="sit"> <para>Start input timers value (0: no, 1: yes [start with RECOGNIZE],
						2: auto [start when prompt is finished]).</para>
					</option>
					<option name="plt"> <para>Persistent lifetime (0: no [MRCP session is created and destroyed dynamically],
						1: yes [MRCP session is created on demand, reused and destroyed on hang-up].</para>
					</option>
					<option name="dse"> <para>Datastore entry.</para></option>
					<option name="sbs"> <para>Always stop barged synthesis request.</para></option>
					<option name="vsp"> <para>Vendor-specific parameters.</para></option>
					<option name="nif"> <para>NLSML instance format (either "xml" or "json") used by RECOG_INSTANCE().</para></option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application establishes two MRCP sessions: one for speech synthesis and the other for speech recognition.
			Once the user starts speaking (barge-in occurred), the synthesis session is stopped, and the recognition engine
			starts processing the input. Once recognition completes, the application exits and returns results to the dialplan.</para>
			<para>If recognition completed, the variable ${RECOG_STATUS} is set to "OK". Otherwise, if recognition couldn't be started,
			the variable ${RECOG_STATUS} is set to "ERROR". If the caller hung up while recognition was still in-progress,
			the variable ${RECOG_STATUS} is set to "INTERRUPTED".</para>
			<para>The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed successfully with a match or
			an error occurred. ("000" - success, "001" - nomatch, "002" - noinput) </para>
			<para>If recognition completed successfully, the variable ${RECOG_RESULT} is set to an NLSML result received
			from the MRCP server. Alternatively, the recognition result data can be retrieved by using the following dialplan
			functions RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().</para>
		</description>
		<see-also>
			<ref type="application">MRCPSynth</ref>
			<ref type="application">MRCPRecog</ref>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</application>
 ***/

/* The name of the application. */
static const char *synthandrecog_name = "SynthAndRecog";

/* The application instance. */
static ast_mrcp_application_t *synthandrecog = NULL;

/* The enumeration of application options (excluding the MRCP params). */
enum sar_option_flags {
	SAR_RECOG_PROFILE          = (1 << 0),
	SAR_SYNTH_PROFILE          = (1 << 1),
	SAR_BARGEIN                = (1 << 2),
	SAR_GRAMMAR_DELIMITERS     = (1 << 3),
	SAR_URI_ENCODED_RESULTS    = (1 << 4),
	SAR_OUTPUT_DELIMITERS      = (1 << 5),
	SAR_INPUT_TIMERS           = (1 << 6),
	SAR_PERSISTENT_LIFETIME    = (1 << 7),
	SAR_DATASTORE_ENTRY        = (1 << 8),
	SAR_STOP_BARGED_SYNTH      = (1 << 9),
	SAR_INSTANCE_FORMAT        = (1 << 10)
};

/* The enumeration of plocies for the use of input timers. */
enum sar_it_policies {
	IT_POLICY_OFF               = 0, /* do not start input timers */
	IT_POLICY_ON                = 1, /* start input timers with RECOGNIZE */
	IT_POLICY_AUTO                   /* start input timers once prompt is finished [default] */
};

/* The prompt item structure. */
struct sar_prompt_item_t {
	const char *content;
	int         is_audio_file;
};

typedef struct sar_prompt_item_t sar_prompt_item_t;

/* Get speech channel associated with provided MRCP session. */
static APR_INLINE speech_channel_t * get_speech_channel(mrcp_session_t *session)
{
	if (session)
		return (speech_channel_t *)mrcp_application_session_object_get(session);

	return NULL;
}

/* --- MRCP TTS --- */

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
static int synth_channel_speak(speech_channel_t *schannel, const char *content, const char *content_type, mrcprecogverif_options_t *options)
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

	if ((mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session, schannel->unimrcp_channel, SYNTHESIZER_SPEAK)) == NULL) {
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
	speech_channel_set_params(schannel, mrcp_message, options->synth_hfs, options->syn_vendor_par_list);

	/* Set body (plain text or SSML). */
	apt_string_assign(&mrcp_message->body, content, schannel->pool);

	/* Empty audio queue and send SPEAK to MRCP server. */
	audio_queue_clear(schannel->audio_queue);

	if (!mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel, mrcp_message)) {
		ast_log(LOG_ERROR,"(%s) Failed to send SPEAK message", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Wait for IN PROGRESS. */
	apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech_channel_timeout);

	if (schannel->state != SPEECH_CHANNEL_PROCESSING) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Send BARGE-IN-OCCURRED. */
int synth_channel_bargein_occurred(speech_channel_t *schannel) 
{
	int status = 0;
	
	if (!schannel) {
		ast_log(LOG_ERROR, "bargein_occurred: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
		mrcp_method_id method;
		mrcp_message_t *mrcp_message;

#if 1	/* Use STOP instead of BARGE-IN-OCCURRED for now. */
		method = SYNTHESIZER_STOP;
#else
		method = SYNTHESIZER_BARGE_IN_OCCURRED;
#endif
		ast_log(LOG_DEBUG, "(%s) Sending BARGE-IN-OCCURRED request\n", schannel->name);

		/* Send BARGE-IN-OCCURRED to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session, schannel->unimrcp_channel, method);

		if (mrcp_message) {
			mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel, mrcp_message);
		} else {
			ast_log(LOG_ERROR, "(%s) Failed to create BARGE-IN-OCCURRED message\n", schannel->name);
			status = -1;
		}
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* --- MRCP ASR --- */

/* Apply application options. */
static int synthandrecog_option_apply(mrcprecogverif_options_t *options, const char *key, char *value)
{
	char *vendor_name, *vendor_value;
	if (strcasecmp(key, "ct") == 0) {
		apr_hash_set(options->recog_hfs, "Confidence-Threshold", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sva") == 0) {
		apr_hash_set(options->recog_hfs, "Speed-vs-Accuracy", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nb") == 0) {
		apr_hash_set(options->recog_hfs, "N-Best-List-Length", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nit") == 0) {
		apr_hash_set(options->recog_hfs, "No-Input-Timeout", APR_HASH_KEY_STRING, value);
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
		apr_hash_set(options->synth_hfs, "Speech-Language", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "mt") == 0) {
		apr_hash_set(options->recog_hfs, "Media-Type", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vbu") == 0) {
		apr_hash_set(options->recog_hfs, "Ver-Buffer-Utterance", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "pv") == 0) {
		apr_hash_set(options->synth_hfs, "Prosody-Volume", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "pr") == 0) {
		apr_hash_set(options->synth_hfs, "Prosody-Rate", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vn") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Name", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vv") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Variant", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vg") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Gender", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "a") == 0) {
		apr_hash_set(options->synth_hfs, "Voice-Age", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vsp") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->rec_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
			apr_hash_set(options->syn_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "vsprec") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->rec_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "vspsyn") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->syn_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "p") == 0) {
		/* Set the same profile for synth and recog. There might be a separate 
		configuration option for each of them in the future. */
		options->flags |= SAR_RECOG_PROFILE | SAR_SYNTH_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
		options->params[OPT_ARG_SYNTH_PROFILE] = value;
	} else if (strcasecmp(key, "prec") == 0) {
		/* Separate configuration option for recognizer operation */
		options->flags |= SAR_RECOG_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
	} else if (strcasecmp(key, "psyn") == 0) {
		/* Separate configuration option for synthesizer operation */
		options->flags |= SAR_SYNTH_PROFILE;
		options->params[OPT_ARG_SYNTH_PROFILE] = value;
	} else if (strcasecmp(key, "t") == 0) {
		apr_hash_set(options->recog_hfs, "Recognition-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "b") == 0) {
		options->flags |= SAR_BARGEIN;
		options->params[OPT_ARG_BARGEIN] = value;
	} else if (strcasecmp(key, "gd") == 0) {
		options->flags |= SAR_GRAMMAR_DELIMITERS;
		options->params[OPT_ARG_GRAMMAR_DELIMITERS] = value;
	} else if (strcasecmp(key, "uer") == 0) {
		options->flags |= SAR_URI_ENCODED_RESULTS;
		options->params[OPT_ARG_URI_ENCODED_RESULTS] = value;
	} else if (strcasecmp(key, "od") == 0) {
		options->flags |= SAR_OUTPUT_DELIMITERS;
		options->params[OPT_ARG_OUTPUT_DELIMITERS] = value;
	} else if (strcasecmp(key, "sit") == 0) {
		options->flags |= SAR_INPUT_TIMERS;
		options->params[OPT_ARG_INPUT_TIMERS] = value;
	} else if (strcasecmp(key, "plt") == 0) {
		options->flags |= SAR_PERSISTENT_LIFETIME;
		options->params[OPT_ARG_PERSISTENT_LIFETIME] = value;
	} else if (strcasecmp(key, "dse") == 0) {
		options->flags |= SAR_DATASTORE_ENTRY;
		options->params[OPT_ARG_DATASTORE_ENTRY] = value;
	} else if (strcasecmp(key, "sbs") == 0) {
		options->flags |= SAR_STOP_BARGED_SYNTH;
		options->params[OPT_ARG_STOP_BARGED_SYNTH] = value;
	} else if (strcasecmp(key, "nif") == 0) {
		options->flags |= SAR_INSTANCE_FORMAT;
		options->params[OPT_ARG_INSTANCE_FORMAT] = value;
	} else {
		ast_log(LOG_WARNING, "Unknown option: %s\n", key);
	}
	return 0;
}

/* Parse application options. */
static int synthandrecog_options_parse(char *str, mrcprecogverif_options_t *options, apr_pool_t *pool)
{
	char *s;
	char *name, *value;

	if (!str) 
		return 0;

	if ((options->recog_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}
	if ((options->synth_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->syn_vendor_par_list = apr_hash_make(pool)) == NULL) {
		return -1;
	}
	if ((options->rec_vendor_par_list = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	while ((s = strsep(&str, "&"))) {
		value = s;
		if ((name = strsep(&value, "=")) && value) {
			ast_log(LOG_DEBUG, "Apply option %s: %s\n", name, value);
			synthandrecog_option_apply(options, name, value);
		}
	}
	return 0;
}

/* Return the number of prompts which still needs to be played. */
static APR_INLINE int synthandrecog_prompts_available(app_session_t *app_session)
{
	if(app_session->cur_prompt >= app_session->prompts->nelts)
		return 0;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Advance the current prompt index and return the number of prompts remaining. */
static APR_INLINE int synthandrecog_prompts_advance(app_session_t *app_session)
{
	if(app_session->cur_prompt >= app_session->prompts->nelts)
		return -1;
	app_session->cur_prompt++;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Start playing the current prompt. */
static sar_prompt_item_t* synthandrecog_prompt_play(app_datastore_t* datastore, app_session_t *app_session, mrcprecogverif_options_t *sar_options)
{
	if(app_session->cur_prompt >= app_session->prompts->nelts) {
		ast_log(LOG_ERROR, "(%s) Out of bounds prompt index\n", app_session->synth_channel->name);
		return NULL;
	}

	sar_prompt_item_t *prompt_item = &APR_ARRAY_IDX(app_session->prompts, app_session->cur_prompt, sar_prompt_item_t);

	if(prompt_item->is_audio_file) {
		app_session->filestream = astchan_stream_file(datastore->chan, prompt_item->content, &app_session->max_filelength);
		if (!app_session->filestream) {
			return NULL;
		}
		/* If synth channel has already been created, destroy it at this stage in order to release an associated TTS license. */
		if (app_session->synth_channel && app_session->lifetime == APP_SESSION_LIFETIME_DYNAMIC) {
			speech_channel_destroy(app_session->synth_channel);
			app_session->synth_channel = NULL;
		}
	}
	else {
		if (!app_session->synth_channel) {
			const char *synth_name = apr_psprintf(app_session->pool, "TTS-%lu", (unsigned long int)app_session->schannel_number);

			/* Create speech channel for synthesis. */
			app_session->synth_channel = speech_channel_create(
											app_session->pool,
											synth_name,
											SPEECH_CHANNEL_SYNTHESIZER,
											synthandrecog,
											app_session->nwriteformat,
											NULL,
											datastore->chan,
											app_session->recog_channel->session);
			if (!app_session->synth_channel) {
				return NULL;
			}
			app_session->synth_channel->app_session = app_session;

			ast_mrcp_profile_t *synth_profile = NULL;
			const char *synth_profile_option = NULL;
			if ((sar_options->flags & SAR_SYNTH_PROFILE) == SAR_SYNTH_PROFILE) {
				if (!ast_strlen_zero(sar_options->params[OPT_ARG_SYNTH_PROFILE])) {
					synth_profile_option = sar_options->params[OPT_ARG_SYNTH_PROFILE];
				}
			}

			/* Get synthesis profile. */
			synth_profile = get_synth_profile(synth_profile_option);
			if (!synth_profile) {
				ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", app_session->synth_channel->name, synth_profile_option);
				return NULL;
			}

			/* Open synthesis channel. */
			if (speech_channel_open(app_session->synth_channel, synth_profile) != 0) {
				ast_log(LOG_ERROR, "(%s) Unable to open speech channel\n", app_session->synth_channel->name);
				return NULL;
			}
		}

		const char *content = NULL;
		const char *content_type = NULL;
		/* Determine synthesis content type. */
		if (determine_synth_content_type(app_session->synth_channel, prompt_item->content, &content, &content_type) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to determine synthesis content type\n", app_session->synth_channel->name);
			return NULL;
		}

		/* Start synthesis. */
		if (synth_channel_speak(app_session->synth_channel, content, content_type, sar_options) != 0) {
			ast_log(LOG_ERROR, "(%s) Unable to send SPEAK request\n", app_session->synth_channel->name);
			return NULL;
		}
	}

	return prompt_item;
}

/* Exit the application. */
static int synthandrecog_exit(struct ast_channel *chan, app_session_t *app_session, speech_channel_status_t status)
{
	if (app_session) {
		if (app_session->writeformat && app_session->rawwriteformat)
			ast_set_write_format_path(chan, app_session->writeformat, app_session->rawwriteformat);

		if (app_session->readformat && app_session->rawreadformat)
			ast_set_read_format_path(chan, app_session->rawreadformat, app_session->readformat);

		if (app_session->recog_channel)
			if (app_session->recog_channel->session_id)
				pbx_builtin_setvar_helper(chan, "RECOG_SID", app_session->recog_channel->session_id);

		if (app_session->lifetime == APP_SESSION_LIFETIME_DYNAMIC) {
			if (app_session->synth_channel) {
				if (app_session->stop_barged_synth == TRUE) {
					speech_channel_stop(app_session->synth_channel);
				}
				speech_channel_destroy(app_session->synth_channel);
				app_session->synth_channel = NULL;
			}

			if (app_session->recog_channel) {
				speech_channel_destroy(app_session->recog_channel);
				app_session->recog_channel = NULL;
			}
		}
	}

	const char *status_str = speech_channel_status_to_string(status);
	pbx_builtin_setvar_helper(chan, "RECOGSTATUS", status_str);
	pbx_builtin_setvar_helper(chan, "SYNTHSTATUS", status_str);
	ast_log(LOG_NOTICE, "%s() exiting status: %s on %s\n", synthandrecog_name, status_str, ast_channel_name(chan));
	return 0;
}

/* The entry point of the application. */
static int app_synthandrecog_exec(struct ast_channel *chan, ast_app_data data)
{
	struct ast_frame *f = NULL;
	apr_size_t len;

	const char *recog_name;
	speech_channel_status_t syn_status = SPEECH_CHANNEL_STATUS_OK;
	speech_channel_status_t status = SPEECH_CHANNEL_STATUS_OK;

	mrcprecogverif_options_t sar_options;
	char *parse;
	int i;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(prompt);
		AST_APP_ARG(grammar);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s() requires arguments (prompt,grammar[,options])\n", synthandrecog_name);
		return synthandrecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.prompt)) {
		ast_log(LOG_WARNING, "%s() requires a prompt argument (prompt,grammar[,options])\n", synthandrecog_name);
		return synthandrecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	args.prompt = normalize_input_string(args.prompt);
	ast_log(LOG_NOTICE, "%s() prompt: %s\n", synthandrecog_name, args.prompt);

	if (ast_strlen_zero(args.grammar)) {
		ast_log(LOG_WARNING, "%s() requires a grammar argument (prompt,grammar[,options])\n", synthandrecog_name);
		return synthandrecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	args.grammar = normalize_input_string(args.grammar);
	ast_log(LOG_NOTICE, "%s() grammar: %s\n", synthandrecog_name, args.grammar);
	
	app_datastore_t* datastore = app_datastore_get(chan);
	if (!datastore) {
		ast_log(LOG_ERROR, "Unable to retrieve data from app datastore on %s\n", ast_channel_name(chan));
		return synthandrecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}
	
	sar_options.recog_hfs = NULL;
	sar_options.synth_hfs = NULL;
	sar_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		sar_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		args.options = normalize_input_string(args.options);
		ast_log(LOG_NOTICE, "%s() options: %s\n", synthandrecog_name, args.options);
		char *options_buf = apr_pstrdup(datastore->pool, args.options);
		synthandrecog_options_parse(options_buf, &sar_options, datastore->pool);
	}

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	/* Ensure no streams are currently playing. */
	ast_stopstream(chan);

	/* Set default lifetime to dynamic. */
	int lifetime = APP_SESSION_LIFETIME_DYNAMIC;

	/* Get datastore entry. */
	const char *entry = DEFAULT_DATASTORE_ENTRY;
	if ((sar_options.flags & SAR_DATASTORE_ENTRY) == SAR_DATASTORE_ENTRY) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_DATASTORE_ENTRY])) {
			entry = sar_options.params[OPT_ARG_DATASTORE_ENTRY];
			lifetime = APP_SESSION_LIFETIME_PERSISTENT;
		}
	}

	/* Check session lifetime. */
	if ((sar_options.flags & SAR_PERSISTENT_LIFETIME) == SAR_PERSISTENT_LIFETIME) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_PERSISTENT_LIFETIME])) {
			lifetime = (atoi(sar_options.params[OPT_ARG_PERSISTENT_LIFETIME]) == 0) ? 
				APP_SESSION_LIFETIME_DYNAMIC : APP_SESSION_LIFETIME_PERSISTENT;
		}
	}
	
	/* Get application datastore. */
	app_session_t *app_session = app_datastore_session_add(datastore, entry);
	if (!app_session) {
		return synthandrecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	datastore->last_recog_entry = entry;
	app_session->nlsml_result = NULL;

	app_session->prompts = apr_array_make(app_session->pool, 1, sizeof(sar_prompt_item_t));
	app_session->it_policy = IT_POLICY_AUTO;
	app_session->lifetime = lifetime;
	app_session->msg_process_dispatcher = &synthandrecog->message_process;

	if(!app_session->recog_channel) {
		/* Get new read format. */
		app_session->nreadformat = ast_channel_get_speechreadformat(chan, app_session->pool);

		/* Get new write format. */
		app_session->nwriteformat = ast_channel_get_speechwriteformat(chan, app_session->pool);

		recog_name = apr_psprintf(app_session->pool, "ASR-%lu", (unsigned long int)app_session->schannel_number);

		/* Create speech channel for recognition. */
		app_session->recog_channel = speech_channel_create(
											app_session->pool,
											recog_name,
											SPEECH_CHANNEL_RECOGNIZER,
											synthandrecog,
											app_session->nreadformat,
											NULL,
											chan,
											NULL);
		if (app_session->recog_channel == NULL) {
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
		app_session->recog_channel->app_session = app_session;

		const char *recog_profile_option = NULL;
		if ((sar_options.flags & SAR_RECOG_PROFILE) == SAR_RECOG_PROFILE) {
			if (!ast_strlen_zero(sar_options.params[OPT_ARG_PROFILE])) {
				recog_profile_option = sar_options.params[OPT_ARG_PROFILE];
			}
		}

		/* Get recognition profile. */
		ast_mrcp_profile_t *recog_profile = get_recog_profile(recog_profile_option);
		if (!recog_profile) {
			ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", recog_name, recog_profile_option);
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		/* Open recognition channel. */
		if (speech_channel_open(app_session->recog_channel, recog_profile) != 0) {
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}
	else {
		recog_name = app_session->recog_channel->name;
	}

	/* Get old read format. */
	ast_format_compat *oreadformat = ast_channel_get_readformat(chan, app_session->pool);
	ast_format_compat *orawreadformat = ast_channel_get_rawreadformat(chan, app_session->pool);

	/* Get old write format. */
	ast_format_compat *owriteformat = ast_channel_get_writeformat(chan, app_session->pool);
	ast_format_compat *orawwriteformat = ast_channel_get_rawwriteformat(chan, app_session->pool);

	/* Set read format. */
	ast_set_read_format_path(chan, orawreadformat, app_session->nreadformat);

	/* Store old read format. */
	app_session->readformat = oreadformat;
	app_session->rawreadformat = orawreadformat;

	/* Set write format. */
	ast_set_write_format_path(chan, app_session->nwriteformat, orawwriteformat);

	/* Store old write format. */
	app_session->writeformat = owriteformat;
	app_session->rawwriteformat = orawwriteformat;

	/* Check if barge-in is allowed. */
	int bargein = 1;
	if ((sar_options.flags & SAR_BARGEIN) == SAR_BARGEIN) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_BARGEIN])) {
			bargein = (atoi(sar_options.params[OPT_ARG_BARGEIN]) == 0) ? 0 : 1;
		}
	}

	/* Check whether or not to always stop barged synthesis request. */
	app_session->stop_barged_synth = FALSE;
	if ((sar_options.flags & SAR_STOP_BARGED_SYNTH) == SAR_STOP_BARGED_SYNTH) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_STOP_BARGED_SYNTH])) {
			app_session->stop_barged_synth = (atoi(sar_options.params[OPT_ARG_STOP_BARGED_SYNTH]) == 0) ? FALSE : TRUE;
		}
	}

	/* Get NLSML instance format, if specified */
	if ((sar_options.flags & SAR_INSTANCE_FORMAT) == SAR_INSTANCE_FORMAT) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_INSTANCE_FORMAT])) {
			const char *format = sar_options.params[OPT_ARG_INSTANCE_FORMAT];
			if (strcasecmp(format, "xml") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_XML;
			else if (strcasecmp(format, "json") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_JSON;
		}
	}

	/* Get grammar delimiters. */
	const char *grammar_delimiters = ",";
	if ((sar_options.flags & SAR_GRAMMAR_DELIMITERS) == SAR_GRAMMAR_DELIMITERS) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_GRAMMAR_DELIMITERS])) {
			grammar_delimiters = sar_options.params[OPT_ARG_GRAMMAR_DELIMITERS];
			ast_log(LOG_DEBUG, "(%s) Grammar delimiters: %s\n", grammar_delimiters, recog_name);
		}
	}
	/* Parse the grammar argument into a sequence of grammars. */
	char *grammar_arg = apr_pstrdup(app_session->pool, args.grammar);
	char *last;
	char *grammar_str;
	char grammar_name[32];
	int grammar_id = 0;
	grammar_str = apr_strtok(grammar_arg, grammar_delimiters, &last);
	while (grammar_str) {
		const char *grammar_content = NULL;
		grammar_type_t grammar_type = GRAMMAR_TYPE_UNKNOWN;
		ast_log(LOG_DEBUG, "(%s) Determine grammar type: %s\n", recog_name, grammar_str);
		if (determine_grammar_type(app_session->recog_channel, grammar_str, &grammar_content, &grammar_type) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to determine grammar type: %s\n", recog_name, grammar_str);
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		apr_snprintf(grammar_name, sizeof(grammar_name) - 1, "grammar-%d", grammar_id++);
		grammar_name[sizeof(grammar_name) - 1] = '\0';
		/* Load grammar. */
		if (recog_channel_load_grammar(app_session->recog_channel, grammar_name, grammar_type, grammar_content) != 0) {
			ast_log(LOG_ERROR, "(%s) Unable to load grammar\n", recog_name);

			const char *completion_cause = NULL;
			channel_get_results(app_session->recog_channel, &completion_cause, NULL, NULL);
			if (completion_cause)
				pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);
			
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		grammar_str = apr_strtok(NULL, grammar_delimiters, &last);
	}

	/* Get output delimiters. */
	const char *output_delimiters = "^";
	if ((sar_options.flags & SAR_OUTPUT_DELIMITERS) == SAR_OUTPUT_DELIMITERS) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_OUTPUT_DELIMITERS])) {
			output_delimiters = sar_options.params[OPT_ARG_OUTPUT_DELIMITERS];
			ast_log(LOG_DEBUG, "(%s) Output delimiters: %s\n", output_delimiters, recog_name);
		}
	}

	/* Parse the prompt argument into a list of prompts. */
	char *prompt_arg = apr_pstrdup(app_session->pool, args.prompt);
	char *prompt_str = apr_strtok(prompt_arg, output_delimiters, &last);
	while (prompt_str) {
		prompt_str = normalize_input_string(prompt_str);
		ast_log(LOG_DEBUG, "(%s) Add prompt: %s\n", recog_name, prompt_str);
		sar_prompt_item_t *prompt_item = apr_array_push(app_session->prompts);

		prompt_item->content = NULL;
		prompt_item->is_audio_file = 0;

		if (determine_prompt_type(prompt_str, &prompt_item->content, &prompt_item->is_audio_file) !=0 ) {
			ast_log(LOG_WARNING, "(%s) Unable to determine prompt type\n", recog_name);
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		prompt_str = apr_strtok(NULL, output_delimiters, &last);
	}

	int prompt_processing = (synthandrecog_prompts_available(app_session)) ? 1 : 0;
	sar_prompt_item_t *prompt_item = NULL;
	int end_of_prompt;

	/* If bargein is not allowed, play all the prompts and wait for for them to complete. */
	if (!bargein && prompt_processing) {
		/* Start playing first prompt. */
		prompt_item = synthandrecog_prompt_play(datastore, app_session, &sar_options);
		if (!prompt_item) {
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		int ms;
		do {
			end_of_prompt = 0;
			if(prompt_item->is_audio_file) {
				if (ast_waitstream(chan, "") != 0) {
					f = ast_read(chan);
					if (!f) {
						ast_log(LOG_DEBUG, "(%s) ast_waitstream failed on %s, channel read is a null frame. Hangup detected\n", recog_name, ast_channel_name(chan));
						return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
					}
					ast_frfree(f);

					ast_log(LOG_WARNING, "(%s) ast_waitstream failed on %s\n", recog_name, ast_channel_name(chan));
					return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
				app_session->filestream = NULL;
				end_of_prompt = 1;
			}
			else {
				ms = ast_waitfor(chan, 100);
				if (ms < 0) {
					ast_log(LOG_DEBUG, "(%s) Hangup detected\n", recog_name);
					return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
				}

				f = ast_read(chan);
				if (!f) {
					ast_log(LOG_DEBUG, "(%s) Null frame. Hangup detected\n", recog_name);
					return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
				}

				ast_frfree(f);

				if (app_session->synth_channel->state != SPEECH_CHANNEL_PROCESSING) {
					end_of_prompt = 1;
				}
			}
			if (end_of_prompt) {
				/* End of current prompt -> advance to the next one. */
				if (synthandrecog_prompts_advance(app_session) > 0) {
					/* Start playing current prompt. */
					prompt_item = synthandrecog_prompt_play(datastore, app_session, &sar_options);
					if (!prompt_item) {
						return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
					}
				}
				else {
					/* End of prompts. */
					break;
				}
			}
		}
		while (synthandrecog_prompts_available(app_session));

		prompt_processing = 0;
	}

	/* Check the policy for input timers. */
	if ((sar_options.flags & SAR_INPUT_TIMERS) == SAR_INPUT_TIMERS) {
		if (!ast_strlen_zero(sar_options.params[OPT_ARG_INPUT_TIMERS])) {
			switch(atoi(sar_options.params[OPT_ARG_INPUT_TIMERS])) {
				case 0: app_session->it_policy = IT_POLICY_OFF; break;
				case 1: app_session->it_policy = IT_POLICY_ON; break;
				default: app_session->it_policy = IT_POLICY_AUTO;
			}
		}
	}

	int start_input_timers = !prompt_processing;
	if (app_session->it_policy != IT_POLICY_AUTO)
		start_input_timers = app_session->it_policy;
	recognizer_data_t *r = app_session->recog_channel->data;

	ast_log(LOG_NOTICE, "(%s) Recognizing, Start-Input-Timers: %d\n", recog_name, start_input_timers);

	/* Start recognition. */
	if (recog_channel_start(app_session->recog_channel, recog_name, start_input_timers, &sar_options) != 0) {
		ast_log(LOG_ERROR, "(%s) Unable to start recognition\n", recog_name);

		const char *completion_cause = NULL;
		channel_get_results(app_session->recog_channel, &completion_cause, NULL, NULL);
		if (completion_cause)
			pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);
		
		return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	if (prompt_processing) {
		/* Start playing first prompt. */
		prompt_item = synthandrecog_prompt_play(datastore, app_session, &sar_options);
		if (!prompt_item) {
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

#if !AST_VERSION_AT_LEAST(11,0,0)
	off_t read_filestep = 0;
	off_t read_filelength;
#endif
	int waitres;
	/* Continue with recognition. */
	while ((waitres = ast_waitfor(chan, 100)) >= 0) {
		int recog_processing = 1;

		if (app_session->recog_channel && app_session->recog_channel->mutex) {
			apr_thread_mutex_lock(app_session->recog_channel->mutex);

			if (app_session->recog_channel->state != SPEECH_CHANNEL_PROCESSING) {
				recog_processing = 0;
			}

			apr_thread_mutex_unlock(app_session->recog_channel->mutex);
		}

		if (recog_processing == 0)
			break;

		if (prompt_processing) {
			end_of_prompt = 0;
			if (prompt_item->is_audio_file) {
				if (app_session->filestream) {
#if AST_VERSION_AT_LEAST(11,0,0)
					if (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL) {
						ast_stopstream(chan);
						ast_log(LOG_DEBUG, "(%s) File is over\n", recog_name);
						end_of_prompt = 1;
						app_session->filestream = NULL;
					}
#else
					read_filelength = ast_tellstream(app_session->filestream);
					if(!read_filestep)
						read_filestep = read_filelength;
					if (read_filelength + read_filestep > app_session->max_filelength) {
						ast_log(LOG_DEBUG, "(%s) File is over, read length:%"APR_OFF_T_FMT"\n", recog_name, read_filelength);
						end_of_prompt = 1;
						app_session->filestream = NULL;
						read_filestep = 0;
					}
#endif
				}
			}
			else {
				if (app_session->synth_channel->state != SPEECH_CHANNEL_PROCESSING) {
					end_of_prompt = 1;
				}
			}

			if (end_of_prompt) {
				/* End of current prompt -> advance to the next one. */
				if (synthandrecog_prompts_advance(app_session) > 0) {
					/* Start playing current prompt. */
					prompt_item = synthandrecog_prompt_play(datastore, app_session, &sar_options);
					if (!prompt_item) {
						return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
					}
				}
				else {
					/* End of prompts -> start input timers. */
					if (app_session->it_policy == IT_POLICY_AUTO) {
						ast_log(LOG_DEBUG, "(%s) Start input timers\n", recog_name);
						channel_start_input_timers(app_session->recog_channel, RECOGNIZER_START_INPUT_TIMERS);
					}
					prompt_processing = 0;
				}
			}

			if (prompt_processing && r && r->start_of_input) {
				ast_log(LOG_DEBUG, "(%s) Bargein occurred\n", recog_name);
				if (prompt_item->is_audio_file) {
					ast_stopstream(chan);
					app_session->filestream = NULL;
				}
				else {
					synth_channel_bargein_occurred(app_session->synth_channel);
				}
				prompt_processing = 0;
			}
		}

		if (waitres == 0)
			continue;

		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_DEBUG, "(%s) Null frame. Hangup detected\n", recog_name);
			status = SPEECH_CHANNEL_STATUS_INTERRUPTED;
			break;
		}

		if (f->frametype == AST_FRAME_VOICE && f->datalen) {
			len = f->datalen;
			if (speech_channel_write(app_session->recog_channel, ast_frame_get_data(f), &len) != 0) {
				ast_frfree(f);
				break;
			}
		} else if (f->frametype == AST_FRAME_VIDEO) {
			/* Ignore. */
		} else if (f->frametype == AST_FRAME_DTMF) {
			int dtmfkey = ast_frame_get_dtmfkey(f);
			ast_log(LOG_DEBUG, "(%s) User pressed DTMF key (%d)\n", recog_name, dtmfkey);
			/* Send DTMF frame to ASR engine. */
			if (app_session->dtmf_generator != NULL) {
				char digits[2];
				digits[0] = (char)dtmfkey;
				digits[1] = '\0';

				ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", app_session->recog_channel->name, digits);
				mpf_dtmf_generator_enqueue(app_session->dtmf_generator, digits);
			}
		}

		ast_frfree(f);
	}

	if (prompt_processing) {
		ast_log(LOG_DEBUG, "(%s) Stop prompt\n", recog_name);
		if (prompt_item->is_audio_file) {
			ast_stopstream(chan);
			app_session->filestream = NULL;
		}
		else {
			/* do nothing, synth channel will be destroyed anyway */
		}
		prompt_processing = 0;
	}

	const char *completion_cause = NULL;
	const char *result = NULL;
	const char *waveform_uri = NULL;

	if (status == SPEECH_CHANNEL_STATUS_OK) {
		int uri_encoded_results = 0;
		/* Check if the results should be URI-encoded. */
		if ((sar_options.flags & SAR_URI_ENCODED_RESULTS) == SAR_URI_ENCODED_RESULTS) {
			if (!ast_strlen_zero(sar_options.params[OPT_ARG_URI_ENCODED_RESULTS])) {
				uri_encoded_results = (atoi(sar_options.params[OPT_ARG_URI_ENCODED_RESULTS]) == 0) ? 0 : 1;
			}
		}

		/* Get recognition result. */
		if (channel_get_results(app_session->recog_channel, &completion_cause, &result, &waveform_uri) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", recog_name);
			return synthandrecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
		
		if (result) {
			/* Store the results for further reference from the dialplan. */
			apr_size_t result_len = strlen(result);
			app_session->nlsml_result = nlsml_result_parse(result, result_len, datastore->pool);

			if (uri_encoded_results != 0) {
				apr_size_t len = result_len * 2;
				char *buf = apr_palloc(app_session->pool, len);
				result = ast_uri_encode_http(result, buf, len);
			}
		}
	}

	/* Completion cause should always be available at this stage. */
	if (completion_cause)
		pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);

	/* Result may not be available if recognition completed with nomatch, noinput, or other error cause. */
	pbx_builtin_setvar_helper(chan, "RECOG_RESULT", result ? result : "");

	/* If Waveform URI is available, pass it further to dialplan. */
	if (waveform_uri)
		pbx_builtin_setvar_helper(chan, "RECOG_WAVEFORM_URI", waveform_uri);

	return synthandrecog_exit(chan, app_session, status);
}

/* Process messages from UniMRCP for the synthandrecog application. */
static apt_bool_t synthandrecog_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message)
		return mrcp_application_message_dispatch(&synthandrecog->dispatcher, app_message);

	ast_log(LOG_ERROR, "(unknown) app_message error!\n");
	return TRUE;
}

/* Load SynthAndRecog application. */
int load_synthandrecog_app()
{
	apr_pool_t *pool = globals.pool;

	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	if(synthandrecog) {
		ast_log(LOG_ERROR, "Application %s is already loaded\n", synthandrecog_name);
		return -1;
	}

	synthandrecog = (ast_mrcp_application_t*) apr_palloc(pool, sizeof(ast_mrcp_application_t));
	synthandrecog->name = synthandrecog_name;
	synthandrecog->exec = app_synthandrecog_exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	synthandrecog->synopsis = NULL;
	synthandrecog->description = NULL;
#endif

	/* Create the recognizer application and link its callbacks */
	if ((synthandrecog->app = mrcp_application_create(synthandrecog_message_handler, (void *)synthandrecog, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create MRCP application %s\n", synthandrecog_name);
		synthandrecog = NULL;
		return -1;
	}

	synthandrecog->dispatcher.on_session_update = NULL;
	synthandrecog->dispatcher.on_session_terminate = speech_on_session_terminate;
	synthandrecog->dispatcher.on_channel_add = speech_on_channel_add;
	synthandrecog->dispatcher.on_channel_remove = NULL;
	synthandrecog->dispatcher.on_message_receive = mrcp_on_message_receive;
	synthandrecog->dispatcher.on_terminate_event = NULL;
	synthandrecog->dispatcher.on_resource_discover = NULL;
	synthandrecog->message_process.recog_message_process = recog_on_message_receive;
	synthandrecog->message_process.synth_message_process = synth_on_message_receive;
	synthandrecog->audio_stream_vtable.destroy = NULL;
	synthandrecog->audio_stream_vtable.open_rx = stream_open;
	synthandrecog->audio_stream_vtable.close_rx = NULL;
	synthandrecog->audio_stream_vtable.read_frame = stream_read;
	synthandrecog->audio_stream_vtable.open_tx = NULL;
	synthandrecog->audio_stream_vtable.close_tx = NULL;
	synthandrecog->audio_stream_vtable.write_frame = synth_stream_write;
	synthandrecog->audio_stream_vtable.trace = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, synthandrecog->app, synthandrecog_name)) {
		ast_log(LOG_ERROR, "Unable to register MRCP application %s\n", synthandrecog_name);
		if (!mrcp_application_destroy(synthandrecog->app))
			ast_log(LOG_WARNING, "Unable to destroy MRCP application %s\n", synthandrecog_name);
		synthandrecog = NULL;
		return -1;
	}

	apr_hash_set(globals.apps, synthandrecog_name, APR_HASH_KEY_STRING, synthandrecog);

	return 0;
}

/* Unload SynthAndRecog application. */
int unload_synthandrecog_app()
{
	if(!synthandrecog) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", synthandrecog_name);
		return -1;
	}

	apr_hash_set(globals.apps, synthandrecog_name, APR_HASH_KEY_STRING, NULL);
	synthandrecog = NULL;

	return 0;
}
