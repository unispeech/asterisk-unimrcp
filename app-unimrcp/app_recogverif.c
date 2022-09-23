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
 * \brief MRCPRecogVerif application
 *
 * \author\verbatim J.W.F. Thirion <derik@molo.co.za> \endverbatim
 * 
 * MRCPRecogVerif application
 * \ingroup applications
 */

/* Asterisk includes. */
#include "ast_compat_defs.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/app.h"

/* UniMRCP includes. */
#include "app_datastore.h"
#include "app_msg_process_dispatcher.h"
#include "app_channel_methods.h"

/*** DOCUMENTATION
	<application name="MRCPRecogVerif" language="en_US">
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
					<option name="vc"> <para>Verificarion score (-1.0 - 1.0).</para> </option>
					<option name="minph"> <para>Minimum verification phrases.</para> </option>
					<option name="maxph"> <para>Maximum verification phrases.</para> </option>
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
					<option name="vm"> <para>Verification mode (verify/enroll).</para> </option>
					<option name="nac"> <para>New audio channel (true/false).</para> </option>
					<option name="spl"> <para>Speech language (e.g. "en-GB", "en-US", "en-AU", etc.).</para> </option>
					<option name="rm"> <para>Recognition mode (normal/hotword).</para> </option>
					<option name="hmaxd"> <para>Hotword max duration (msec).</para> </option>
					<option name="hmind"> <para>Hotword min duration (msec).</para> </option>
					<option name="cdb"> <para>Clear DTMF buffer (true/false).</para> </option>
					<option name="enm"> <para>Early nomatch (true/false).</para> </option>
					<option name="iwu"> <para>Input waveform URI.</para> </option>
					<option name="rpuri"> <para>Repository URI.</para> </option>
					<option name="vpid"> <para>Voiceprint identifier.</para> </option>
					<option name="mt"> <para>Media type.</para> </option>
					<option name="vbu"> <para>Verify Buffer Utterance (true/false).</para> </option>
					<option name="epe"> <para>Exit on play error 
						(1: terminate recognition on file play error, 0: continue even if file play fails).</para>
					</option>
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
					<option name="vsp"> <para>Vendor-specific parameters.</para></option>
					<option name="vsprec"> <para>Vendor-specific parameters for recognition.</para></option>
					<option name="vspver"> <para>Vendor-specific parameters for verify.</para></option>
					<option name="nif"> <para>NLSML instance format (either "xml" or "json") used by RECOG_INSTANCE().</para></option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application establishes an MRCP session for speech recognition and optionally plays a prompt file.
			Once recognition completes, the application exits and returns results to the dialplan.</para>
			<para>If recognition completed, the variable ${RECOG_VERIF_STATUS} is set to "OK". Otherwise, if recognition couldn't be started,
			the variable ${RECOG_VERIF_STATUS} is set to "ERROR". If the caller hung up while recognition was still in-progress,
			the variable ${RECOG_VERIF_STATUS} is set to "INTERRUPTED".</para>
			<para>The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed successfully with a match or
			an error occurred. ("000" - success, "001" - nomatch, "002" - noinput) </para>
			<para>If recognition completed successfully, the variable ${RECOG_RESULT} is set to an NLSML result received
			from the MRCP server. Alternatively, the recognition result data can be retrieved by using the following dialplan
			functions RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().</para>
		</description>
		<see-also>
			<ref type="application">MRCPRecog</ref>
			<ref type="application">MRCPVerif</ref>
		</see-also>
	</application>
 ***/

/* The name of the application. */
static const char *app_recog = "MRCPRecogVerif";

/* The application instance. */
static ast_mrcp_application_t *mrcprecogverif = NULL;

/* The enumeration of plocies for the use of input timers. */
enum mrcprecog_it_policies {
	IT_POLICY_OFF               = 0, /* do not start input timers */
	IT_POLICY_ON                = 1, /* start input timers with RECOGNIZE */
	IT_POLICY_AUTO                   /* start input timers once prompt is finished [default] */
};

/* --- MRCP SPEECH CHANNEL INTERFACE TO UNIMRCP --- */

/* --- MRCP ASR --- */

/* Process messages from UniMRCP for the recognizer application. */
static apt_bool_t recog_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message)
		return mrcp_application_message_dispatch(&mrcprecogverif->dispatcher, app_message);

	ast_log(LOG_ERROR, "(unknown) app_message error!\n");
	return TRUE;
}

