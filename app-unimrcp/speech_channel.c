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

/* Asterisk includes. */
#include "ast_compat_defs.h"

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"

#include "audio_queue.h"
#include "speech_channel.h"

#define MIME_TYPE_SSML_XML						"application/ssml+xml"
#define MIME_TYPE_PLAIN_TEXT					"text/plain"

#define XML_ID									"<?xml"
#define SRGS_ID									"<grammar"
#define SSML_ID									"<speak"
#define GSL_ID									";GSL2.0"
#define ABNF_ID									"#ABNF"
#define JSGF_ID									"#JSGF"
#define BUILTIN_ID								"builtin:"
#define SESSION_ID								"session:"
#define HTTP_ID									"http://"
#define HTTPS_ID								"https://"
#define FILE_ID									"file://"
#define INLINE_ID								"inline:"


/* --- MRCP SPEECH CHANNEL --- */

/* Convert channel state to string. */
const char *speech_channel_state_to_string(speech_channel_state_t state)
{
	switch (state) {
		case SPEECH_CHANNEL_CLOSED: return "CLOSED";
		case SPEECH_CHANNEL_READY: return "READY";
		case SPEECH_CHANNEL_PROCESSING: return "PROCESSING";
		case SPEECH_CHANNEL_ERROR: return "ERROR";
		default: return "UNKNOWN";
	}
}

/* Convert speech channel type into a string. */
const char *speech_channel_type_to_string(speech_channel_type_t type)
{
	switch (type) {
		case SPEECH_CHANNEL_SYNTHESIZER: return "SYNTHESIZER";
		case SPEECH_CHANNEL_RECOGNIZER: return "RECOGNIZER";
		default: return "UNKNOWN";
	}
}

/* Set parameter. */
int speech_channel_set_param(speech_channel_t *schannel, const char *param, const char *val)
{
	if ((schannel != NULL) && (param != NULL) && (strlen(param) > 0)) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		ast_log(LOG_DEBUG, "(%s) param = %s, val = %s\n", schannel->name, param, val);

		if (schannel->params != NULL)
			apr_hash_set(schannel->params, apr_pstrdup(schannel->pool, param), APR_HASH_KEY_STRING, apr_pstrdup(schannel->pool, val));

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	}

	return 0;
}

/* Use this function to set the current channel state without locking the 
 * speech channel.  Do this if you already have the speech channel locked.
 */
void speech_channel_set_state_unlocked(speech_channel_t *schannel, speech_channel_state_t state)
{
	if (schannel != NULL) {
		/* Wake anyone waiting for audio data. */
		if ((schannel->state == SPEECH_CHANNEL_PROCESSING) && (state != SPEECH_CHANNEL_PROCESSING))
			audio_queue_clear(schannel->audio_queue);

		ast_log(LOG_DEBUG, "(%s) %s ==> %s\n", schannel->name, speech_channel_state_to_string(schannel->state), speech_channel_state_to_string(state));
		schannel->state = state;

		if (schannel->cond != NULL)
			apr_thread_cond_signal(schannel->cond);
	}
}

/* Set the current channel state. */
void speech_channel_set_state(speech_channel_t *schannel, speech_channel_state_t state)
{
	if (schannel != NULL) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		speech_channel_set_state_unlocked(schannel, state);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	}
}

