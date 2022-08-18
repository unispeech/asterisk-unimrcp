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
 * \brief MRCP message process
 *
 * \author\verbatim Fabiano Zaruch Chinasso <fzaruch@cpqd.com.br> \endverbatim
 * 
 * MRCP message process
 * \ingroup applications
 */

/* UniMRCP includes. */
#include "app_msg_process_dispatcher.h"
#include "mrcp_client_session.h"

/* Handle the MRCP responses/events from UniMRCP. */
apt_bool_t mrcp_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !message) {
		ast_log(LOG_ERROR, "speech_on_message_receive: unknown channel error!\n");
		return FALSE;
	}

	ast_mrcp_application_t* ast_app = (ast_mrcp_application_t*) application->obj;
	if (ast_app) {
		ast_log(LOG_NOTICE, "mrcp_on_message_receive channel: %d\n", schannel->type );
		if(schannel->type == SPEECH_CHANNEL_RECOGNIZER)
			return ast_app->message_process.recog_message_process(application, session, channel, message);
		else if(schannel->type == SPEECH_CHANNEL_VERIFIER)
			return ast_app->message_process.verif_message_process(application, session, channel, message);
	}

	return TRUE;
}

/* Handle the UniMRCP responses sent to session terminate requests. */
apt_bool_t speech_on_session_terminate(mrcp_application_t *application, mrcp_session_t *session, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel) {
		ast_log(LOG_ERROR, "speech_on_session_terminate: unknown channel error!\n");
		return FALSE;
	}

	ast_log(LOG_NOTICE, "(%s) TERMINATE speech_on_session_terminate\n", schannel->name);

	if (schannel->dtmf_generator != NULL) {
		ast_log(LOG_DEBUG, "(%s) DTMF generator destroyed\n", schannel->name);
		mpf_dtmf_generator_destroy(schannel->dtmf_generator);
		schannel->dtmf_generator = NULL;
	}

	ast_log(LOG_DEBUG, "(%s) Destroying MRCP session\n", schannel->name);

	if (!mrcp_application_session_destroy(session))
		ast_log(LOG_WARNING, "(%s) Unable to destroy application session\n", schannel->name);

	//speech_channel_set_state(schannel, SPEECH_CHANNEL_CLOSED);
	ast_mrcp_application_t* ast_app = (ast_mrcp_application_t*) application->obj;
	if (ast_app && ast_app->app_session) {
		speech_channel_set_state(ast_app->app_session->recog_channel, SPEECH_CHANNEL_CLOSED);
		speech_channel_set_state(ast_app->app_session->verif_channel, SPEECH_CHANNEL_CLOSED);
	}
	return TRUE;
}

/* Handle the UniMRCP responses sent to channel add requests. */
apt_bool_t speech_on_channel_add(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_sig_status_code_e status)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !channel) {
		ast_log(LOG_ERROR, "speech_on_channel_add: unknown channel error!\n");
		return FALSE;
	}

	ast_log(LOG_DEBUG, "(%s) speech_on_channel_add\n", schannel->name);

	if (status == MRCP_SIG_STATUS_CODE_SUCCESS) {
		const mpf_codec_descriptor_t *descriptor = mrcp_application_source_descriptor_get(channel);
		if (!descriptor) {
			ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
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

		if (!schannel->session_id) {
			const apt_str_t *session_id = mrcp_application_session_id_get(session);
			if (session_id && session_id->buf) {
				schannel->session_id = apr_pstrdup(schannel->pool, session_id->buf);
			}
		}
		
		ast_log(LOG_NOTICE, "(%s) Channel ready codec=%s, sample rate=%d\n",
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