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
#include "app_channel_methods.h"
#include "mrcp_client_session.h"

/* Start recognizer's input timers. */
int channel_start_input_timers(speech_channel_t *schannel, mrcp_method_id method_id)
{
	int status = 0;

	if (!schannel) {
		ast_log(LOG_ERROR, "start_input_timers: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if ((schannel->state == SPEECH_CHANNEL_PROCESSING) && (!r->timers_started)) {
		mrcp_message_t *mrcp_message;
		ast_log(LOG_DEBUG, "(%s) Sending START-INPUT-TIMERS request\n", schannel->name);

		/* Send START-INPUT-TIMERS to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session,
													   schannel->unimrcp_channel,
													   method_id);

		if (mrcp_message) {
			mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel,
										  mrcp_message);
		} else {
			ast_log(LOG_ERROR, "(%s) Failed to create START-INPUT-TIMERS message\n", schannel->name);
			status = -1;
		}
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Flag that input has started. */
int channel_set_start_of_input(speech_channel_t *schannel)
{
	int status = 0;

	if (!schannel) {
		ast_log(LOG_ERROR, "set_start_of_input: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	r->start_of_input = 1;

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Set the results. */
int channel_set_results(speech_channel_t *schannel, int completion_cause, const apt_str_t *result,
			const apt_str_t *waveform_uri)
{
	int status = 0;

	if (!schannel) {
		ast_log(LOG_ERROR, "set_results: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (r->completion_cause >= 0) {
		ast_log(LOG_DEBUG, "(%s) Result is already set\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (result && result->length > 0) {
		/* The duplicated string will always be NUL-terminated. */
		r->result = apr_pstrndup(schannel->pool, result->buf, result->length);
		ast_log(LOG_DEBUG, "(%s) Set result:\n\n%s\n", schannel->name, r->result);
	}
	r->completion_cause = completion_cause;
	if (waveform_uri && waveform_uri->length > 0)
		r->waveform_uri = apr_pstrndup(schannel->pool, waveform_uri->buf, waveform_uri->length);

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Get the Completion Cause results. */
int channel_get_completion_cause(speech_channel_t *schannel, const char **completion_cause)
{
	if (!schannel) {
		ast_log(LOG_ERROR, "verif_channel_get_completion_cause: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Result data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (r->completion_cause < 0) {
		ast_log(LOG_ERROR, "(%s) Method terminated prematurely\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (completion_cause) {
		*completion_cause = apr_psprintf(schannel->pool, "%03d", r->completion_cause);
		ast_log(LOG_DEBUG, "(%s) Completion-Cause: %s\n", schannel->name, *completion_cause);
		r->completion_cause = 0;
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return 0;
}

/* Get the results. */
int channel_get_results(speech_channel_t *schannel, const char **completion_cause,
			const char **result, const char **waveform_uri)
{
	if (!schannel) {
		ast_log(LOG_ERROR, "get_results: unknown channel error!\n");
		return -1;
	}

	channel_get_completion_cause(schannel, completion_cause);

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (result && r->result && strlen(r->result) > 0) {
		*result = apr_pstrdup(schannel->pool, r->result);
		ast_log(LOG_NOTICE, "(%s) Result:\n\n%s\n", schannel->name, *result);
		r->result = NULL;
	}

	if (waveform_uri && r->waveform_uri && (strlen(r->waveform_uri)) > 0) {
		*waveform_uri = apr_pstrdup(schannel->pool, r->waveform_uri);
		ast_log(LOG_DEBUG, "(%s) Waveform-URI: %s\n", schannel->name, *waveform_uri);
		r->waveform_uri = NULL;
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return 0;
}

/* Flag that the channel timers are started. */
int channel_set_timers_started(speech_channel_t *schannel)
{
	if (!schannel) {
		ast_log(LOG_ERROR, "set_timers_started: unknown channel error!\n");
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	r->timers_started = 1;

	apr_thread_mutex_unlock(schannel->mutex);
	return 0;
}

/* Start RECOGNIZE request. */
int recog_channel_start(speech_channel_t *schannel, const char *name, int start_input_timers,
			mrcprecogverif_options_t *options)
{
	int status = 0;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_recog_header_t *recog_header = NULL;
	recognizer_data_t *r = NULL;
	grammar_t *grammar = NULL;

	if (!schannel || !name) {
		ast_log(LOG_ERROR, "recog_channel_start: unknown channel error!\n");
		return -1;
	}
	
	apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state != SPEECH_CHANNEL_READY) {
		ast_log(LOG_ERROR, "Channel not ready!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (schannel->data == NULL) {
		ast_log(LOG_ERROR, "Channel data NULL!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if ((r = (recognizer_data_t *)schannel->data) == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	r->result = NULL;
	r->completion_cause = -1;
	r->start_of_input = 0;

	r->timers_started = start_input_timers;

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

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}
	grammar_refs[length] = '\0';

	/* Create MRCP message. */
	if ((mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session,
														schannel->unimrcp_channel,
														RECOGNIZER_RECOGNIZE)) == NULL) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Allocate generic header. */
	if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
		ast_log(LOG_ERROR, "Error to allocate generic header!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Set Content-Type to text/uri-list. */
	const char *mime_type = grammar_type_to_mime(GRAMMAR_TYPE_URI, schannel->profile);
	apt_string_assign(&generic_header->content_type, mime_type, mrcp_message->pool);
	mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

	/* Allocate recognizer-specific header. */
	if ((recog_header = (mrcp_recog_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
		ast_log(LOG_ERROR, "Error to allocate specific header!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Set Cancel-If-Queue. */
	if (mrcp_message->start_line.version == MRCP_VERSION_2) {
		recog_header->cancel_if_queue = FALSE;
		mrcp_resource_header_property_add(mrcp_message, RECOGNIZER_HEADER_CANCEL_IF_QUEUE);
	}

	/* Set Start-Input-Timers. */
	recog_header->start_input_timers = start_input_timers ? TRUE : FALSE;
	mrcp_resource_header_property_add(mrcp_message, RECOGNIZER_HEADER_START_INPUT_TIMERS);

	/* Set parameters. */
	speech_channel_set_params(schannel, mrcp_message, options->recog_hfs, options->rec_vendor_par_list);

	/* Set message body. */
	apt_string_assign_n(&mrcp_message->body, grammar_refs, length, mrcp_message->pool);

	/* Empty audio queue and send RECOGNIZE to MRCP server. */
	audio_queue_clear(schannel->audio_queue);

	if (mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel,
									  mrcp_message) == FALSE) {
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Wait for IN PROGRESS. */
	apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech_channel_timeout);

	if (schannel->state != SPEECH_CHANNEL_PROCESSING) {
		ast_log(LOG_ERROR, "Channel not processing!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Load speech recognition grammar. */
int recog_channel_load_grammar(speech_channel_t *schannel, const char *name, grammar_type_t type, const char *data)
{
	int status = 0;
	grammar_t *g = NULL;
	char ldata[256];

	if (!schannel || !name || !data) {
		ast_log(LOG_ERROR, "load_grammar: unknown channel error!\n");
		return -1;
	}

	const char *mime_type;
	if (((mime_type = grammar_type_to_mime(type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
		ast_log(LOG_WARNING, "(%s) Unable to get MIME type: %i\n", schannel->name, type);
		return -1;
	}
	ast_log(LOG_DEBUG, "(%s) Loading grammar name=%s, type=%s, data=%s\n", schannel->name, name, mime_type, data);

	apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state != SPEECH_CHANNEL_READY) {
		ast_log(LOG_ERROR, "Channel not ready!\n");
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* If inline, use DEFINE-GRAMMAR to cache it on the server. */
	if (type != GRAMMAR_TYPE_URI) {
		mrcp_message_t *mrcp_message;
		mrcp_generic_header_t *generic_header;

		/* Create MRCP message. */
		if ((mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session,
															schannel->unimrcp_channel,
															RECOGNIZER_DEFINE_GRAMMAR)) == NULL) {
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Set Content-Type and Content-ID in message. */
		if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
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

		if (mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel,
										  mrcp_message) == FALSE) {
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech_channel_timeout);

		if (schannel->state != SPEECH_CHANNEL_READY) {
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

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}

/* Start VERIFY request. */
int verif_channel_start(speech_channel_t *schannel, const char *name, int start_input_timers, mrcprecogverif_options_t *options)
{
	int status = 0;
	apt_bool_t buffer_handling = FALSE;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_message_t *verif_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_verifier_header_t *verif_header = NULL;
	recognizer_data_t *r = NULL;
	grammar_t *grammar = NULL;

	if (!schannel || !name) {
		ast_log(LOG_ERROR, "(%s) verif_channel_start: unknown channel error!\n", schannel->name);
		return -1;
	}

	apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state != SPEECH_CHANNEL_READY) {
		ast_log(LOG_ERROR, "(%s) verif_channel_start: SPEECH_CHANNEL_READY error!\n", schannel->name);
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if (schannel->data == NULL) {
		ast_log(LOG_ERROR, "(%s) verif_channel_start: SPEECH_CHANNEL_NULL error!\n", schannel->name);
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	if ((r = (recognizer_data_t *)schannel->data) == NULL) {
		ast_log(LOG_ERROR, "(%s) Verify data struct is NULL\n", schannel->name);

		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	r->result = NULL;
	r->completion_cause = -1;
	r->start_of_input = 0;

	r->timers_started = start_input_timers;

	apr_hash_index_t *hi;
	void *val;
	int length = 0;

	if (!(schannel->has_sess & CHANNEL_VER_SESS)) {
		/* Create MRCP message. */
		if ((mrcp_message = mrcp_application_message_create(schannel->session->unimrcp_session,
															schannel->unimrcp_channel,
															VERIFIER_START_SESSION)) == NULL) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: Create MRCP message error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Allocate generic header. */
		if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: Allocate generic header error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Allocate verifier-specific header. */
		if ((verif_header = (mrcp_verifier_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: Allocate verifier-specific header error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Set Start-Input-Timers. */
		verif_header->start_input_timers = start_input_timers ? TRUE : FALSE;
		mrcp_resource_header_property_add(mrcp_message, VERIFIER_HEADER_START_INPUT_TIMERS);

		/* Set parameters for START SESSION. */
		speech_channel_set_params(schannel, mrcp_message, options->verif_session_hfs, options->ver_vendor_par_list);

		/* Empty audio queue and send START-SESSION to MRCP server. */
		audio_queue_clear(schannel->audio_queue);

		if (mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel,
										  mrcp_message) == FALSE) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: mrcp_application_message_send error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}

		/* Wait for COMPLETE. */
		apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech_channel_timeout);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: Not SPEECH_CHANNEL_READY error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}
		schannel->has_sess |= CHANNEL_VER_SESS;
	}

	if (options->flags & MRCPRECOGVERIF_BUF_HND) {
		if (!strncmp("verify", options->params[OPT_ARG_BUF_HND], 6)) {
			if ((verif_message = mrcp_application_message_create(schannel->session->unimrcp_session,
																 schannel->unimrcp_channel,
																 VERIFIER_VERIFY_FROM_BUFFER)) == NULL) {
				ast_log(LOG_ERROR, "(%s) verif_channel_start: Not mrcp_application_message_create error!\n", schannel->name);
				apr_thread_mutex_unlock(schannel->mutex);
				return -1;
			}
		} else if (!strncmp("clear", options->params[OPT_ARG_BUF_HND], 5)) {
			if ((verif_message = mrcp_application_message_create(schannel->session->unimrcp_session,
																 schannel->unimrcp_channel,
																 VERIFIER_CLEAR_BUFFER)) == NULL) {
				ast_log(LOG_ERROR, "(%s) verif_channel_start: Not mrcp_application_message_create error!\n", schannel->name);
				apr_thread_mutex_unlock(schannel->mutex);
				return -1;
			}
			buffer_handling = TRUE;
		} else if (!strncmp("rollback", options->params[OPT_ARG_BUF_HND], 8)) {
			if ((verif_message = mrcp_application_message_create(schannel->session->unimrcp_session,
																 schannel->unimrcp_channel,
																 VERIFIER_VERIFY_ROLLBACK)) == NULL) {
				ast_log(LOG_ERROR, "(%s) verif_channel_start: Not mrcp_application_message_create error!\n", schannel->name);
				apr_thread_mutex_unlock(schannel->mutex);
				return -1;
			}
			buffer_handling = TRUE;
		}
	}

	if ((!verif_message && (verif_message = mrcp_application_message_create(schannel->session->unimrcp_session,
																			schannel->unimrcp_channel,
																			VERIFIER_VERIFY)) == NULL)) {
		ast_log(LOG_ERROR, "(%s) verif_channel_start: Not mrcp_application_message_create error!\n", schannel->name);
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Set parameters for VERIFY. */
	speech_channel_set_params(schannel, verif_message, options->verif_hfs, NULL);

	if (mrcp_application_message_send(schannel->session->unimrcp_session, schannel->unimrcp_channel, verif_message) == FALSE) {
		ast_log(LOG_ERROR, "(%s) verif_channel_start: Not mrcp_application_message_send error!\n", schannel->name);
		apr_thread_mutex_unlock(schannel->mutex);
		return -1;
	}

	/* Wait for IN PROGRESS. */
	apr_thread_cond_timedwait(schannel->cond, schannel->mutex, globals.speech_channel_timeout);

	/* Verify if it is processing when the result was not receive yet; */
	if (r->completion_cause < 0) {
		if (!buffer_handling && schannel->state != SPEECH_CHANNEL_PROCESSING) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: SPEECH CHANNEL NOT PROCESSING error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		} else if (buffer_handling && schannel->state != SPEECH_CHANNEL_READY) {
			ast_log(LOG_ERROR, "(%s) verif_channel_start: SPEECH CHANNEL NOT READY error!\n", schannel->name);
			apr_thread_mutex_unlock(schannel->mutex);
			return -1;
		}
	}

	apr_thread_mutex_unlock(schannel->mutex);
	return status;
}