/* Apply application options. */
static int mrcprecogverif_option_apply(mrcprecogverif_options_t *options, const char *key, char *value)
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
	} else if (strcasecmp(key, "mt") == 0) {
		apr_hash_set(options->recog_hfs, "Media-Type", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vbu") == 0) {
		apr_hash_set(options->recog_hfs, "Verify-Buffer-Utterance", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vsp") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->rec_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
			apr_hash_set(options->ver_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "vsprec") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->rec_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "vspver") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->ver_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "vc") == 0) {
		apr_hash_set(options->verif_session_hfs, "Min-Verification-Score", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "minph") == 0) {
		apr_hash_set(options->verif_session_hfs, "Num-Min-Verification-Phrases", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "maxph") == 0) {
		apr_hash_set(options->verif_session_hfs, "Num-Max-Verification-Phrases", APR_HASH_KEY_STRING, value);
	}  else if (strcasecmp(key, "vm") == 0) {
		apr_hash_set(options->verif_session_hfs, "Verification-Mode", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "rpuri") == 0) {
		apr_hash_set(options->verif_session_hfs, "Repository-URI", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vpid") == 0) {
		apr_hash_set(options->verif_session_hfs, "Voiceprint-Identifier", APR_HASH_KEY_STRING, value);
	}else if (strcasecmp(key, "p") == 0) {
		options->flags |= MRCPRECOGVERIF_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
	} else if (strcasecmp(key, "i") == 0) {
		options->flags |= MRCPRECOGVERIF_INTERRUPT;
		options->params[OPT_ARG_INTERRUPT] = value;
	} else if (strcasecmp(key, "f") == 0) {
		options->flags |= MRCPRECOGVERIF_FILENAME;
		options->params[OPT_ARG_FILENAME] = value;
	} else if (strcasecmp(key, "t") == 0) {
		apr_hash_set(options->recog_hfs, "Recognition-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "b") == 0) {
		options->flags |= MRCPRECOGVERIF_BARGEIN;
		options->params[OPT_ARG_BARGEIN] = value;
	} else if (strcasecmp(key, "gd") == 0) {
		options->flags |= MRCPRECOGVERIF_GRAMMAR_DELIMITERS;
		options->params[OPT_ARG_GRAMMAR_DELIMITERS] = value;
	} else if (strcasecmp(key, "epe") == 0) {
		options->flags |= MRCPRECOGVERIF_EXIT_ON_PLAYERROR;
		options->params[OPT_ARG_EXIT_ON_PLAYERROR] = value;
	} else if (strcasecmp(key, "uer") == 0) {
		options->flags |= MRCPRECOGVERIF_URI_ENCODED_RESULTS;
		options->params[OPT_ARG_URI_ENCODED_RESULTS] = value;
	} else if (strcasecmp(key, "od") == 0) {
		options->flags |= MRCPRECOGVERIF_OUTPUT_DELIMITERS;
		options->params[OPT_ARG_OUTPUT_DELIMITERS] = value;
	} else if (strcasecmp(key, "sit") == 0) {
		options->flags |= MRCPRECOGVERIF_INPUT_TIMERS;
		options->params[OPT_ARG_INPUT_TIMERS] = value;
	} else if (strcasecmp(key, "plt") == 0) {
		options->flags |= MRCPRECOGVERIF_PERSISTENT_LIFETIME;
		options->params[OPT_ARG_PERSISTENT_LIFETIME] = value;
	} else if (strcasecmp(key, "dse") == 0) {
		options->flags |= MRCPRECOGVERIF_DATASTORE_ENTRY;
		options->params[OPT_ARG_DATASTORE_ENTRY] = value;
	} else if (strcasecmp(key, "nif") == 0) {
		options->flags |= MRCPRECOGVERIF_INSTANCE_FORMAT;
		options->params[OPT_ARG_INSTANCE_FORMAT] = value;
	} else if (strcasecmp(key, "bufh") == 0) {
		options->flags |= MRCPRECOGVERIF_BUF_HND;
		options->params[OPT_ARG_BUF_HND] = value;
	} else {
		ast_log(LOG_WARNING, "Unknown option: %s\n", key);
	}
	return 0;
}

/* Parse application options. */
static int mrcprecogverif_options_parse(char *str, mrcprecogverif_options_t *options, apr_pool_t *pool)
{
	char *s;
	char *name, *value;

	if (!str)
		return 0;

	if ((options->recog_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->verif_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->verif_session_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->rec_vendor_par_list = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->ver_vendor_par_list = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	do {
		/* Skip any leading spaces. */
		while (isspace(*str))
			str++;

		if (*str == '<') {
			/* Special case -> found an option quoted with < > */
			str++;
			s = strsep(&str, ">");
			/* Skip to the next option, if any */
			strsep(&str, "&");
		}
		else {
			/* Regular processing */
			s = strsep(&str, "&");
		}

		if (s) {
			value = s;
			if ((name = strsep(&value, "=")) && value) {
				ast_log(LOG_DEBUG, "Apply option %s: %s\n", name, value);
				mrcprecogverif_option_apply(options, name, value);
			}
		}
	}
	while (str);

	if (!apr_hash_get(options->verif_session_hfs, "Verification-Mode", APR_HASH_KEY_STRING))
		return -1;

	if (!apr_hash_get(options->verif_session_hfs, "Repository-URI", APR_HASH_KEY_STRING))
		return -1;

	if (!apr_hash_get(options->verif_session_hfs, "Voiceprint-Identifier", APR_HASH_KEY_STRING))
		return -1;

	apr_hash_set(options->recog_hfs, "Ver-Buffer-Utterance", APR_HASH_KEY_STRING, "true");

	return 0;
}

/* Return the number of prompts which still need to be played. */
static APR_INLINE int mrcprecog_prompts_available(app_session_t *app_session)
{
	if(app_session->cur_prompt >= app_session->prompts->nelts)
		return 0;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Advance the current prompt index and return the number of prompts remaining. */
static APR_INLINE int mrcprecog_prompts_advance(app_session_t *app_session)
{
	if (app_session->cur_prompt >= app_session->prompts->nelts)
		return -1;
	app_session->cur_prompt++;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Start playing the current prompt. */
static struct ast_filestream* mrcprecog_prompt_play(app_session_t *app_session, mrcprecogverif_options_t *mrcprecogverif_options, off_t *max_filelength)
{
	if (app_session->cur_prompt >= app_session->prompts->nelts) {
		ast_log(LOG_ERROR, "(%s) Out of bounds prompt index\n", app_session->recog_channel->name);
		return NULL;
	}

	char *filename = APR_ARRAY_IDX(app_session->prompts, app_session->cur_prompt, char*);
	if (!filename) {
		ast_log(LOG_ERROR, "(%s) Invalid file name\n", app_session->recog_channel->name);
		return NULL;
	}
	return astchan_stream_file(app_session->recog_channel->chan, filename, max_filelength);
}

/* Exit the application. */
static int mrcprecog_exit(struct ast_channel *chan, app_session_t *app_session, speech_channel_status_t status)
{
	ast_log(LOG_NOTICE, "%s() Will exiting on %s\n", app_recog, ast_channel_name(chan));
	if (app_session) {
		if (app_session->readformat && app_session->rawreadformat)
			ast_set_read_format_path(chan, app_session->rawreadformat, app_session->readformat);

		if (app_session->recog_channel) {
			if (app_session->recog_channel->session_id)
				pbx_builtin_setvar_helper(chan, "RECOG_SID", app_session->recog_channel->session_id);

			if (app_session->lifetime == APP_SESSION_LIFETIME_DYNAMIC) {
				ast_log(LOG_NOTICE, "%s() Will stop recog on %s\n", app_recog, ast_channel_name(chan));
				speech_channel_destroy(app_session->recog_channel);
				app_session->recog_channel = NULL;
			}
		}
		if (app_session->verif_channel) {
			if (app_session->verif_channel->session_id)
				pbx_builtin_setvar_helper(chan, "VERIF_SID", app_session->verif_channel->session_id);

			if (app_session->lifetime == APP_SESSION_LIFETIME_DYNAMIC) {
				ast_log(LOG_NOTICE, "%s() Will stop verif on %s\n", app_recog, ast_channel_name(chan));
				speech_channel_destroy(app_session->verif_channel);
				app_session->verif_channel = NULL;
			}
		}
	}

	const char *status_str = speech_channel_status_to_string(status);
	pbx_builtin_setvar_helper(chan, "RECOG_VERIF_STATUS", status_str);
	ast_log(LOG_NOTICE, "%s() exiting status: %s on %s\n", app_recog, status_str, ast_channel_name(chan));
	return 0;
}

/* The entry point of the application. */
static int app_recog_verif_exec(struct ast_channel *chan, ast_app_data data)
{
	int dtmf_enable;
	struct ast_frame *f = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	const char *name;
	speech_channel_status_t status = SPEECH_CHANNEL_STATUS_OK;
	char *parse;
	int i;
	mrcprecogverif_options_t mrcprecogverif_options;
	const char *profile_name = NULL;
	ast_mrcp_profile_t *profile;

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

	args.grammar = normalize_input_string(args.grammar);
	ast_log(LOG_NOTICE, "%s() grammar: %s\n", app_recog, args.grammar);

	app_datastore_t* datastore = app_datastore_get(chan);
	if (!datastore) {
		ast_log(LOG_ERROR, "Unable to retrieve data from app datastore on %s\n", ast_channel_name(chan));
		return mrcprecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	mrcprecogverif_options.recog_hfs = NULL;
	mrcprecogverif_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		mrcprecogverif_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		args.options = normalize_input_string(args.options);
		ast_log(LOG_NOTICE, "%s() options: %s\n", app_recog, args.options);
		char *options_buf = apr_pstrdup(datastore->pool, args.options);
		mrcprecogverif_options_parse(options_buf, &mrcprecogverif_options, datastore->pool);
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
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_DATASTORE_ENTRY) == MRCPRECOGVERIF_DATASTORE_ENTRY) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_DATASTORE_ENTRY])) {
			entry = mrcprecogverif_options.params[OPT_ARG_DATASTORE_ENTRY];
			lifetime = APP_SESSION_LIFETIME_PERSISTENT;
		}
	}

	/* Check session lifetime. */
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_PERSISTENT_LIFETIME) == MRCPRECOGVERIF_PERSISTENT_LIFETIME) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_PERSISTENT_LIFETIME])) {
			lifetime = (atoi(mrcprecogverif_options.params[OPT_ARG_PERSISTENT_LIFETIME]) == 0) ?
				APP_SESSION_LIFETIME_DYNAMIC : APP_SESSION_LIFETIME_PERSISTENT;
		}
	}

	/* Get application datastore. */
	app_session_t *app_session = app_datastore_session_add(datastore, entry);
	if (!app_session) {
		return mrcprecog_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	datastore->last_recog_entry = entry;
	app_session->nlsml_result = NULL;

	app_session->prompts = apr_array_make(app_session->pool, 1, sizeof(char*));
	app_session->cur_prompt = 0;
	app_session->it_policy = IT_POLICY_AUTO;
	app_session->lifetime = lifetime;
	app_session->msg_process_dispatcher = &mrcprecogverif->message_process;

	if(!app_session->recog_channel) {
		/* Get new read format. */
		app_session->nreadformat = ast_channel_get_speechreadformat(chan, app_session->pool);

		name = apr_psprintf(app_session->pool, "ASR-%lu", (unsigned long int)speech_channel_number);

		/* Create speech channel for recognition. */
		app_session->recog_channel = speech_channel_create(
										app_session->pool,
										name,
										SPEECH_CHANNEL_RECOGNIZER,
										mrcprecogverif,
										app_session->nreadformat,
										NULL,
										chan,
										app_session->synth_channel ? app_session->synth_channel->session : NULL);
		if (!app_session->recog_channel) {
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
		app_session->recog_channel->app_session = app_session;

		if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_PROFILE) == MRCPRECOGVERIF_PROFILE) {
			if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_PROFILE])) {
				profile_name = mrcprecogverif_options.params[OPT_ARG_PROFILE];
			}
		}

		/* Get recognition profile. */
		profile = get_recog_profile(profile_name);
		if (!profile) {
			ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", name, profile_name);
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		/* Open recognition channel. */
		if (speech_channel_open(app_session->recog_channel, profile) != 0) {
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}
	else {
		name = app_session->recog_channel->name;
	}

	/* Get old read format. */
	ast_format_compat *oreadformat = ast_channel_get_readformat(chan, app_session->pool);
	ast_format_compat *orawreadformat = ast_channel_get_rawreadformat(chan, app_session->pool);

	/* Set read format. */
	ast_set_read_format_path(chan, orawreadformat, app_session->nreadformat);

	/* Store old read format. */
	app_session->readformat = oreadformat;
	app_session->rawreadformat = orawreadformat;

	/* Check if barge-in is allowed. */
	int bargein = 1;
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_BARGEIN) == MRCPRECOGVERIF_BARGEIN) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_BARGEIN])) {
			bargein = (atoi(mrcprecogverif_options.params[OPT_ARG_BARGEIN]) == 0) ? 0 : 1;
		}
	}

	dtmf_enable = 2;
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_INTERRUPT) == MRCPRECOGVERIF_INTERRUPT) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_INTERRUPT])) {
			dtmf_enable = 1;
			if (strcasecmp(mrcprecogverif_options.params[OPT_ARG_INTERRUPT], "any") == 0)
				mrcprecogverif_options.params[OPT_ARG_INTERRUPT] = AST_DIGIT_ANY;
			else if (strcasecmp(mrcprecogverif_options.params[OPT_ARG_INTERRUPT], "none") == 0)
				dtmf_enable = 2;
			else if (strcasecmp(mrcprecogverif_options.params[OPT_ARG_INTERRUPT], "disable") == 0)
				dtmf_enable = 0;
		}
	}

	/* Get NLSML instance format, if specified */
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_INSTANCE_FORMAT) == MRCPRECOGVERIF_INSTANCE_FORMAT) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_INSTANCE_FORMAT])) {
			const char *format = mrcprecogverif_options.params[OPT_ARG_INSTANCE_FORMAT];
			if (strcasecmp(format, "xml") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_XML;
			else if (strcasecmp(format, "json") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_JSON;
		}
	}

	const char *grammar_delimiters = ",";
	/* Get grammar delimiters. */
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_GRAMMAR_DELIMITERS) == MRCPRECOGVERIF_GRAMMAR_DELIMITERS) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_GRAMMAR_DELIMITERS])) {
			grammar_delimiters = mrcprecogverif_options.params[OPT_ARG_GRAMMAR_DELIMITERS];
			ast_log(LOG_DEBUG, "(%s) Grammar delimiters: %s\n", name, grammar_delimiters);
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
		ast_log(LOG_DEBUG, "(%s) Determine grammar type: %s\n", name, grammar_str);
		if (determine_grammar_type(app_session->recog_channel, grammar_str, &grammar_content, &grammar_type) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to determine grammar type: %s\n", name, grammar_str);
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		apr_snprintf(grammar_name, sizeof(grammar_name) - 1, "grammar-%d", grammar_id++);
		grammar_name[sizeof(grammar_name) - 1] = '\0';
		/* Load grammar. */
		if (recog_channel_load_grammar(app_session->recog_channel, grammar_name, grammar_type, grammar_content) != 0) {
			ast_log(LOG_ERROR, "(%s) Unable to load grammar\n", name);

			const char *completion_cause = NULL;
			channel_get_results(app_session->recog_channel, &completion_cause, NULL, NULL);
			if (completion_cause)
				pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);

			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		grammar_str = apr_strtok(NULL, grammar_delimiters, &last);
	}

	const char *filenames = NULL;
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_FILENAME) == MRCPRECOGVERIF_FILENAME) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_FILENAME])) {
			filenames = mrcprecogverif_options.params[OPT_ARG_FILENAME];
		}
	}

	if (filenames) {
		/* Get output delimiters. */
		const char *output_delimiters = "^";
		if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_OUTPUT_DELIMITERS) == MRCPRECOGVERIF_OUTPUT_DELIMITERS) {
			if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_OUTPUT_DELIMITERS])) {
				output_delimiters = mrcprecogverif_options.params[OPT_ARG_OUTPUT_DELIMITERS];
				ast_log(LOG_DEBUG, "(%s) Output delimiters: %s\n", output_delimiters, name);
			}
		}

		/* Parse the file names into a list of files. */
		char *last;
		char *filenames_arg = apr_pstrdup(app_session->pool, filenames);
		char *filename = apr_strtok(filenames_arg, output_delimiters, &last);
		while (filename) {
			filename = normalize_input_string(filename);
			ast_log(LOG_DEBUG, "(%s) Add prompt: %s\n", name, filename);
			APR_ARRAY_PUSH(app_session->prompts, char*) = filename;

			filename = apr_strtok(NULL, output_delimiters, &last);
		}
	}

	int exit_on_playerror = 0;
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_EXIT_ON_PLAYERROR) == MRCPRECOGVERIF_EXIT_ON_PLAYERROR) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_EXIT_ON_PLAYERROR])) {
			exit_on_playerror = atoi(mrcprecogverif_options.params[OPT_ARG_EXIT_ON_PLAYERROR]);
			if ((exit_on_playerror < 0) || (exit_on_playerror > 2))
				exit_on_playerror = 1;
		}
	}

	int prompt_processing = (mrcprecog_prompts_available(app_session)) ? 1 : 0;
	struct ast_filestream *filestream = NULL;
	off_t max_filelength;

	/* If bargein is not allowed, play all the prompts and wait for for them to complete. */
	if (!bargein && prompt_processing) {
		/* Start playing first prompt. */
		filestream = mrcprecog_prompt_play(app_session, &mrcprecogverif_options, &max_filelength);
		if (!filestream && exit_on_playerror) {
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		do {
			if (filestream) {
				if (ast_waitstream(chan, "") != 0) {
					f = ast_read(chan);
					if (!f) {
						ast_log(LOG_DEBUG, "(%s) ast_waitstream failed on %s, channel read is a null frame. Hangup detected\n", name, ast_channel_name(chan));
						return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
					}
					ast_frfree(f);

					ast_log(LOG_WARNING, "(%s) ast_waitstream failed on %s\n", name, ast_channel_name(chan));
					return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
				filestream = NULL;
			}

			/* End of current prompt -> advance to the next one. */
			if (mrcprecog_prompts_advance(app_session) > 0) {
				/* Start playing current prompt. */
				filestream = mrcprecog_prompt_play(app_session, &mrcprecogverif_options, &max_filelength);
				if (!filestream && exit_on_playerror) {
					return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
			}
			else {
				/* End of prompts. */
				break;
			}
		}
		while (mrcprecog_prompts_available(app_session));

		prompt_processing = 0;
	}

	/* Check the policy for input timers. */
	if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_INPUT_TIMERS) == MRCPRECOGVERIF_INPUT_TIMERS) {
		if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_INPUT_TIMERS])) {
			switch(atoi(mrcprecogverif_options.params[OPT_ARG_INPUT_TIMERS])) {
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

	ast_log(LOG_NOTICE, "(%s) Recognizing, enable DTMFs: %d, start input timers: %d\n", name, dtmf_enable, start_input_timers);

	/* Start recognition. */
	if (recog_channel_start(app_session->recog_channel, name, start_input_timers, &mrcprecogverif_options) != 0) {
		ast_log(LOG_ERROR, "(%s) Unable to start recognition\n", name);

		const char *completion_cause = NULL;
		channel_get_results(app_session->recog_channel, &completion_cause, NULL, NULL);
		if (completion_cause)
			pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);

		return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	if (prompt_processing) {
		/* Start playing first prompt. */
		filestream = mrcprecog_prompt_play(app_session, &mrcprecogverif_options, &max_filelength);
		if (!filestream && exit_on_playerror) {
			ast_log(LOG_ERROR, " Error on prompt processing\n");
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

#if !AST_VERSION_AT_LEAST(11,0,0)
	off_t read_filestep = 0;
	off_t read_filelength;
#endif
	int waitres;
	int recog_processing;
	/* Continue with recognition. */
	while ((waitres = ast_waitfor(chan, 100)) >= 0) {
		recog_processing = 1;

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
			if (filestream) {
#if AST_VERSION_AT_LEAST(11,0,0)
				if (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL) {
					ast_stopstream(chan);
					filestream = NULL;
				}
#else
				read_filelength = ast_tellstream(filestream);
				if(!read_filestep)
					read_filestep = read_filelength;
				if (read_filelength + read_filestep > max_filelength) {
					ast_log(LOG_DEBUG, "(%s) File is over, read length:%"APR_OFF_T_FMT"\n", name, read_filelength);
					filestream = NULL;
					read_filestep = 0;
				}
#endif
			}

			if (!filestream) {
				/* End of current prompt -> advance to the next one. */
				if (mrcprecog_prompts_advance(app_session) > 0) {
					/* Start playing current prompt. */
					filestream = mrcprecog_prompt_play(app_session, &mrcprecogverif_options, &max_filelength);
					if (!filestream && exit_on_playerror) {
						ast_log(LOG_ERROR, " Error on filestream processing\n");
						return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
					}
				}
				else {
					/* End of prompts -> start input timers. */
					if (app_session->it_policy == IT_POLICY_AUTO) {
						ast_log(LOG_DEBUG, "(%s) Start input timers\n", name);
						channel_start_input_timers(app_session->recog_channel, RECOGNIZER_START_INPUT_TIMERS);
					}
					prompt_processing = 0;
				}
			}

			if (prompt_processing && r && r->start_of_input) {
				ast_log(LOG_DEBUG, "(%s) Bargein occurred\n", name);
				ast_stopstream(chan);
				filestream = NULL;
				prompt_processing = 0;
			}
		}

		if (waitres == 0)
			continue;

		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_DEBUG, "(%s) Null frame. Hangup detected\n", name);
			status = SPEECH_CHANNEL_STATUS_INTERRUPTED;
			break;
		}

		if (f->frametype == AST_FRAME_VOICE && f->datalen) {
			apr_size_t len = f->datalen;
			if (speech_channel_write(app_session->recog_channel, ast_frame_get_data(f), &len) != 0) {
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
				if (app_session->dtmf_generator != NULL) {
					char digits[2];
					digits[0] = (char)dtmfkey;
					digits[1] = '\0';

					ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", app_session->recog_channel->name, digits);
					mpf_dtmf_generator_enqueue(app_session->dtmf_generator, digits);
				}
			} else if (dtmf_enable == 1) {
				/* Stop streaming if within i chars. */
				if (strchr(mrcprecogverif_options.params[OPT_ARG_INTERRUPT], dtmfkey) || (strcmp(mrcprecogverif_options.params[OPT_ARG_INTERRUPT],"any"))) {
					ast_frfree(f);
					ast_log(LOG_ERROR, " Error on dtmf processing\n");
					mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_OK);
					return dtmfkey;
				}

				/* Continue if not an i-key. */
			}
		}

		ast_frfree(f);
	}

	if (prompt_processing) {
		ast_log(LOG_DEBUG, "(%s) Stop prompt\n", name);
		ast_stopstream(chan);
		filestream = NULL;
		prompt_processing = 0;
	}

	const char *completion_cause = NULL;
	const char *result = NULL;
	const char *waveform_uri = NULL;

	if (status == SPEECH_CHANNEL_STATUS_OK) {
		int uri_encoded_results = 0;
		/* Check if the results should be URI-encoded. */
		if ((mrcprecogverif_options.flags & MRCPRECOGVERIF_URI_ENCODED_RESULTS) == MRCPRECOGVERIF_URI_ENCODED_RESULTS) {
			if (!ast_strlen_zero(mrcprecogverif_options.params[OPT_ARG_URI_ENCODED_RESULTS])) {
				uri_encoded_results = (atoi(mrcprecogverif_options.params[OPT_ARG_URI_ENCODED_RESULTS]) == 0) ? 0 : 1;
			}
		}

		/* Get recognition result. */
		if (channel_get_results(app_session->recog_channel, &completion_cause, &result, &waveform_uri) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", name);
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
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

	ast_log(LOG_NOTICE, " Will start Verification processing\n");
	/* Create speech channel for Verification. */
	app_session->verif_channel = speech_channel_create(
									app_session->pool,
									name,
									SPEECH_CHANNEL_VERIFIER,
									mrcprecogverif,
									app_session->nreadformat,
									NULL,
									chan,
									app_session->recog_channel ? app_session->recog_channel->session : NULL);
	if (!app_session->verif_channel) {
		return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	app_session->verif_channel->app_session = app_session;
	if (speech_channel_open(app_session->verif_channel, profile) != 0) {
			ast_log(LOG_ERROR, " Error on Verification processing\n");
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}
	/* Start Verification. */
	if (verif_channel_start(app_session->verif_channel, name, start_input_timers, &mrcprecogverif_options) != 0) {
		ast_log(LOG_ERROR, "(%s) Unable to start verification\n", name);

		const char *completion_cause = NULL;
		channel_get_results(app_session->recog_channel, &completion_cause, NULL, NULL);
		if (completion_cause)
			pbx_builtin_setvar_helper(chan, "RECOG_COMPLETION_CAUSE", completion_cause);

		return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}
	/* Continue with verification. */
	while ((waitres = ast_waitfor(chan, 100)) >= 0) {
		recog_processing = 1;

		if (app_session->verif_channel && app_session->verif_channel->mutex) {
			apr_thread_mutex_lock(app_session->verif_channel->mutex);

			if (app_session->verif_channel->state != SPEECH_CHANNEL_PROCESSING) {
				recog_processing = 0;
			}

			apr_thread_mutex_unlock(app_session->verif_channel->mutex);
		}

		if (recog_processing == 0)
			break;
	}


	apt_bool_t has_result = !(mrcprecogverif_options.flags & MRCPRECOGVERIF_BUF_HND)
				|| !strncmp("verify", mrcprecogverif_options.params[OPT_ARG_BUF_HND], 6);
	ast_log(LOG_NOTICE, " The result is %s\n", has_result ? "available" : "unavailable");

	if (has_result) {

		/* Get Verification result. */
		if (has_result && channel_get_results(app_session->verif_channel, &completion_cause, &result, &waveform_uri) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", name);
				return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		if (result) {
			int uri_encoded_results = 0;
			/* Store the results for further reference from the dialplan. */
			apr_size_t result_len = strlen(result);
			//app_session->nlsml_verif_result = nlsml_verification_result_parse(result, result_len, datastore->pool);

			if (uri_encoded_results != 0) {
				apr_size_t len = result_len * 2;
				char *buf = apr_palloc(app_session->pool, len);
				result = ast_uri_encode_http(result, buf, len);
			}
		}
	} else {
		if (channel_get_completion_cause(app_session->verif_channel, &completion_cause) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", name);
			return mrcprecog_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}


	/* Completion cause should always be available at this stage. */
	if (completion_cause)
		pbx_builtin_setvar_helper(chan, "VERIF_COMPLETION_CAUSE", completion_cause);

	/* Result may not be available if recognition completed with nomatch, noinput, or other error cause. */
	pbx_builtin_setvar_helper(chan, "VERIF_RESULT", result ? result : "");

	return mrcprecog_exit(chan, app_session, status);
}

/* Load MRCPRecogVerif application. */
int load_mrcprecogverif_app()
{
	apr_pool_t *pool = globals.pool;

	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	if(mrcprecogverif) {
		ast_log(LOG_ERROR, "Application %s is already loaded\n", app_recog);
		return -1;
	}

	mrcprecogverif = (ast_mrcp_application_t*) apr_palloc(pool, sizeof(ast_mrcp_application_t));
	mrcprecogverif->name = app_recog;
	mrcprecogverif->exec = app_recog_verif_exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	mrcprecogverif->synopsis = NULL;
	mrcprecogverif->description = NULL;
#endif

	/* Create the recognizer application and link its callbacks */
	if ((mrcprecogverif->app = mrcp_application_create(recog_message_handler, (void *)mrcprecogverif, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create recognizer MRCP application %s\n", app_recog);
		mrcprecogverif = NULL;
		return -1;
	}

	mrcprecogverif->dispatcher.on_session_update = NULL;
	mrcprecogverif->dispatcher.on_session_terminate = speech_on_session_terminate;
	mrcprecogverif->dispatcher.on_channel_add = speech_on_channel_add;
	mrcprecogverif->dispatcher.on_channel_remove = NULL;
	mrcprecogverif->dispatcher.on_message_receive = mrcp_on_message_receive;
	mrcprecogverif->dispatcher.on_terminate_event = NULL;
	mrcprecogverif->dispatcher.on_resource_discover = NULL;
	mrcprecogverif->message_process.recog_message_process = recog_on_message_receive;
	mrcprecogverif->message_process.verif_message_process = verif_on_message_receive;
	mrcprecogverif->audio_stream_vtable.destroy = NULL;
	mrcprecogverif->audio_stream_vtable.open_rx = stream_open;
	mrcprecogverif->audio_stream_vtable.close_rx = NULL;
	mrcprecogverif->audio_stream_vtable.read_frame = stream_read;
	mrcprecogverif->audio_stream_vtable.open_tx = NULL;
	mrcprecogverif->audio_stream_vtable.close_tx = NULL;
	mrcprecogverif->audio_stream_vtable.write_frame = NULL;
	mrcprecogverif->audio_stream_vtable.trace = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, mrcprecogverif->app, app_recog)) {
		ast_log(LOG_ERROR, "Unable to register recognizer MRCP application %s\n", app_recog);
		if (!mrcp_application_destroy(mrcprecogverif->app))
			ast_log(LOG_WARNING, "Unable to destroy recognizer MRCP application %s\n", app_recog);
		mrcprecogverif = NULL;
		return -1;
	}

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, mrcprecogverif);

	return 0;
}

/* Unload MRCPRecogVerif application. */
int unload_mrcprecogverif_app()
{
	if(!mrcprecogverif) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", app_recog);
		return -1;
	}

	apr_hash_set(globals.apps, app_recog, APR_HASH_KEY_STRING, NULL);
	mrcprecogverif = NULL;

	return 0;
}
