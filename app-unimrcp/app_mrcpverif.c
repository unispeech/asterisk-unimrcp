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
 * \brief MRCPVerif application
 *
 * \author\verbatim F.Z.C Zaruch Chinasso <fzaruch@cpqd.com.br> \endverbatim
 * 
 * MRCPVerif application
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
#include "mrcp_client_session.h"

/*** DOCUMENTATION
	<application name="MRCPVerif" language="en_US">
		<synopsis>
			MRCP verification application.
		</synopsis>
		<syntax>
			<parameter name="options" required="false">
				<optionlist>
					<option name="p"> <para>Profile to use in mrcp.conf.</para> </option>
					<option name="i"> <para>Digits to allow recognition to be interrupted with
						(set to "none" for DTMF grammars to allow DTMFs to be sent to the MRCP server;
						otherwise, if "any" or other digits specified, recognition will be interrupted
						and the digit will be returned to dialplan).</para>
					</option>
					<option name="f"> <para>Filename to play (if empty or not specified, no file is played).</para> </option>
					<option name="b"> <para>Bargein value (0: no barge-in, 1: enable barge-in).</para> </option>
					<option name="vc"> <para>Verificarion score (-1.0 - 1.0).</para> </option>
					<option name="minph"> <para>Minimum verification phrases.</para> </option>
					<option name="maxph"> <para>Maximum verification phrases.</para> </option>
					<option name="nit"> <para>No input timeout (msec).</para> </option>
					<option name="sct"> <para>Speech complete timeout (msec).</para> </option>
					<option name="dit"> <para>DTMF interdigit timeout (msec).</para> </option>
					<option name="dtt"> <para>DTMF terminate timeout (msec).</para> </option>
					<option name="dttc"> <para>DTMF terminate characters.</para> </option>
					<option name="sw"> <para>Save waveform (true/false).</para> </option>
					<option name="vm"> <para>Verification mode (verify/enroll).</para> </option>
					<option name="enm"> <para>Early nomatch (true/false).</para> </option>
					<option name="iwu"> <para>Input waveform URI.</para> </option>
					<option name="rpuri"> <para>Repository URI.</para> </option>
					<option name="vpid"> <para>Voiceprint identifier.</para> </option>
					<option name="mt"> <para>Media type.</para> </option>
					<option name="iwu"> <para>Input waveform URI.</para> </option>
					<option name="vbu"> <para>Verify Buffer Utterance (true/false).</para> </option>
					<option name="bufh"> <para> Control buffer handling (
						verify: Perform a verify from audio buffer,
						clear: Perform a buffer clear and
						rollback: Perform a buffer rollback).</para>
					</option>
					<option name="uer"> <para>URI-encoded results 
						(1: URI-encode NLMSL results, 0: do not encode).</para>
					</option>
					<option name="sit"> <para>Start input timers value (0: no, 1: yes [start with RECOGNIZE], 
						2: auto [start when prompt is finished]).</para>
					</option>
					<option name="vsp"> <para>Vendor-specific parameters.</para></option>
					<option name="nif"> <para>NLSML instance format (either "xml" or "json") used by RECOG_INSTANCE().</para></option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application establishes an MRCP session for speak verification and optionally plays a prompt file.
			Once recognition completes, the application exits and returns results to the dialplan.</para>
			<para>If recognition completed, the variable ${VERIFSTATUS} is set to "OK". Otherwise, if recognition couldn't be started,
			the variable ${VERIFSTATUS} is set to "ERROR". If the caller hung up while recognition was still in-progress,
			the variable ${VERIFSTATUS} is set to "INTERRUPTED".</para>
			<para>The variable ${VERIF_COMPLETION_CAUSE} indicates whether recognition completed successfully with a match or
			an error occurred. ("000" - success, "001" - nomatch, "002" - noinput) </para>
			<para>If recognition completed successfully, the variable ${VERIF_RESULT} is set to an NLSML result received
			from the MRCP server.</para>
		</description>
		<see-also>
			<ref type="application">MRCPRecogVerif</ref>
		</see-also>
	</application>
 ***/

