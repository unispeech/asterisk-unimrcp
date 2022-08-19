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
#include "app_channel_methods.h"
#include "mrcp_client_session.h"

/* Handle the MRCP responses/events from UniMRCP. */
/* Get speech channel associated with provided MRCP session. */
APR_INLINE speech_channel_t * get_speech_channel(mrcp_session_t *session)
{
	if (session)
		return (speech_channel_t *)mrcp_application_session_object_get(session);

	return NULL;
}

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
		if(schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
			return ast_app->message_process.synth_message_process(application, session, channel, message);
		else if(schannel->type == SPEECH_CHANNEL_RECOGNIZER)
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

/* Handle the MRCP responses/events. */
apt_bool_t recog_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !message) {
		ast_log(LOG_ERROR, "recog_on_message_receive: unknown channel error!\n");
		return FALSE;
	}

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
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d\n", schannel->name, message->start_line.status_code);
				else {
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d, completion-cause = %03d\n", schannel->name, message->start_line.status_code, recog_hdr->completion_cause);
					channel_set_results(schannel, recog_hdr->completion_cause, NULL, NULL);
				}
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
					channel_set_timers_started(schannel);
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
					if (recog_hdr->completion_cause == RECOGNIZER_COMPLETION_CAUSE_UNKNOWN)
						ast_log(LOG_DEBUG, "(%s) Grammar failed to load, status code = %d\n", schannel->name, message->start_line.status_code);
					else {
						ast_log(LOG_DEBUG, "(%s) Grammar failed to load, status code = %d, completion-cause = %03d\n", schannel->name, message->start_line.status_code, recog_hdr->completion_cause);
						channel_set_results(schannel, recog_hdr->completion_cause, NULL, NULL);
					}
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
			channel_set_results(schannel, recog_hdr->completion_cause, &message->body, &recog_hdr->waveform_uri);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
		} else if (message->start_line.method_id == RECOGNIZER_START_OF_INPUT) {
			ast_log(LOG_DEBUG, "(%s) START OF INPUT\n", schannel->name);
			channel_set_start_of_input(schannel);
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

/* Handle the MRCP responses/events. */
apt_bool_t verif_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel = get_speech_channel(session);
	if (!schannel || !message) {
		ast_log(LOG_ERROR, "verif_on_message_receive: unknown channel error!\n");
		return FALSE;
	}

	mrcp_verifier_header_t *recog_hdr = (mrcp_verifier_header_t *)mrcp_resource_header_get(message);
	if (message->start_line.message_type == MRCP_MESSAGE_TYPE_RESPONSE) {
		/* Received MRCP response. */
		if (message->start_line.method_id == VERIFIER_VERIFY
			|| message->start_line.method_id == VERIFIER_VERIFY_FROM_BUFFER) {
			/* Received the response to VERIFY request. */
			if (message->start_line.request_state == MRCP_REQUEST_STATE_INPROGRESS) {
				/* VERIFY in progress. */
				ast_log(LOG_NOTICE, "(%s) VERIFY IN PROGRESS\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_PROCESSING);
			} else if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
				/* RECOGNIZE failed to start. */
				if (recog_hdr->completion_cause == VERIFIER_COMPLETION_CAUSE_UNKNOWN)
					ast_log(LOG_DEBUG, "(%s) VERIFY failed: status = %d\n", schannel->name, message->start_line.status_code);
				else {
					ast_log(LOG_DEBUG, "(%s) VERIFY failed: status = %d, completion-cause = %03d\n", schannel->name, message->start_line.status_code, recog_hdr->completion_cause);
					channel_set_results(schannel, recog_hdr->completion_cause, NULL, NULL);
				}
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			} else if (message->start_line.request_state == MRCP_REQUEST_STATE_PENDING)
				/* RECOGNIZE is queued. */
				ast_log(LOG_NOTICE, "(%s) VERIFY PENDING\n", schannel->name);
			else {
				/* Received unexpected request_state. */
				ast_log(LOG_DEBUG, "(%s) Unexpected VERIFY request state: %d\n", schannel->name, message->start_line.request_state);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else if (message->start_line.method_id == VERIFIER_START_SESSION) {
			/* Received response to the STOP request. */
			if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
				/* Got COMPLETE. */
				ast_log(LOG_DEBUG, "(%s) VERIFIER STARTED\n", schannel->name);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
			} else {
				/* Received unexpected request state. */
				ast_log(LOG_DEBUG, "(%s) Unexpected VERIFIER START request state: %d\n", schannel->name, message->start_line.request_state);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else if (message->start_line.method_id == VERIFIER_START_INPUT_TIMERS) {
			/* Received response to START-INPUT-TIMERS request. */
			if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
				if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
					ast_log(LOG_DEBUG, "(%s) Timers started\n", schannel->name);
					channel_set_timers_started(schannel);
				} else
					ast_log(LOG_DEBUG, "(%s) Timers failed to start, status code = %d\n", schannel->name, message->start_line.status_code);
			}
		} else if (message->start_line.method_id == VERIFIER_VERIFY_ROLLBACK
			   || message->start_line.method_id == VERIFIER_CLEAR_BUFFER) {
			/* Received response to START-INPUT-TIMERS request. */
			if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
				if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
					ast_log(LOG_DEBUG, "(%s) Buffer clered / Rollbacked\n", schannel->name);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
				} else
					ast_log(LOG_WARNING, "(%s) Fail to Buffer handle, status code = %d\n", schannel->name, message->start_line.status_code);
				channel_set_results(schannel, message->start_line.status_code, NULL, NULL);
			}
		} else {
			/* Received unexpected response. */
			ast_log(LOG_WARNING, "(%s) Unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
		}
	} else if (message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
		/* Received MRCP event. */
		if (message->start_line.method_id == VERIFIER_VERIFICATION_COMPLETE) {
			ast_log(LOG_DEBUG, "(%s) VERIFICATION COMPLETE, Completion-Cause: %03d\n", schannel->name, recog_hdr->completion_cause);
			channel_set_results(schannel, recog_hdr->completion_cause, &message->body, &recog_hdr->waveform_uri);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
		} else if (message->start_line.method_id == VERIFIER_START_OF_INPUT) {
			ast_log(LOG_DEBUG, "(%s) START OF INPUT\n", schannel->name);
			channel_set_start_of_input(schannel);
		} else {
			ast_log(LOG_DEBUG, "(%s) Unexpected event, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
		}
	} else {
		ast_log(LOG_WARNING, "(%s) Unexpected message type, message_type = %d\n", schannel->name, message->start_line.message_type);
		speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
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
		const mpf_codec_descriptor_t *descriptor = NULL;
		if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
			descriptor = mrcp_application_sink_descriptor_get(channel);
		else
			descriptor = mrcp_application_source_descriptor_get(channel);
		if (!descriptor) {
			ast_log(LOG_ERROR, "(%s) Unable to determine codec descriptor\n", schannel->name);
			speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			return FALSE;
		}

		if (schannel->type != SPEECH_CHANNEL_SYNTHESIZER && schannel->stream != NULL) {
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

/* UniMRCP callback requesting stream to be opened. */
apt_bool_t stream_open(mpf_audio_stream_t* stream, mpf_codec_t *codec)
{
	speech_channel_t* schannel;

	if (stream)
		schannel = (speech_channel_t*)stream->obj;
	else
		schannel = NULL;

	if (!schannel) {
		ast_log(LOG_ERROR, "recog_stream_open: unknown channel error!\n");
		return FALSE;
	}

	schannel->stream = stream;
	return TRUE;
}

/* UniMRCP callback requesting next frame for speech treatment. */
apt_bool_t stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
	speech_channel_t *schannel;

	if (stream != NULL)
		schannel = (speech_channel_t *)stream->obj;
	else
		schannel = NULL;

	if (!schannel || !frame) {
		ast_log(LOG_ERROR, "recog_stream_read: unknown channel error!\n");
		return FALSE;
	}

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

	return TRUE;
}