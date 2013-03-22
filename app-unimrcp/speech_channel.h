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

#ifndef SPEECH_CHANNEL_H
#define SPEECH_CHANNEL_H

#define SPEECH_CHANNEL_TIMEOUT_USEC				(30 * 1000000)

/* Type of MRCP channel. */
enum speech_channel_type_t {
	SPEECH_CHANNEL_SYNTHESIZER,
	SPEECH_CHANNEL_RECOGNIZER
};
typedef enum speech_channel_type_t speech_channel_type_t;

/* Channel states. */
enum speech_channel_state_t {
	/* Closed. */
	SPEECH_CHANNEL_CLOSED,
	/* Ready for speech request. */
	SPEECH_CHANNEL_READY,
	/* Processing speech request. */
	SPEECH_CHANNEL_PROCESSING,
	/* Error opening channel. */
	SPEECH_CHANNEL_ERROR
};
typedef enum speech_channel_state_t speech_channel_state_t;

/* An MRCP speech channel. */
struct speech_channel_t {
	/* The name of this channel (for logging). */
	char *name;
	/* The profile used by this channel. */
	ast_mrcp_profile_t *profile;
	/* Type of channel. */
	speech_channel_type_t type;
	/* Application this channel is running. */
	ast_mrcp_application_t *application;
	/* UniMRCP session. */
	mrcp_session_t *unimrcp_session;
	/* UniMRCP channel. */
	mrcp_channel_t *unimrcp_channel;
	/* UniMRCP stream object. */
	mpf_audio_stream_t *stream;
	/* UniMRCP DTMF digit generator. */
	mpf_dtmf_generator_t *dtmf_generator;
	/* Memory pool. */
	apr_pool_t *pool;
	/* Synchronizes channel state/ */
	apr_thread_mutex_t *mutex;
	/* Wait on channel states. */
	apr_thread_cond_t *cond;
	/* Channel state. */
	speech_channel_state_t state;
	/* UniMRCP <--> Asterisk audio buffer. */
	audio_queue_t *audio_queue;
	/* Codec. */
	/* BOOKMARK - Remove next line. */
	char *codec;
	/* Rate. */
	apr_uint16_t rate;
	/* Silence byte. */
	apr_byte_t silence;
	/* App specific data. */
	void *data;
	/* Asterisk channel. Needed to stop playback on barge-in. */
	struct ast_channel *chan;
};
typedef struct speech_channel_t speech_channel_t;

/* Type of the grammar. */
enum grammar_type_t {
	GRAMMAR_TYPE_UNKNOWN,
	/* text/uri-list. */
	GRAMMAR_TYPE_URI,
	/* application/srgs. */
	GRAMMAR_TYPE_SRGS,
	/* application/srgs+xml. */
	GRAMMAR_TYPE_SRGS_XML,
	/* application/x-nuance-gsl. */
	GRAMMAR_TYPE_NUANCE_GSL,
	/* application/x-jsgf. */
	GRAMMAR_TYPE_JSGF
};  
typedef enum grammar_type_t grammar_type_t;

/* A grammar for recognition. */
struct grammar_t {
	/* Name of this grammar. */
	char *name;
	/* Grammar MIME type. */
	grammar_type_t type;
	/* The grammar or its URI, depending on type. */
	char *data;
};
typedef struct grammar_t grammar_t;

/* Data specific to the recognizer. */
struct recognizer_data_t {
	/* The available grammars. */
	apr_hash_t *grammars;
	/* The last grammar used (for pause/resume). */
	grammar_t *last_grammar;
	/* Recognition result. */
	const char *result;
	/* Completion cause. */
	int completion_cause;
	/* Waveform URI [optional]. */
	const char *waveform_uri;
	/* True, if voice has started. */
	int start_of_input;
	/* True, if input timers have started. */
	int timers_started;
};
typedef struct recognizer_data_t recognizer_data_t;


/* Use this function to set the current channel state without locking the 
 * speech channel.  Do this if you already have the speech channel locked.
 */
void speech_channel_set_state_unlocked(speech_channel_t *schannel, speech_channel_state_t state);

/* Set the current channel state. */
void speech_channel_set_state(speech_channel_t *schannel, speech_channel_state_t state);

/* Send BARGE-IN-OCCURRED. */
int speech_channel_bargeinoccurred(speech_channel_t *schannel);

int speech_channel_create(speech_channel_t **schannel, const char *name, speech_channel_type_t type, ast_mrcp_application_t *app, const char *codec, apr_uint16_t rate, struct ast_channel *chan);

mpf_termination_t *speech_channel_create_mpf_termination(speech_channel_t *schannel);

/* Destroy the speech channel. */
int speech_channel_destroy(speech_channel_t *schannel);

/* Open the speech channel. */
int speech_channel_open(speech_channel_t *schannel, ast_mrcp_profile_t *profile);

/* Stop SPEAK/RECOGNIZE request on speech channel. */
int speech_channel_stop(speech_channel_t *schannel);

/* Set parameters in an MRCP header. */
int speech_channel_set_params(speech_channel_t *schannel, mrcp_message_t *msg, apr_hash_t *header_fields);

/* Read synthesized speech / speech to be recognized. */
int speech_channel_read(speech_channel_t *schannel, void *data, apr_size_t *len, int block);

/* Write synthesized speech / speech to be recognized. */
int speech_channel_write(speech_channel_t *schannel, void *data, apr_size_t *len);

/* Convert channel state to string. */
const char *speech_channel_state_to_string(speech_channel_state_t state);

/* Convert speech channel type into a string. */
const char *speech_channel_type_to_string(speech_channel_type_t type);

/* 
 * Determine synthesis content type by specified text.
 * @param schannel the speech channel to use
 * @param text the input text
 * @param content_type the output content
 * @param content_type the output content type
 */
int determine_synth_content_type(speech_channel_t *schannel, const char *text, const char **content, const char **content_type);

/* 
 * Determine grammar type by specified grammar data.
 * @param schannel the speech channel to use
 * @param grammar_data the input grammar data
 * @param grammar_type the output grammar content
 * @param grammar_type the output grammar type
 */
int determine_grammar_type(speech_channel_t *schannel, const char *grammar_data, const char **grammar_content, grammar_type_t *grammar_type);

/* Create a grammar object to reference in recognition requests. */
int grammar_create(grammar_t **grammar, const char *name, grammar_type_t type, const char *data, apr_pool_t *pool);

/* Get the MIME type for this grammar type. */
const char *grammar_type_to_mime(grammar_type_t type, const ast_mrcp_profile_t *profile);

/* --- CODEC/FORMAT FUNCTIONS  --- */
int get_synth_format(struct ast_channel *chan, ast_format_compat *format);
int get_recog_format(struct ast_channel *chan, ast_format_compat *format);

const char* format_to_str(const ast_format_compat *format);
int format_to_bytes_per_sample(const ast_format_compat *format);

#endif /* SPEECH_CHANNEL_H */