/* Send BARGE-IN-OCCURRED. */
int speech_channel_bargeinoccurred(speech_channel_t *schannel) 
{
	int status = 0;
	
	if (schannel == NULL)
		return -1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
		mrcp_method_id method;
		mrcp_message_t *mrcp_message;

		method = SYNTHESIZER_BARGE_IN_OCCURRED;
		ast_log(LOG_DEBUG, "(%s) Sending barge-in on %s\n", schannel->name, speech_channel_type_to_string(schannel->type));

		/* Send STOP to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, method);

		if (mrcp_message == NULL) {
			ast_log(LOG_ERROR, "(%s) Failed to create BARGE_IN_OCCURRED message\n", schannel->name);
			status = -1;
		} else {
			if (!mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message))
				ast_log(LOG_WARNING, "(%s) [speech_channel_bargeinoccurred] Failed to send BARGE_IN_OCCURRED message\n", schannel->name);
			else if (schannel->cond != NULL) {
				while (schannel->state == SPEECH_CHANNEL_PROCESSING) {
					if (apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC) == APR_TIMEUP) {
						break;
					}
				}
			}

			if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
				ast_log(LOG_ERROR, "(%s) Timed out waiting for session to close.  Continuing\n", schannel->name);
				schannel->state = SPEECH_CHANNEL_ERROR;
				status = -1;
			} else if (schannel->state == SPEECH_CHANNEL_ERROR) {
				ast_log(LOG_ERROR, "(%s) Channel error\n", schannel->name);
				schannel->state = SPEECH_CHANNEL_ERROR;
				status = -1;
			} else {
				ast_log(LOG_DEBUG, "(%s) %s barge-in sent\n", schannel->name, speech_channel_type_to_string(schannel->type));
			}
		}
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

int speech_channel_create(speech_channel_t **schannel, const char *name, speech_channel_type_t type, ast_mrcp_application_t *app, const char *codec, apr_uint16_t rate, struct ast_channel *chan)
{
	apr_pool_t *pool = NULL;
	speech_channel_t *schan = NULL;
	int status = 0;

	if (schannel != NULL)
		*schannel = NULL;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "Speech channel structure pointer is NULL\n");
		status = -1;
	} else if (app == NULL) {
		ast_log(LOG_ERROR, "MRCP application is NULL\n");
		status = -1;
	} else if ((pool = apt_pool_create()) == NULL) {
		ast_log(LOG_ERROR, "Unable to create memory pool for channel\n");
		status = -1;
	} else if ((schan = (speech_channel_t *)apr_palloc(pool, sizeof(speech_channel_t))) == NULL) {
		ast_log(LOG_ERROR, "Unable to allocate speech channel structure\n");
		status = -1;
	} else {
		if ((name == NULL) || (strlen(name) == 0)) {
			ast_log(LOG_WARNING, "No name specified, assuming \"TTS\"\n");
			schan->name = "TTS";
		} else
			schan->name = apr_pstrdup(pool, name);
		if ((schan->name == NULL) || (strlen(schan->name) == 0)) {
			ast_log(LOG_WARNING, "Unable to allocate name for channel, using \"TTS\"\n");
			schan->name = "TTS";
		}

		if ((codec == NULL) || (strlen(codec) == 0)) {
			ast_log(LOG_WARNING, "(%s) No codec specified, assuming \"L16\"\n", schan->name);
			schan->codec = "L16";
		} else
			schan->codec = apr_pstrdup(pool, codec);
		if ((schan->codec == NULL) || (strlen(schan->codec) == 0)) {
			ast_log(LOG_WARNING, "(%s) Unable to allocate codec for channel, using \"L16\"\n", schan->name);
			schan->codec = "L16";
		}

		schan->profile = NULL;
		schan->type = type;
		schan->application = app;
		schan->unimrcp_session = NULL;
		schan->unimrcp_channel = NULL;
		schan->stream = NULL;
		schan->dtmf_generator = NULL;
		schan->pool = pool;
		schan->mutex = NULL;
		schan->cond = NULL;
		schan->state = SPEECH_CHANNEL_CLOSED;
		schan->audio_queue = NULL;
		schan->rate = rate;
		schan->params = apr_hash_make(pool);
		schan->data = NULL;
		schan->chan = chan;

		if (strstr("L16", schan->codec)) {
			schan->silence = 0;
		} else {
			/* 8-bit PCMU, PCMA. */
			schan->silence = 128;
		}

		if (schan->params == NULL) {
			ast_log(LOG_ERROR, "(%s) Unable to allocate hash for channel parameters\n",schan->name);
			status = -1;
		} else if ((apr_thread_mutex_create(&schan->mutex, APR_THREAD_MUTEX_UNNESTED, pool) != APR_SUCCESS) || (schan->mutex == NULL)) {
			ast_log(LOG_ERROR, "(%s) Unable to create channel mutex\n", schan->name);
			status = -1;
		} else if ((apr_thread_cond_create(&schan->cond, pool) != APR_SUCCESS) || (schan->cond == NULL)) {
			ast_log(LOG_ERROR, "(%s) Unable to create channel condition variable\n",schan->name);
			status = -1;
		} else if ((audio_queue_create(&schan->audio_queue, name) != 0) || (schan->audio_queue == NULL)) {
			ast_log(LOG_ERROR, "(%s) Unable to create audio queue for channel\n",schan->name);
			status = -1;
		} else {
			ast_log(LOG_DEBUG, "Created speech channel: Name=%s, Type=%s, Codec=%s, Rate=%u\n", schan->name, speech_channel_type_to_string(schan->type), schan->codec, schan->rate);
			*schannel = schan;
		}
	}

	if (status != 0) {
		if (schan != NULL) {
			if (schan->audio_queue != NULL) {
				if (audio_queue_destroy(schan->audio_queue) != 0)
					ast_log(LOG_WARNING, "(%s) Unable to destroy channel audio queue\n", schan->name);
			}

			if (schan->cond != NULL) {
				if (apr_thread_cond_destroy(schan->cond) != APR_SUCCESS)
					ast_log(LOG_WARNING, "(%s) Unable to destroy channel condition variable\n", schan->name);

				schan->cond = NULL;
			}

			if (schan->mutex != NULL) {
				if (apr_thread_mutex_destroy(schan->mutex) != APR_SUCCESS)
					ast_log(LOG_WARNING, "(%s) Unable to destroy channel mutex variable\n", schan->name);
			}

			schan->name = NULL;
			schan->profile = NULL;
			schan->application = NULL;
			schan->unimrcp_session = NULL;
			schan->unimrcp_channel = NULL;
			schan->stream = NULL;
			schan->dtmf_generator = NULL;
			schan->pool = NULL;
			schan->mutex = NULL;
			schan->cond = NULL;
			schan->audio_queue = NULL;
			schan->codec = NULL;
			schan->params = NULL;
			schan->data = NULL;
			schan->chan = NULL;
		}

		if (pool != NULL)
			apr_pool_destroy(pool);
	}

	return status;
}