/* The name of the application. */
static const char *app_verif = "MRCPVerif";

/* The application instance. */
static ast_mrcp_application_t *mrcpverif = NULL;

/* The enumeration of policies for the use of input timers. */
enum mrcpverif_it_policies {
	IT_POLICY_OFF               = 0, /* do not start input timers */
	IT_POLICY_ON                = 1, /* start input timers with RECOGNIZE */
	IT_POLICY_AUTO                   /* start input timers once prompt is finished [default] */
};

/* --- MRCP SPEECH CHANNEL INTERFACE TO UNIMRCP --- */

/* --- MRCP ASR --- */

/* Process messages from UniMRCP for the verifier application. */
static apt_bool_t verif_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message)
		return mrcp_application_message_dispatch(&mrcpverif->dispatcher, app_message);

	ast_log(LOG_ERROR, "(unknown) app_message error!\n");
	return TRUE;
}

/* Apply application options. */
static int mrcpverif_option_apply(mrcprecogverif_options_t *options, const char *key, char *value)
{
	char *vendor_name, *vendor_value;
	if (strcasecmp(key, "vc") == 0) {
		apr_hash_set(options->verif_session_hfs, "Min-Verification-Score", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "minph") == 0) {
		apr_hash_set(options->verif_session_hfs, "Num-Min-Verification-Phrases", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "maxph") == 0) {
		apr_hash_set(options->verif_session_hfs, "Num-Max-Verification-Phrases", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "nit") == 0) {
		apr_hash_set(options->verif_hfs, "No-Input-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "sct") == 0) {
		apr_hash_set(options->verif_hfs, "Speech-Complete-Timeout", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vbu") == 0) {
		apr_hash_set(options->verif_hfs, "Ver-Buffer-Utterance", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vm") == 0) {
		apr_hash_set(options->verif_session_hfs, "Verification-Mode", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "rpuri") == 0) {
		apr_hash_set(options->verif_session_hfs, "Repository-URI", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vpid") == 0) {
		apr_hash_set(options->verif_session_hfs, "Voiceprint-Identifier", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "iwu") == 0) {
		apr_hash_set(options->verif_hfs, "Input-Waveform-URI", APR_HASH_KEY_STRING, value);
	} else if (strcasecmp(key, "vsp") == 0) {
		vendor_value = value;
		if ((vendor_name = strsep(&vendor_value, "=")) && vendor_value) {
			apr_hash_set(options->ver_vendor_par_list, vendor_name, APR_HASH_KEY_STRING, vendor_value);
		}
	} else if (strcasecmp(key, "p") == 0) {
		options->flags |= MRCPRECOGVERIF_PROFILE;
		options->params[OPT_ARG_PROFILE] = value;
	} else if (strcasecmp(key, "i") == 0) {
		options->flags |= MRCPRECOGVERIF_INTERRUPT;
		options->params[OPT_ARG_INTERRUPT] = value;
	} else if (strcasecmp(key, "f") == 0) {
		options->flags |= MRCPRECOGVERIF_FILENAME;
		options->params[OPT_ARG_FILENAME] = value;
	} else if (strcasecmp(key, "b") == 0) {
		options->flags |= MRCPRECOGVERIF_BARGEIN;
		options->params[OPT_ARG_BARGEIN] = value;
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
static int mrcpverif_options_parse(char *str, mrcprecogverif_options_t *options, apr_pool_t *pool)
{
	char *s;
	char *name, *value;

	if (!str)
		return 0;

	if ((options->verif_hfs = apr_hash_make(pool)) == NULL) {
		return -1;
	}

	if ((options->verif_session_hfs = apr_hash_make(pool)) == NULL) {
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
				mrcpverif_option_apply(options, name, value);
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

	return 0;
}

/* Return the number of prompts which still need to be played. */
static APR_INLINE int mrcpverif_prompts_available(app_session_t *app_session)
{
	if(app_session->cur_prompt >= app_session->prompts->nelts)
		return 0;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Advance the current prompt index and return the number of prompts remaining. */
static APR_INLINE int mrcpverif_prompts_advance(app_session_t *app_session)
{
	if (app_session->cur_prompt >= app_session->prompts->nelts)
		return -1;
	app_session->cur_prompt++;
	return app_session->prompts->nelts - app_session->cur_prompt;
}

/* Start playing the current prompt. */
static struct ast_filestream* mrcpverif_prompt_play(app_session_t *app_session, mrcprecogverif_options_t *mrcpverif_options, off_t *max_filelength)
{
	if (app_session->cur_prompt >= app_session->prompts->nelts) {
		ast_log(LOG_ERROR, "(%s) Out of bounds prompt index\n", app_session->verif_channel->name);
		return NULL;
	}

	char *filename = APR_ARRAY_IDX(app_session->prompts, app_session->cur_prompt, char*);
	if (!filename) {
		ast_log(LOG_ERROR, "(%s) Invalid file name\n", app_session->verif_channel->name);
		return NULL;
	}
	return astchan_stream_file(app_session->verif_channel->chan, filename, max_filelength);
}

/* Exit the application. */
static int mrcpverif_exit(struct ast_channel *chan, app_session_t *app_session, speech_channel_status_t status)
{
	if (app_session) {
		if (app_session->readformat && app_session->rawreadformat)
			ast_set_read_format_path(chan, app_session->rawreadformat, app_session->readformat);

		if (app_session->verif_channel) {
			if (app_session->verif_channel->session_id)
				pbx_builtin_setvar_helper(chan, "VERIF_SID", app_session->verif_channel->session_id);

			if (app_session->lifetime == APP_SESSION_LIFETIME_DYNAMIC) {
				speech_channel_destroy(app_session->verif_channel);
				app_session->verif_channel = NULL;
			}
		}
	}

	const char *status_str = speech_channel_status_to_string(status);
	pbx_builtin_setvar_helper(chan, "VERIFSTATUS", status_str);
	ast_log(LOG_NOTICE, "%s() exiting status: %s on %s\n", app_verif, status_str, ast_channel_name(chan));
	return 0;
}

/* The entry point of the application. */
static int app_verif_exec(struct ast_channel *chan, ast_app_data data)
{
	int dtmf_enable;
	struct ast_frame *f = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	const char *name;
	speech_channel_status_t status = SPEECH_CHANNEL_STATUS_OK;
	char *parse;
	int i;
	mrcprecogverif_options_t mrcpverif_options;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
	);

	ast_log(LOG_NOTICE, "%s() Executing Verification for channel: %s\n", app_verif, ast_channel_name(chan));

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s() requires options\n", app_verif);
		return mrcpverif_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	app_datastore_t* datastore = app_datastore_get(chan);
	if (!datastore) {
		ast_log(LOG_ERROR, "Unable to retrieve data from app datastore on %s\n", ast_channel_name(chan));
		return mrcpverif_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	mrcpverif_options.verif_hfs = NULL;
	mrcpverif_options.verif_session_hfs = NULL;
	mrcpverif_options.flags = 0;
	for (i=0; i<OPT_ARG_ARRAY_SIZE; i++)
		mrcpverif_options.params[i] = NULL;

	if (!ast_strlen_zero(args.options)) {
		args.options = normalize_input_string(args.options);
		ast_log(LOG_NOTICE, "%s() options: %s\n", app_verif, args.options);
		char *options_buf = apr_pstrdup(datastore->pool, args.options);
		if (mrcpverif_options_parse(options_buf, &mrcpverif_options, datastore->pool) < 0) {
			ast_log(LOG_ERROR, "%s() Missing mandatory options: Rpository URI, Voiceprint and Verification mode\n", app_verif);
			return mrcpverif_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

	/* Answer if it's not already going. */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	/* Ensure no streams are currently playing. */
	ast_stopstream(chan);

	/* Set default lifetime to dynamic. */
	int lifetime = APP_SESSION_LIFETIME_DYNAMIC;

	/* Get datastore entry. */
	const char *entry = ast_channel_name(chan);
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_DATASTORE_ENTRY) == MRCPRECOGVERIF_DATASTORE_ENTRY) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_DATASTORE_ENTRY])) {
			entry = mrcpverif_options.params[OPT_ARG_DATASTORE_ENTRY];
			lifetime = APP_SESSION_LIFETIME_PERSISTENT;
		}
	}

	/* Check session lifetime. */
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_PERSISTENT_LIFETIME) == MRCPRECOGVERIF_PERSISTENT_LIFETIME) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_PERSISTENT_LIFETIME])) {
			lifetime = (atoi(mrcpverif_options.params[OPT_ARG_PERSISTENT_LIFETIME]) == 0) ?
				APP_SESSION_LIFETIME_DYNAMIC : APP_SESSION_LIFETIME_PERSISTENT;
		}
	}

	/* Get application datastore. */
	ast_log(LOG_NOTICE, "%s() Using datastore entry: %s\n", app_verif, entry);
	app_session_t *app_session = app_datastore_session_add(datastore, entry);
	if (!app_session) {
		return mrcpverif_exit(chan, NULL, SPEECH_CHANNEL_STATUS_ERROR);
	}

	datastore->last_recog_entry = entry;
	app_session->nlsml_result = NULL;

	app_session->prompts = apr_array_make(app_session->pool, 1, sizeof(char*));
	app_session->cur_prompt = 0;
	app_session->it_policy = IT_POLICY_AUTO;
	app_session->lifetime = lifetime;

	if(!app_session->verif_channel) {
		/* Get new read format. */
		app_session->nreadformat = ast_channel_get_speechreadformat(chan, app_session->pool);

		name = apr_psprintf(app_session->pool, "VER-%lu", (unsigned long int)speech_channel_number);

		/* Create speech channel for recognition. */
		app_session->verif_channel = speech_channel_create(
										app_session->pool,
										name,
										SPEECH_CHANNEL_VERIFIER,
										mrcpverif,
										app_session->nreadformat,
										NULL,
										chan,
										app_session->recog_channel ? app_session->recog_channel->session : NULL);
		if (!app_session->verif_channel) {
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
		app_session->verif_channel->app_session = app_session;

		const char *profile_name = NULL;
		if ((mrcpverif_options.flags & MRCPRECOGVERIF_PROFILE) == MRCPRECOGVERIF_PROFILE) {
			if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_PROFILE])) {
				profile_name = mrcpverif_options.params[OPT_ARG_PROFILE];
			}
		}

		/* Get recognition profile. */
		ast_mrcp_profile_t *profile = get_recog_profile(profile_name);
		if (!profile) {
			ast_log(LOG_ERROR, "(%s) Can't find profile, %s\n", name, profile_name);
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
		if (app_session->recog_channel) {
			if (app_session->msg_process_dispatcher) {
				app_session->msg_process_dispatcher->verif_message_process = mrcpverif->message_process.verif_message_process;
				mrcpverif->message_process.recog_message_process = app_session->msg_process_dispatcher->recog_message_process;
			}
			const char* ch_id = apt_string_buffer_get(&app_session->recog_channel->session->unimrcp_session->id);
			ast_log(LOG_NOTICE, "(%s) Using CHANNEL ID, %s\n", app_verif, ch_id);
		}
		/* Open recognition channel. */
		if (speech_channel_open(app_session->verif_channel, profile) != 0) {
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}
	else {
		name = app_session->verif_channel->name;
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
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_BARGEIN) == MRCPRECOGVERIF_BARGEIN) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_BARGEIN])) {
			bargein = (atoi(mrcpverif_options.params[OPT_ARG_BARGEIN]) == 0) ? 0 : 1;
		}
	}

	dtmf_enable = 2;
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_INTERRUPT) == MRCPRECOGVERIF_INTERRUPT) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_INTERRUPT])) {
			dtmf_enable = 1;
			if (strcasecmp(mrcpverif_options.params[OPT_ARG_INTERRUPT], "any") == 0)
				mrcpverif_options.params[OPT_ARG_INTERRUPT] = AST_DIGIT_ANY;
			else if (strcasecmp(mrcpverif_options.params[OPT_ARG_INTERRUPT], "none") == 0)
				dtmf_enable = 2;
			else if (strcasecmp(mrcpverif_options.params[OPT_ARG_INTERRUPT], "disable") == 0)
				dtmf_enable = 0;
		}
	}

	/* Get NLSML instance format, if specified */
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_INSTANCE_FORMAT) == MRCPRECOGVERIF_INSTANCE_FORMAT) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_INSTANCE_FORMAT])) {
			const char *format = mrcpverif_options.params[OPT_ARG_INSTANCE_FORMAT];
			if (strcasecmp(format, "xml") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_XML;
			else if (strcasecmp(format, "json") == 0)
				app_session->instance_format = NLSML_INSTANCE_FORMAT_JSON;
		}
	}

	const char *filenames = NULL;
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_FILENAME) == MRCPRECOGVERIF_FILENAME) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_FILENAME])) {
			filenames = mrcpverif_options.params[OPT_ARG_FILENAME];
		}
	}

	if (filenames) {
		/* Get output delimiters. */
		const char *output_delimiters = "^";
		if ((mrcpverif_options.flags & MRCPRECOGVERIF_OUTPUT_DELIMITERS) == MRCPRECOGVERIF_OUTPUT_DELIMITERS) {
			if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_OUTPUT_DELIMITERS])) {
				output_delimiters = mrcpverif_options.params[OPT_ARG_OUTPUT_DELIMITERS];
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
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_EXIT_ON_PLAYERROR) == MRCPRECOGVERIF_EXIT_ON_PLAYERROR) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_EXIT_ON_PLAYERROR])) {
			exit_on_playerror = atoi(mrcpverif_options.params[OPT_ARG_EXIT_ON_PLAYERROR]);
			if ((exit_on_playerror < 0) || (exit_on_playerror > 2))
				exit_on_playerror = 1;
		}
	}

	int prompt_processing = (mrcpverif_prompts_available(app_session)) ? 1 : 0;
	struct ast_filestream *filestream = NULL;
	off_t max_filelength;

	/* If bargein is not allowed, play all the prompts and wait for for them to complete. */
	if (!bargein && prompt_processing) {
		/* Start playing first prompt. */
		filestream = mrcpverif_prompt_play(app_session, &mrcpverif_options, &max_filelength);
		if (!filestream && exit_on_playerror) {
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		do {
			if (filestream) {
				if (ast_waitstream(chan, "") != 0) {
					f = ast_read(chan);
					if (!f) {
						ast_log(LOG_DEBUG, "(%s) ast_waitstream failed on %s, channel read is a null frame. Hangup detected\n", name, ast_channel_name(chan));
						return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_INTERRUPTED);
					}
					ast_frfree(f);

					ast_log(LOG_WARNING, "(%s) ast_waitstream failed on %s\n", name, ast_channel_name(chan));
					return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
				filestream = NULL;
			}

			/* End of current prompt -> advance to the next one. */
			if (mrcpverif_prompts_advance(app_session) > 0) {
				/* Start playing current prompt. */
				filestream = mrcpverif_prompt_play(app_session, &mrcpverif_options, &max_filelength);
				if (!filestream && exit_on_playerror) {
					return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
				}
			}
			else {
				/* End of prompts. */
				break;
			}
		}
		while (mrcpverif_prompts_available(app_session));

		prompt_processing = 0;
	}

	/* Check the policy for input timers. */
	if ((mrcpverif_options.flags & MRCPRECOGVERIF_INPUT_TIMERS) == MRCPRECOGVERIF_INPUT_TIMERS) {
		if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_INPUT_TIMERS])) {
			switch(atoi(mrcpverif_options.params[OPT_ARG_INPUT_TIMERS])) {
				case 0: app_session->it_policy = IT_POLICY_OFF; break;
				case 1: app_session->it_policy = IT_POLICY_ON; break;
				default: app_session->it_policy = IT_POLICY_AUTO;
			}
		}
	}

	int start_input_timers = !prompt_processing;
	if (app_session->it_policy != IT_POLICY_AUTO)
		start_input_timers = app_session->it_policy;
	recognizer_data_t *r = app_session->verif_channel->data;

	ast_log(LOG_NOTICE, "(%s) Recognizing, enable DTMFs: %d, start input timers: %d\n", name, dtmf_enable, start_input_timers);

	/* Start verification. */
	if (verif_channel_start(app_session->verif_channel, name, start_input_timers, &mrcpverif_options) != 0) {

		const char *completion_cause = NULL;
		channel_get_results(app_session->verif_channel, &completion_cause, NULL, NULL);
		if (completion_cause)
			pbx_builtin_setvar_helper(chan, "VERIF_COMPLETION_CAUSE", completion_cause);

		return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
	}

	if (prompt_processing) {
		/* Start playing first prompt. */
		filestream = mrcpverif_prompt_play(app_session, &mrcpverif_options, &max_filelength);
		if (!filestream && exit_on_playerror) {
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

#if !AST_VERSION_AT_LEAST(11,0,0)
	off_t read_filestep = 0;
	off_t read_filelength;
#endif
	int waitres;
	int recog_processing;
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
			if (speech_channel_write(app_session->verif_channel, ast_frame_get_data(f), &len) != 0) {
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

					ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", app_session->verif_channel->name, digits);
					mpf_dtmf_generator_enqueue(app_session->dtmf_generator, digits);
				}
			} else if (dtmf_enable == 1) {
				/* Stop streaming if within i chars. */
				if (strchr(mrcpverif_options.params[OPT_ARG_INTERRUPT], dtmfkey) || (strcmp(mrcpverif_options.params[OPT_ARG_INTERRUPT],"any"))) {
					ast_frfree(f);
					mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_OK);
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

	apt_bool_t has_result = !(mrcpverif_options.flags & MRCPRECOGVERIF_BUF_HND)
				|| !strncmp("verify", mrcpverif_options.params[OPT_ARG_BUF_HND], 6);
	ast_log(LOG_NOTICE, "%s The result is %s.\n", name, has_result ? "available" : "unavailable");

	if (status == SPEECH_CHANNEL_STATUS_OK && has_result) {
		int uri_encoded_results = 0;
		/* Check if the results should be URI-encoded. */
		if ((mrcpverif_options.flags & MRCPRECOGVERIF_URI_ENCODED_RESULTS) == MRCPRECOGVERIF_URI_ENCODED_RESULTS) {
			if (!ast_strlen_zero(mrcpverif_options.params[OPT_ARG_URI_ENCODED_RESULTS])) {
				uri_encoded_results = (atoi(mrcpverif_options.params[OPT_ARG_URI_ENCODED_RESULTS]) == 0) ? 0 : 1;
			}
		}

		/* Get recognition result. */
		if (channel_get_results(app_session->verif_channel, &completion_cause, &result, &waveform_uri) != 0) {
			ast_log(LOG_WARNING, "(%s) Unable to retrieve result\n", name);
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}

		if (result) {
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
			return mrcpverif_exit(chan, app_session, SPEECH_CHANNEL_STATUS_ERROR);
		}
	}

	/* Completion cause should always be available at this stage. */
	if (completion_cause)
		pbx_builtin_setvar_helper(chan, "VERIF_COMPLETION_CAUSE", completion_cause);

	/* Result may not be available if recognition completed with nomatch, noinput, or other error cause. */
	pbx_builtin_setvar_helper(chan, "VERIF_RESULT", result ? result : "");

	/* If Waveform URI is available, pass it further to dialplan. */
	if (waveform_uri)
		pbx_builtin_setvar_helper(chan, "VERIF_WAVEFORM_URI", waveform_uri);

	return mrcpverif_exit(chan, app_session, status);
}

/* Load MRCPVerif application. */
int load_mrcpverif_app()
{
	apr_pool_t *pool = globals.pool;

	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	if(mrcpverif) {
		ast_log(LOG_ERROR, "Application %s is already loaded\n", app_verif);
		return -1;
	}

	mrcpverif = (ast_mrcp_application_t*) apr_palloc(pool, sizeof(ast_mrcp_application_t));
	mrcpverif->name = app_verif;
	mrcpverif->exec = app_verif_exec;
#if !AST_VERSION_AT_LEAST(1,6,2)
	mrcpverif->synopsis = NULL;
	mrcpverif->description = NULL;
#endif

	/* Create the recognizer application and link its callbacks */
	if ((mrcpverif->app = mrcp_application_create(verif_message_handler, (void *)mrcpverif, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create recognizer MRCP application %s\n", app_verif);
		mrcpverif = NULL;
		return -1;
	}

	mrcpverif->dispatcher.on_session_update = NULL;
	mrcpverif->dispatcher.on_session_terminate = speech_on_session_terminate;
	mrcpverif->dispatcher.on_channel_add = speech_on_channel_add;
	mrcpverif->dispatcher.on_channel_remove = NULL;
	mrcpverif->dispatcher.on_message_receive = mrcp_on_message_receive;
	mrcpverif->dispatcher.on_terminate_event = NULL;
	mrcpverif->dispatcher.on_resource_discover = NULL;
	mrcpverif->message_process.synth_message_process = synth_on_message_receive;
	mrcpverif->message_process.verif_message_process = verif_on_message_receive;
	mrcpverif->message_process.recog_message_process = recog_on_message_receive;
	mrcpverif->audio_stream_vtable.destroy = NULL;
	mrcpverif->audio_stream_vtable.open_rx = stream_open;
	mrcpverif->audio_stream_vtable.close_rx = NULL;
	mrcpverif->audio_stream_vtable.read_frame = stream_read;
	mrcpverif->audio_stream_vtable.open_tx = NULL;
	mrcpverif->audio_stream_vtable.close_tx = NULL;
	mrcpverif->audio_stream_vtable.write_frame = NULL;
	mrcpverif->audio_stream_vtable.trace = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, mrcpverif->app, app_verif)) {
		ast_log(LOG_ERROR, "Unable to register recognizer MRCP application %s\n", app_verif);
		if (!mrcp_application_destroy(mrcpverif->app))
			ast_log(LOG_WARNING, "Unable to destroy recognizer MRCP application %s\n", app_verif);
		mrcpverif = NULL;
		return -1;
	}

	apr_hash_set(globals.apps, app_verif, APR_HASH_KEY_STRING, mrcpverif);

	return 0;
}

/* Unload MRCPVerif application. */
int unload_mrcpverif_app()
{
	if(!mrcpverif) {
		ast_log(LOG_ERROR, "Application %s doesn't exist\n", app_verif);
		return -1;
	}

	apr_hash_set(globals.apps, app_verif, APR_HASH_KEY_STRING, NULL);
	mrcpverif = NULL;

	return 0;
}