#if UNI_VERSION_AT_LEAST(0,8,0)
mpf_termination_t *speech_channel_create_mpf_termination(speech_channel_t *schannel)
{   
	mpf_termination_t *termination = NULL;
	mpf_stream_capabilities_t *capabilities = NULL;
	int sample_rates;

	if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
		capabilities = mpf_sink_stream_capabilities_create(schannel->unimrcp_session->pool);
	else
		capabilities = mpf_source_stream_capabilities_create(schannel->unimrcp_session->pool);

	if (capabilities == NULL)
		ast_log(LOG_ERROR, "(%s) Unable to create capabilities\n", schannel->name);

	/* UniMRCP should transcode whatever the MRCP server wants to use into LPCM
	 * (host-byte ordered L16) for us. Asterisk may not support all of these.
	 */
	if (schannel->rate == 16000)
		sample_rates = MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000;
	else if (schannel->rate == 32000)
		sample_rates = MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000 | MPF_SAMPLE_RATE_32000;
	else if (schannel->rate == 48000)
		sample_rates = MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000 | MPF_SAMPLE_RATE_48000;
	else
		sample_rates = MPF_SAMPLE_RATE_8000;

	/* TO DO : Check if all of these are supported on Asterisk for all codecs. */
	if (strcasecmp(schannel->codec, "L16") == 0)
		mpf_codec_capabilities_add(&capabilities->codecs, sample_rates, "LPCM");
	else
		mpf_codec_capabilities_add(&capabilities->codecs, sample_rates, schannel->codec);

	termination = mrcp_application_audio_termination_create(
					schannel->unimrcp_session,                        /* Session, termination belongs to. */
					&schannel->application->audio_stream_vtable,      /* Virtual methods table of audio stream. */
                    capabilities,                                     /* Capabilities of audio stream. */
                    schannel);                                        /* Object to associate. */

	if (termination == NULL)
		ast_log(LOG_ERROR, "(%s) Unable to create termination\n", schannel->name);

	return termination;
}
#else
mpf_termination_t *speech_channel_create_mpf_termination(speech_channel_t *schannel)
{
	mpf_termination_t *termination = NULL;
	mpf_codec_descriptor_t *codec = NULL;

	/* Create RTP endpoint and link to session channel. */
	if ((codec = (mpf_codec_descriptor_t *)apr_palloc(schannel->unimrcp_session->pool, sizeof(mpf_codec_descriptor_t))) == NULL) {
		ast_log(LOG_ERROR, "(%s) Unable to create codec\n", schannel->name);

		if (!mrcp_application_session_destroy(schannel->unimrcp_session))
			ast_log(LOG_WARNING, "(%s) Unable to destroy application session\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return NULL;
	}

	mpf_codec_descriptor_init(codec);
	codec->channel_count = 1;
	codec->payload_type = 96;
	codec->sampling_rate = schannel->rate;

	/* "LPCM" is UniMRCP's name for L16 host byte ordered */
	if (strcasecmp(schannel->codec, "L16") == 0)
		apt_string_set(&codec->name, "LPCM");
	else
		apt_string_set(&codec->name, schannel->codec);

	/* See RFC 1890 for payload types. */
	if ((strcasecmp(schannel->codec, "PCMU") == 0) && (schannel->rate == 8000))
		codec->payload_type = 0;
	else if ((strcasecmp(schannel->codec, "PCMA") == 0) && (schannel->rate == 8000))
		codec->payload_type = 8;

	ast_log(LOG_DEBUG, "(%s) requesting codec %s/%d/%d\n", schannel->name, schannel->codec, codec->payload_type, codec->sampling_rate);

	if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
		termination = mrcp_application_sink_termination_create(schannel->unimrcp_session, &schannel->application->audio_stream_vtable, codec, schannel);
	else
		termination = mrcp_application_source_termination_create(schannel->unimrcp_session, &schannel->application->audio_stream_vtable, codec, schannel);

	return termination;
}
#endif

/* Destroy the speech channel. */
int speech_channel_destroy(speech_channel_t *schannel)
{
	if (schannel != NULL) {
		apr_pool_t *pool = schannel->pool;
		ast_log(LOG_DEBUG, "Destroying speech channel: Name=%s, Type=%s, Codec=%s, Rate=%u\n", schannel->name, speech_channel_type_to_string(schannel->type), schannel->codec, schannel->rate);

		if (schannel->mutex)
			apr_thread_mutex_lock(schannel->mutex);

		/* Destroy the channel and session if not already done. */
		if (schannel->state != SPEECH_CHANNEL_CLOSED) {
			int warned = 0;

			if ((schannel->unimrcp_session != NULL) && (schannel->unimrcp_channel != NULL)) {
				if (!mrcp_application_session_terminate(schannel->unimrcp_session))
					ast_log(LOG_WARNING, "(%s) %s unable to terminate application session\n", schannel->name, speech_channel_type_to_string(schannel->type));

				/* if (!mrcp_application_channel_remove(schannel->unimrcp_session, schannel->unimrcp_channel))
					ast_log(LOG_WARNING, "(%s) Unable to remove channel from application\n", schannel->name);
				 */
			}

			ast_log(LOG_DEBUG, "(%s) Waiting for MRCP session to terminate\n", schannel->name);
			while (schannel->state != SPEECH_CHANNEL_CLOSED) {
				if (schannel->cond != NULL) {
					if ((apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC) == APR_TIMEUP) && (!warned)) {
						warned = 1;
						ast_log(LOG_WARNING, "(%s) MRCP session has not terminated after %d ms\n", schannel->name, SPEECH_CHANNEL_TIMEOUT_USEC / 1000);
					}
				}
			}
		}

		if (schannel->state != SPEECH_CHANNEL_CLOSED) {
			ast_log(LOG_ERROR, "(%s) Failed to destroy channel.  Continuing\n", schannel->name);
		}

		if (schannel->dtmf_generator != NULL) {
			ast_log(LOG_NOTICE, "(%s) DTMF generator destroyed\n", schannel->name);
			mpf_dtmf_generator_destroy(schannel->dtmf_generator);
			schannel->dtmf_generator = NULL;
		}

		if (schannel->audio_queue != NULL) {
			if (audio_queue_destroy(schannel->audio_queue) != 0)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel audio queue\n",schannel->name);
			else
				ast_log(LOG_NOTICE, "(%s) Audio queue destroyed\n", schannel->name);
		}

		if (schannel->params != NULL)
			apr_hash_clear(schannel->params);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		if (schannel->cond != NULL) {
			if (apr_thread_cond_destroy(schannel->cond) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel condition variable\n", schannel->name);
		}

		if (schannel->mutex != NULL) {
			if (apr_thread_mutex_destroy(schannel->mutex) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel condition variable\n", schannel->name);
		}

		schannel->name = NULL;
		schannel->profile = NULL;
		schannel->application = NULL;
		schannel->unimrcp_session = NULL;
		schannel->unimrcp_channel = NULL;
		schannel->stream = NULL;
		schannel->dtmf_generator = NULL;
		schannel->pool = NULL;
		schannel->mutex = NULL;
		schannel->cond = NULL;
		schannel->audio_queue = NULL;
		schannel->codec = NULL;
		schannel->params = NULL;
		schannel->data = NULL;
		schannel->chan = NULL;

		if (pool != NULL)
			apr_pool_destroy(pool);
	} else {
		ast_log(LOG_ERROR, "Speech channel structure pointer is NULL\n");
		return -1;
	}
	
	ast_log(LOG_DEBUG, "Destroyed speech channel complete\n");

	return 0;
}

/* Open the speech channel. */
int speech_channel_open(speech_channel_t *schannel, ast_mrcp_profile_t *profile)
{
	int status = 0;
	mpf_termination_t *termination = NULL;
	mrcp_resource_type_e resource_type;

	if ((schannel == NULL) || (profile == NULL))
		return -1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	/* Make sure we can open channel. */
	if (schannel->state != SPEECH_CHANNEL_CLOSED) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	schannel->profile = profile;

	/* Create MRCP session. */
	if ((schannel->unimrcp_session = mrcp_application_session_create(schannel->application->app, profile->name, schannel)) == NULL) {
		/* Profile doesn't exist? */
		ast_log(LOG_ERROR, "(%s) Unable to create session with %s\n", schannel->name, profile->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return 2;
	}
	
	/* Set session name for logging purposes. */
	mrcp_application_session_name_set(schannel->unimrcp_session, schannel->name);

	/* Create audio termination and add to channel. */
	if ((termination = speech_channel_create_mpf_termination(schannel)) == NULL) {
		ast_log(LOG_ERROR, "(%s) Unable to create termination with %s\n", schannel->name, profile->name);

		if (!mrcp_application_session_destroy(schannel->unimrcp_session))
			ast_log(LOG_WARNING, "(%s) Unable to destroy application session for %s\n", schannel->name, profile->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
		resource_type = MRCP_SYNTHESIZER_RESOURCE;
	else
		resource_type = MRCP_RECOGNIZER_RESOURCE;

	if ((schannel->unimrcp_channel = mrcp_application_channel_create(schannel->unimrcp_session, resource_type, termination, NULL, schannel)) == NULL) {
		ast_log(LOG_ERROR, "(%s) Unable to create channel with %s\n", schannel->name, profile->name);

		if (!mrcp_application_session_destroy(schannel->unimrcp_session))
			ast_log(LOG_WARNING, "(%s) Unable to destroy application session for %s\n", schannel->name, profile->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	/* Add channel to session. This establishes the connection to the MRCP server. */
	if (mrcp_application_channel_add(schannel->unimrcp_session, schannel->unimrcp_channel) != TRUE) {
		ast_log(LOG_ERROR, "(%s) Unable to add channel to session with %s\n", schannel->name, profile->name);

		if (!mrcp_application_session_destroy(schannel->unimrcp_session))
			ast_log(LOG_WARNING, "(%s) Unable to destroy application session for %s\n", schannel->name, profile->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	/* Wait for channel to be ready. */
	while ((schannel->mutex != NULL) && (schannel->cond != NULL) && (schannel->state == SPEECH_CHANNEL_CLOSED))
		apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

	if (schannel->state == SPEECH_CHANNEL_READY) {
		ast_log(LOG_DEBUG, "(%s) channel is ready\n", schannel->name);
	} else if (schannel->state == SPEECH_CHANNEL_CLOSED) {
		ast_log(LOG_ERROR, "(%s) Timed out waiting for channel to be ready\n", schannel->name);
		/* Can't retry. */
		status = -1;
	} else if (schannel->state == SPEECH_CHANNEL_ERROR) {
		/* Wait for session to be cleaned up. */
		if (schannel->cond != NULL)
			apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

		if (schannel->state != SPEECH_CHANNEL_CLOSED) {
			/* Major issue. Can't retry. */
			status = -1;
		} else {
			/* Failed to open profile, retry is allowed. */
			status = 2;
		}
	}

	if (schannel->type == SPEECH_CHANNEL_RECOGNIZER) {
		recognizer_data_t *r = (recognizer_data_t *)apr_palloc(schannel->pool, sizeof(recognizer_data_t));

		if (r != NULL) {
			schannel->data = r;
			memset(r, 0, sizeof(recognizer_data_t));

			if ((r->grammars = apr_hash_make(schannel->pool)) == NULL) {
				ast_log(LOG_ERROR, "Unable to allocate hash for grammars\n");
				status = -1;
			}
		} else {
			ast_log(LOG_ERROR, "Unable to allocate recognizer data structure\n");
			status = -1;
		}
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Stop SPEAK/RECOGNIZE request on speech channel. */
int speech_channel_stop(speech_channel_t *schannel)
{
	int status = 0;

	if (schannel == NULL)
		return -1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
		mrcp_method_id method;
		mrcp_message_t *mrcp_message;

		if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
			method = SYNTHESIZER_STOP;
		else
			method = RECOGNIZER_STOP;

		ast_log(LOG_DEBUG, "(%s) Stopping %s\n", schannel->name, speech_channel_type_to_string(schannel->type));

		/* Send STOP to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, method);

		if (mrcp_message == NULL) {
			ast_log(LOG_ERROR, "(%s) Failed to create STOP message\n", schannel->name);
			status = -1;
		} else {
			if (!mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message))
				ast_log(LOG_WARNING, "(%s) Failed to send STOP message\n", schannel->name);
			else if (schannel->cond != NULL) {
				while (schannel->state == SPEECH_CHANNEL_PROCESSING) {
					if (apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC) == APR_TIMEUP) {
						break;
					}
				}
			}

			if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
				ast_log(LOG_ERROR, "(%s) Timed out waiting for session to close.  Continuing\n", schannel->name);
				schannel->state = SPEECH_CHANNEL_ERROR;
				status = -1;
			} else if (schannel->state == SPEECH_CHANNEL_ERROR) {
				ast_log(LOG_ERROR, "(%s) Channel error\n", schannel->name);
				schannel->state = SPEECH_CHANNEL_ERROR;
				status = -1;
			} else {
				ast_log(LOG_DEBUG, "(%s) %s stopped\n", schannel->name, speech_channel_type_to_string(schannel->type));
			}
		}
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Read synthesized speech / speech to be recognized. */
int speech_channel_read(speech_channel_t *schannel, void *data, apr_size_t *len, int block)
{
	int status = 0;

	if (schannel != NULL) {
		audio_queue_t *queue = schannel->audio_queue;

		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
			if ((queue != NULL) && (data != NULL) && (len > 0))
				audio_queue_read(queue, data, len, block);
		} else
			status = 1;

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	} else {
		ast_log(LOG_ERROR, "Speech channel structure pointer is NULL\n");
		return -1;
	}

	return status;
}

/* Write synthesized speech / speech to be recognized. */
int speech_channel_write(speech_channel_t *schannel, void *data, apr_size_t *len)
{
	apr_size_t	mylen;
	int			res = 0;

	if (len != NULL)
		mylen = *len;
	else
		mylen = 0;

	if ((schannel != NULL) && (mylen > 0)) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		audio_queue_t *queue = schannel->audio_queue;

		if (schannel->state == SPEECH_CHANNEL_PROCESSING) {
			if ((queue != NULL) && (data != NULL) && (mylen > 0)) {
				audio_queue_write(queue, data, &mylen);
				*len = mylen;
			}
		} else
			res = -1;

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	} else {
		ast_log(LOG_ERROR, "Speech channel structure pointer is NULL\n");
		return -1;
	}

	return res;
}

const char* get_synth_content_type(speech_channel_t *schannel, const char *text)
{
	/* Good enough way of determining SSML or plain text body. */
	if (text_starts_with(text, XML_ID) || text_starts_with(text, SSML_ID))
		return schannel->profile->ssml_mime_type;
	
	return MIME_TYPE_PLAIN_TEXT;
}

const char* get_grammar_type(speech_channel_t *schannel, const char *grammar_data, grammar_type_t *grammar_type)
{
	grammar_type_t tmp_grammar = GRAMMAR_TYPE_UNKNOWN;
	if ((text_starts_with(grammar_data, HTTP_ID)) || (text_starts_with(grammar_data, HTTPS_ID)) || (text_starts_with(grammar_data, BUILTIN_ID)) || (text_starts_with(grammar_data, FILE_ID)) || (text_starts_with(grammar_data, SESSION_ID))) {
		tmp_grammar = GRAMMAR_TYPE_URI;
	} else if (text_starts_with(grammar_data, INLINE_ID)) {
		grammar_data = grammar_data + strlen(INLINE_ID);
	} else if (text_starts_with(grammar_data, ABNF_ID)) {
		tmp_grammar = GRAMMAR_TYPE_SRGS;
	} else {
		/* TO DO : Instead of assuming SRGS+XML, assume it is a file and MRCP server can't get to file, so read it and let the content decide the grammar type. */
		tmp_grammar = GRAMMAR_TYPE_SRGS_XML;
	}

	if (tmp_grammar == GRAMMAR_TYPE_UNKNOWN) {
		if ((text_starts_with(grammar_data, HTTP_ID)) || (text_starts_with(grammar_data, HTTPS_ID)) || (text_starts_with(grammar_data, BUILTIN_ID)) || (text_starts_with(grammar_data, FILE_ID)) || (text_starts_with(grammar_data, SESSION_ID))) {
			tmp_grammar = GRAMMAR_TYPE_URI;
		} else if ((text_starts_with(grammar_data, XML_ID) || (text_starts_with(grammar_data, SRGS_ID)))) {
			tmp_grammar = GRAMMAR_TYPE_SRGS_XML;
		} else if (text_starts_with(grammar_data, GSL_ID)) {
			tmp_grammar = GRAMMAR_TYPE_NUANCE_GSL;
		} else if (text_starts_with(grammar_data, ABNF_ID)) {
			tmp_grammar = GRAMMAR_TYPE_SRGS;
		} else if (text_starts_with(grammar_data, JSGF_ID)) {
			tmp_grammar = GRAMMAR_TYPE_JSGF;
		} else {
			// Unable to determine grammar type
		}
	}

	if(grammar_type)
		*grammar_type = tmp_grammar;
	return grammar_data;
}

/* Create a grammar object to reference in recognition requests. */
int grammar_create(grammar_t **grammar, const char *name, grammar_type_t type, const char *data, apr_pool_t *pool)
{
	int status = 0;
	grammar_t *g = (grammar_t *)apr_palloc(pool, sizeof(grammar_t));

	if (g == NULL) {
		status = -1;
		*grammar = NULL;
	} else {
		g->name = apr_pstrdup(pool, name);
		g->type = type;
		g->data = apr_pstrdup(pool, data);
		*grammar = g;
	}

	return status;
}

/* Get the MIME type for this grammar type. */
const char *grammar_type_to_mime(grammar_type_t type, const ast_mrcp_profile_t *profile)
{
	switch (type) {
		case GRAMMAR_TYPE_UNKNOWN: return "";
		case GRAMMAR_TYPE_URI: return "text/uri-list";
		case GRAMMAR_TYPE_SRGS: return profile->srgs_mime_type;
		case GRAMMAR_TYPE_SRGS_XML: return profile->srgs_xml_mime_type;
		case GRAMMAR_TYPE_NUANCE_GSL: return profile->gsl_mime_type;
		case GRAMMAR_TYPE_JSGF: return profile->jsgf_mime_type;
		default: return "";
	}
}

/* --- CODEC/FORMAT FUNCTIONS  --- */

static int get_speech_codec(int codec)
{
	switch(codec) {
		/*! G.723.1 compression */
		case AST_FORMAT_G723_1: return AST_FORMAT_SLINEAR;
		/*! GSM compression */
		case AST_FORMAT_GSM: return AST_FORMAT_SLINEAR;
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return AST_FORMAT_ULAW;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return AST_FORMAT_ALAW;
#if AST_VERSION_AT_LEAST(1,4,0)
		/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
		case AST_FORMAT_G726_AAL2: return AST_FORMAT_SLINEAR;
#endif
		/*! ADPCM (IMA) */
		case AST_FORMAT_ADPCM: return AST_FORMAT_SLINEAR; 
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: return AST_FORMAT_SLINEAR;
		/*! LPC10, 180 samples/frame */
		case AST_FORMAT_LPC10: return AST_FORMAT_SLINEAR;
		/*! G.729A audio */
		case AST_FORMAT_G729A: return AST_FORMAT_SLINEAR;
		/*! SpeeX Free Compression */
		case AST_FORMAT_SPEEX: return AST_FORMAT_SLINEAR;
		/*! iLBC Free Compression */
		case AST_FORMAT_ILBC: return AST_FORMAT_SLINEAR; 
		/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
		case AST_FORMAT_G726: return AST_FORMAT_SLINEAR;
#if AST_VERSION_AT_LEAST(1,4,0)
		/*! G.722 */
		case AST_FORMAT_G722: return AST_FORMAT_SLINEAR;
#endif
#if AST_VERSION_AT_LEAST(1,6,2)
		/*! G.722.1 (also known as Siren7, 32kbps assumed) */
		case AST_FORMAT_SIREN7: return AST_FORMAT_SLINEAR;
		/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
		case AST_FORMAT_SIREN14: return AST_FORMAT_SLINEAR;
#endif
#if AST_VERSION_AT_LEAST(1,6,0)
		/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
		case AST_FORMAT_SLINEAR16: return AST_FORMAT_SLINEAR;
#endif
		default: return AST_FORMAT_SLINEAR;
	}
	return AST_FORMAT_SLINEAR;
}

int get_synth_format(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_compat raw_format;
	ast_channel_get_rawwriteformat(chan, &raw_format);
	format->id = get_speech_codec(raw_format.id);
	return 0;
}

int get_recog_format(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_compat raw_format;
	ast_channel_get_rawreadformat(chan, &raw_format);
	format->id = get_speech_codec(raw_format.id);
	return 0;
}

const char* format_to_str(const ast_format_compat *format)
{
	switch(format->id) {
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return "PCMU";
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return "PCMA";
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: return "L16";
		/*! Use Raw 16-bit Signed Linear (8000 Hz) PCM for the rest */
		default: return "L16";
	}
	return "L16";
}

int format_to_bytes_per_sample(const ast_format_compat *format)
{
	switch(format->id) {
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return 1;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return 1;
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: return 2;
		/*! Use Raw 16-bit Signed Linear (8000 Hz) PCM for the rest */
		default: return 2;
	}
	return 2;
}

/* --- GENERIC FUNCTIONS --- */
void trimstr(char* input) 
{
	unsigned long int i;
	unsigned long int j;

	if (input != NULL) {
		for (i = 0; i < strlen(input); i++) {
			if ((input[i] != ' ') && (input[i] != '\t'))
				break;
		}

		for (j = i; j < strlen(input); j++)
			input[j - i] = input[j];
		for (j = (strlen(input) - i); j < strlen(input); j++)
			input[j] = '\0';
		for (j = strlen(input) - 1; j >= 0; j--) {
			if ((input[j] != ' ') && (input[j] != '\t'))
				break;
			else
				input[j] = '\0';
		}
	}
}

/* Inspect text to determine if its first non-whitespace text matches "match". */
int text_starts_with(const char *text, const char *match)
{
	int result = 0;

	if ((text != NULL) && (strlen(text) > 0) && (match != NULL)) {
		apr_size_t matchlen;
		apr_size_t textlen;

		/* Find first non-space character. */
		while (isspace(*text))
			text++;

		textlen = strlen(text);
		matchlen = strlen(match);

		/* Is there a match? */
		result = (textlen > matchlen) && (strncmp(match, text, matchlen) == 0);
	}
	
	return result;
}
