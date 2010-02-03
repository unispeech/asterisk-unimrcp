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
 * \brief MRCP suite of applications
 *
 * \author\verbatim J.W.F. Thirion <derik@molo.co.za> \endverbatim
 * 
 * MRCP suite of applications
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
	<depend>unimrcp</depend>
	<depend>apr</depend>
 ***/

#define AST_MODULE "app_unimrcp"

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 200656 $")
#ifdef ASTERISK14
	#include <stdio.h>
#endif

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"

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
	<application name="MRCPRecog" language="en_US">
		<synopsis>
			MRCP recognition application.
		</synopsis>
		<syntax>
			<parameter name="grammar" required="true"/>
			<parameter name="options" required="true"/>
		</syntax>
		<description>
		<para>MRCP recognition application.
		Supports version 1 and 2 of MRCP, using UniMRCP. First parameter is grammar /
		text of speech. Second paramater contains more options: p=profile, i=interrupt
		key, t=auto speech timeout, f=filename of prompt to play, b=bargein value (no
		barge-in=0, ASR engine barge-in=1, Asterisk barge-in=2, ct=confidence
		threshold (0.0 - 1.0), sl=sensitivity level (0.0 - 1.0), sva=speed vs accuracy
		(0.0 - 1.0), nb=n-best list length (1 - 19 digits), nit=no input timeout (1 -
		19 digits), sit=start input timers (true/false), sct=speech complete timeout
		(1 - 19 digits), sint=speech incomplete timeout (1 - 19 digits), dit=DTMF
		interdigit timeout (1 - 19 digits), dtt=DTMF terminate timout (1 - 19 digits),
		dttc=DTMF terminate characters, sw=save waveform (true/false), nac=new audio
		channel (true/false), sl=speech language (en-US/en-GB/etc.), rm=recognition
		mode, hmaxd=hotword max duration (1 - 19 digits), hmind=hotword min duration
		(1 - 19 digits), cdb=clear DTMF buffer (true/false), enm=early no match
		(true/false), iwu=input waveform URI, mt=media type.</para>
		</description>
	</application>
 ***/

/* --- MRCP GENERIC --- */

/* UniMRCP includes. */
#include <apr_general.h>
#include <apr_hash.h>
#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>
#include "apt.h"
#include "apt_log.h"
#include "apt_net.h"
#include "apt_pool.h"
#include "unimrcp_client.h"
#include "mrcp_application.h"
#include "mrcp_session.h"
#include "mrcp_message.h"
#include "mrcp_generic_header.h"
#include "mrcp_synth_header.h"
#include "mrcp_synth_resource.h"
#include "mrcp_recog_header.h"
#include "mrcp_recog_resource.h"
#include "uni_version.h"
#if UNI_VERSION_AT_LEAST(0,8,0)
#include "mrcp_resource_loader.h"
#else
#include "mrcp_default_factory.h"
#endif
#include "mpf_engine.h"
#include "mpf_codec_manager.h"
#include "mpf_dtmf_generator.h"
#include "mpf_rtp_termination_factory.h"
#include "mrcp_sofiasip_client_agent.h"
#include "mrcp_unirtsp_client_agent.h"
#include "mrcp_client_connection.h"

/* The name of the applications. */
static char *app_synth = "MRCPSynth";
static char *app_recog = "MRCPRecog";

#if !defined(ASTERISKSVN) && !defined(ASTERISK162)
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

static char *recogsynopsis = "MRCP recognition application.";
static char *recogdescrip =
"Supports version 1 and 2 of MRCP, using UniMRCP. First parameter is grammar /\n"
"text of speech. Second paramater contains more options: p=profile, i=interrupt\n"
"key, t=auto speech timeout, f=filename of prompt to play, b=bargein value (no\n"
"barge-in=0, ASR engine barge-in=1, Asterisk barge-in=2, ct=confidence\n"
"threshold (0.0 - 1.0), sl=sensitivity level (0.0 - 1.0), sva=speed vs accuracy\n"
"(0.0 - 1.0), nb=n-best list length (1 - 19 digits), nit=no input timeout (1 -\n"
"19 digits), sit=start input timers (true/false), sct=speech complete timeout\n"
"(1 - 19 digits), sint=speech incomplete timeout (1 - 19 digits), dit=DTMF\n"
"interdigit timeout (1 - 19 digits), dtt=DTMF terminate timout (1 - 19 digits),\n"
"dttc=DTMF terminate characters, sw=save waveform (true/false), nac=new audio\n"
"channel (true/false), sl=speech language (en-US/en-GB/etc.), rm=recognition\n"
"mode, hmaxd=hotword max duration (1 - 19 digits), hmind=hotword min duration\n"
"(1 - 19 digits), cdb=clear DTMF buffer (true/false), enm=early no match\n"
"(true/false), iwu=input waveform URI, mt=media type.\n";
#endif

/* The configuration file to read. */
#define MRCP_CONFIG "mrcp.conf"

#define DEFAULT_UNIMRCP_MAX_CONNECTION_COUNT	"120"
#define DEFAULT_UNIMRCP_OFFER_NEW_CONNECTION	"1"
#define DEFAULT_UNIMRCP_LOG_LEVEL				"DEBUG"

#define DEFAULT_LOCAL_IP_ADDRESS				"127.0.0.1"
#define DEFAULT_REMOTE_IP_ADDRESS				"127.0.0.1"
#define DEFAULT_SIP_LOCAL_PORT					5090
#define DEFAULT_SIP_REMOTE_PORT					5060
#define DEFAULT_RTP_PORT_MIN					4000
#define DEFAULT_RTP_PORT_MAX					5000

#define DEFAULT_SOFIASIP_UA_NAME				"Asterisk"
#define DEFAULT_SDP_ORIGIN						"Asterisk"
#define DEFAULT_RESOURCE_LOCATION				"media"

/* Default frame size:
 *
 * 8000 samples/sec * 20ms = 160 * 2 bytes/sample = 320 bytes
 * 16000 samples/sec * 20ms = 320 * 2 bytes/sample = 640 bytes
 */
#define DEFAULT_FRAMESIZE						320

#define DSP_FRAME_ARRAY_SIZE					1024

/* Default audio buffer size:
 *
 * 8000 samples/sec * 2 bytes/sample (16-bit) * 1 second = 16000 bytes
 * 16000 samples/sec * 2 bytes/sample (16-bit) * 1 second = 32000 bytes
 *
 * Make provision for 16kHz sample rates with 16-bit samples, 1 second audio.
 */
#define AUDIO_QUEUE_SIZE						(16000 * 2)

#define SPEECH_CHANNEL_TIMEOUT_USEC				(10 * 1000000)

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

int apr_initialized = 0;

#ifndef ASTERISKSVN
typedef int64_t format_t;
#endif

/* MRCP application. */
struct my_mrcp_application_t {
	/* UniMRCP application. */
	mrcp_application_t *app;
	/* MRCP callbacks from UniMRCP to this module's application. */
	mrcp_app_message_dispatcher_t dispatcher;
	/* Audio callbacks from UniMRCP to this module's application. */
	mpf_audio_stream_vtable_t audio_stream_vtable;
	/* Maps MRCP header to unimrcp header handler function. */
	apr_hash_t *param_id_map;
};
typedef struct my_mrcp_application_t my_mrcp_application_t;

/* MRCP globals configuration and variables. */
struct my_mrcp_globals_t {
	/* The memory pool to use. */
	apr_pool_t* pool;

	/* The max-connection-count configuration. */
	char *unimrcp_max_connection_count;
	/* The offer-new-connection configuration. */
	char *unimrcp_offer_new_connection;
	/* The rx-buffer-size configuration. */
	char *unimrcp_rx_buffer_size;
	/* The tx-buffer-size configuration. */
	char *unimrcp_tx_buffer_size;
	/* The default text-to-speech profile to use. */
	char *unimrcp_default_synth_profile;
	/* The default speech recognition profile to use. */
	char *unimrcp_default_recog_profile;
	/* Log level to use for the UniMRCP library. */
	char *unimrcp_log_level;

	/* The MRCP client stack. */
	mrcp_client_t *mrcp_client;

	/* The text-to-speech application. */
	my_mrcp_application_t synth;
	/* The speech recognition application. */
	my_mrcp_application_t recog;

	/* Mutex to used for speech channel numbering. */
	apr_thread_mutex_t *mutex;
	/* Next available speech channel number. */
	apr_uint32_t speech_channel_number;
	/* The available profiles. */
	apr_hash_t *profiles;
};
typedef struct my_mrcp_globals_t my_mrcp_globals_t;

/* Profile-specific configuration. This allows us to handle differing MRCP
 * server behavior on a per-profile basis.
 */
struct profile_t {
	/* Name of the profile. */
	char *name;
	/* MRCP version of the profile. */
	char *version;
	/* MIME type to use for JSGF grammars. */
	const char *jsgf_mime_type;
	/* MIME type to use for GSL grammars. */
	const char *gsl_mime_type;
	/* MIME type to use for SRGS XML grammars. */
	const char *srgs_xml_mime_type;
	/* MIME type to use for SRGS ABNF grammars. */
	const char *srgs_mime_type;
	/* The profile configuration. */
	apr_hash_t *cfg;
};
typedef struct profile_t profile_t;

struct audio_buffer_t {
	/* The memory pool. */
	apr_pool_t *pool;
	/* The actual data. */
	apr_byte_t *data;
	/* Number of bytes used in the buffer. */
	apr_size_t used;
	/* Size of the buffer. */
	apr_size_t datalen;
};
typedef struct audio_buffer_t audio_buffer_t;

struct audio_queue_t {
	/* The memory pool. */
	apr_pool_t *pool;
	/* The buffer of audio data. */
	audio_buffer_t *buffer;
	/* Synchronizes access to queue. */
	apr_thread_mutex_t *mutex;
	/* Signaling for blocked readers/writers. */
	apr_thread_cond_t *cond;
	/* Total bytes written. */
	apr_size_t write_bytes;
	/* Total bytes read. */
	apr_size_t read_bytes;
	/* Number of bytes reader is waiting for. */
	apr_size_t waiting;
	/* Name of this queue (for logging). */
	char *name;
};
typedef struct audio_queue_t audio_queue_t;

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
	profile_t *profile;
	/* Type of channel. */
	speech_channel_type_t type;
	/* Application this channel is running. */
	my_mrcp_application_t *application;
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
	/* Speech channel params. */
	apr_hash_t *params;
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
	/* Recognize result. */
	char *result;
	/* true, if voice has started. */
	int start_of_input;
	/* True, if input timers have started. */
	int timers_started;
};
typedef struct recognizer_data_t recognizer_data_t;

/* UniMRCP parameter ID container. */
struct unimrcp_param_id {
	/* The parameter ID. */
	int id;
};
typedef struct unimrcp_param_id unimrcp_param_id_t;

/* General note on return codes of functions:
 *
 * -1 = FAILURE
 *  0 = SUCCESS
 *  1 = BREAK
 *  2 = RESTART
 */



/* --- MRCP GLOBALS AND CONFIGURATION--- */

/* Global variables. */
static my_mrcp_globals_t globals;

static void globals_null(void)
{
	/* Set all variables to NULL so that checks work as expected. */
	globals.pool = NULL;
	globals.unimrcp_max_connection_count = NULL;
	globals.unimrcp_offer_new_connection = NULL;
	globals.unimrcp_rx_buffer_size = NULL;
	globals.unimrcp_tx_buffer_size = NULL;
	globals.unimrcp_default_synth_profile = NULL;
	globals.unimrcp_default_recog_profile = NULL;
	globals.unimrcp_log_level = NULL;
	globals.mrcp_client = NULL;
	globals.synth.app = NULL;
	globals.synth.param_id_map = NULL;
	globals.recog.app = NULL;
	globals.recog.param_id_map = NULL;
	globals.mutex = NULL;
	globals.speech_channel_number = 0;
	globals.profiles = NULL;
}

static void globals_clear(void)
{
	/* Clear the hashes for the configuration of the profiles and the hash for the
	 * profile itself.
	 */
	if (globals.profiles != NULL) {
		apr_hash_index_t *hi;

		for (hi = apr_hash_first(NULL, globals.profiles); hi; hi = apr_hash_next(hi)) {
			const char *k;
			const void *key;
			profile_t *v;
			void *val;

			apr_hash_this(hi, &key, NULL, &val);
	
			k = (const char *)key;
			v = (profile_t *)val;

			if (v != NULL) {
				ast_log(LOG_DEBUG, "Clearing profile config for %s\n", k);
				v->name = NULL;
				v->version = NULL;
				v->jsgf_mime_type = NULL;
				v->gsl_mime_type = NULL;
				v->srgs_xml_mime_type = NULL;
				v->srgs_mime_type = NULL;
				apr_hash_clear(v->cfg);
				v->cfg = NULL;
			}

			k = NULL;
			v = NULL;
		}

		apr_hash_clear(globals.profiles);
	}
}

static void globals_default(void)
{
	/* Initialize some of the variables with default values. */
	globals.unimrcp_max_connection_count = DEFAULT_UNIMRCP_MAX_CONNECTION_COUNT;
	globals.unimrcp_offer_new_connection = DEFAULT_UNIMRCP_OFFER_NEW_CONNECTION;
	globals.unimrcp_log_level = DEFAULT_UNIMRCP_LOG_LEVEL;
	globals.speech_channel_number = 0;
}

static void globals_destroy(void)
{
	/* Free existing memory in globals. */
	globals_clear();

	if (globals.mutex != NULL) {
		if (apr_thread_mutex_destroy(globals.mutex) != APR_SUCCESS)
			ast_log(LOG_WARNING, "Unable to destroy global mutex\n");
	}

	if (globals.pool != NULL)
		apr_pool_destroy(globals.pool);

	/* Set values to NULL. */
	globals_null();
}

static int globals_init(void)
{
	/* Set values to NULL. */
	globals_null();

	/* Create an APR pool. */
	if ((globals.pool = apt_pool_create()) == NULL) {
		ast_log(LOG_ERROR, "Unable to create global memory pool\n");
		return -1;
	}

	/* Create globals mutex and hash map for profiles. */
	if ((apr_thread_mutex_create(&globals.mutex, APR_THREAD_MUTEX_UNNESTED, globals.pool) != APR_SUCCESS) || (globals.mutex == NULL)) {
		ast_log(LOG_ERROR, "Unable to create global mutex\n");
		apr_pool_destroy(globals.pool);
		globals.pool = NULL;
		globals.mutex = NULL;
		return -1;
	}

	/* Create a hash for the profiles. */
	if ((globals.profiles = apr_hash_make(globals.pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create profiles hash\n");
		apr_thread_mutex_destroy(globals.mutex);
		apr_pool_destroy(globals.pool);
		globals.pool = NULL;
		globals.mutex = NULL;
		return -1;
	}

	/* Set the default values. */
	globals_default();

	return 0;
}



/* --- GENERIC FUNCTIONS --- */
static void trimstr(char* input) {
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

/* Get IP address from IP address value. */
static char *ip_addr_get(const char *value, apr_pool_t *pool)
{   
	if ((value == NULL) || (strcasecmp(value, "auto") == 0)) {
		char *addr = DEFAULT_LOCAL_IP_ADDRESS;
		apt_ip_get(&addr, pool);
		return addr;
	}

	return apr_pstrdup(pool, value);
}

/* Return the next number to assign the channel. */
static apr_uint32_t get_next_speech_channel_number(void)
{
	apr_uint32_t num;

	if (globals.mutex != NULL)
		apr_thread_mutex_lock(globals.mutex);

	num = globals.speech_channel_number;

	if (globals.speech_channel_number == APR_UINT32_MAX)
		globals.speech_channel_number = 0;
	else
		globals.speech_channel_number++;

	if (globals.mutex != NULL)
		apr_thread_mutex_unlock(globals.mutex);

	return num;
}

/* Inspect text to determine if its first non-whitespace text matches "match". */
static int text_starts_with(const char *text, const char *match)
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

/* Create a parameter ID. */
static unimrcp_param_id_t *unimrcp_param_id_create(int id, apr_pool_t *pool)
{   
	unimrcp_param_id_t *param = (unimrcp_param_id_t *)apr_palloc(pool, sizeof(unimrcp_param_id_t));

	if (param != NULL)
		param->id = id;

	return param;
}



/* --- AUDIO BUFFER --- */

static int audio_buffer_create(audio_buffer_t **buffer, apr_size_t max_len)
{
	apr_pool_t *pool;
	audio_buffer_t *new_buffer;

	if (buffer == NULL)
		return -1;
	else
		*buffer = NULL;

	if ((pool = apt_pool_create()) == NULL)
		return -1;

	if ((max_len > 0) && ((new_buffer = apr_palloc(pool, sizeof(audio_buffer_t))) != NULL) && ((new_buffer->data = apr_palloc(pool, max_len)) != NULL)) {
		new_buffer->datalen = max_len;
		new_buffer->pool = pool;
		new_buffer->used = 0;
		*buffer = new_buffer;
		return 0;
	}

	apr_pool_destroy(pool);
	return -1;
}

static void audio_buffer_destroy(audio_buffer_t *buffer)
{
	if (buffer != NULL) {
		if (buffer->pool != NULL) {
			apr_pool_destroy(buffer->pool);
			buffer->pool = NULL;
		}

		buffer->data = NULL;
		buffer->datalen = 0;
		buffer->used = 0;
	}
}

static apr_size_t audio_buffer_inuse(audio_buffer_t *buffer)
{
	if (buffer != NULL)
		return buffer->used;
	else
		return 0;
}

static apr_size_t audio_buffer_read(audio_buffer_t *buffer, void *data, apr_size_t datalen)
{
	apr_size_t reading = 0;

	if ((buffer == NULL) || (buffer->data == NULL) || (data == NULL) || (datalen == 0))
		return 0;

	if (buffer->used < 1) {
		buffer->used = 0;
		return 0;
	} else if (buffer->used >= datalen)
		reading = datalen;
	else
		reading = buffer->used;

	memcpy(data, buffer->data, reading);
	buffer->used = buffer->used - reading;
	memmove(buffer->data, buffer->data + reading, buffer->used);

	return reading;
}

static apr_size_t audio_buffer_write(audio_buffer_t *buffer, const void *data, apr_size_t datalen)
{
	apr_size_t freespace;

	if ((buffer == NULL) || (buffer->data == NULL) || (data == NULL))
		return 0;

	if (datalen == 0)
		return buffer->used;

	freespace = buffer->datalen - buffer->used;

	if (freespace < datalen)
		return 0;

	memcpy(buffer->data + buffer->used, data, datalen);
	buffer->used = buffer->used + datalen;

	return buffer->used;
}

static void audio_buffer_zero(audio_buffer_t *buffer)
{
	if (buffer != NULL)
		buffer->used = 0;
}



/* --- AUDIO QUEUE --- */

/* Empty the queue. */
static int audio_queue_clear(audio_queue_t *queue)
{
	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	if (queue->buffer != NULL)
		audio_buffer_zero(queue->buffer);

	if (queue->cond != NULL)
		apr_thread_cond_signal(queue->cond);

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return 0;
}

/* Destroy the audio queue. */
static int audio_queue_destroy(audio_queue_t *queue)
{
	if (queue != NULL) {
		char *name = queue->name;
		if ((name == NULL) || (strlen(name) == 0))
			name = "";

		if (queue->buffer != NULL) {
			audio_buffer_zero(queue->buffer);
			audio_buffer_destroy(queue->buffer);
			queue->buffer = NULL;
		}

		if (queue->cond != NULL) {
			if (apr_thread_cond_destroy(queue->cond) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) unable to destroy audio queue condition variable\n", name);

			queue->cond = NULL;
		}

		if (queue->mutex != NULL) {
			if (apr_thread_mutex_destroy(queue->mutex) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) unable to destroy audio queue mutex\n", name);

			queue->mutex = NULL;
		}

		if (queue->pool != NULL) {
			apr_pool_destroy(queue->pool);
			queue->pool = NULL;
		}

		queue->name = NULL;
		queue->read_bytes = 0;
		queue->waiting = 0;
		queue->write_bytes = 0;

		ast_log(LOG_DEBUG, "(%s) audio queue destroyed\n", name);
	}

	return 0;
}

/* Create the audio queue. */
static int audio_queue_create(audio_queue_t **audio_queue, const char *name)
{
	int status = 0;
	audio_queue_t *laudio_queue = NULL;
	char *lname = "";
	apr_pool_t *pool;

	if (audio_queue == NULL)
		return -1;
	else
		*audio_queue = NULL;

	if ((pool = apt_pool_create()) == NULL)
		return -1;

	if ((name == NULL) || (strlen(name) == 0))
		lname = "";
	else
		lname = apr_pstrdup(pool, name);
	if (lname == NULL)
		lname = "";

	if ((laudio_queue = (audio_queue_t *)apr_palloc(pool, sizeof(audio_queue_t))) == NULL) {
		ast_log(LOG_ERROR, "(%s) unable to create audio queue\n", lname);
		return -1;
	} else {
		laudio_queue->buffer = NULL;
		laudio_queue->cond = NULL;
		laudio_queue->mutex = NULL;
		laudio_queue->name = lname;
		laudio_queue->pool = pool;
		laudio_queue->read_bytes = 0;
		laudio_queue->waiting = 0;
		laudio_queue->write_bytes = 0;

		if (audio_buffer_create(&laudio_queue->buffer, AUDIO_QUEUE_SIZE) != 0) {
			ast_log(LOG_ERROR, "(%s) unable to create audio queue buffer\n", laudio_queue->name);
			status = -1;
		} else if (apr_thread_mutex_create(&laudio_queue->mutex, APR_THREAD_MUTEX_UNNESTED, pool) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) unable to create audio queue mutex\n", laudio_queue->name);
			status = -1;
		} else if (apr_thread_cond_create(&laudio_queue->cond, pool) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) unable to create audio queue condition variable\n", laudio_queue->name);
			status = -1;
		} else {
			*audio_queue = laudio_queue;
			ast_log(LOG_DEBUG, "(%s) audio queue created\n", laudio_queue->name);
		}
	}

	if (status != 0)
		audio_queue_destroy(laudio_queue);

	return status;
}

/* Read from the audio queue. */
static apr_status_t audio_queue_read(audio_queue_t *queue, void *data, apr_size_t *data_len, int block)
{
	apr_size_t requested;
	int status = 0;

	if ((queue == NULL) || (data == NULL) || (data_len == NULL))
		return -1;
	else
		requested = *data_len;

	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	/* Wait for data, if allowed. */
	if (block != 0) {
		if (audio_buffer_inuse(queue->buffer) < requested) {
			queue->waiting = requested;

			if ((queue->mutex != NULL) && (queue->cond != NULL))
				apr_thread_cond_timedwait(queue->cond, queue->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

		}

		queue->waiting = 0;
	}

	if (audio_buffer_inuse(queue->buffer) < requested)
		requested = audio_buffer_inuse(queue->buffer);

	if (requested == 0) {
		*data_len = 0;
		status = -1;
	} else {
		/* Read the data. */
		*data_len = audio_buffer_read(queue->buffer, data, requested);
		queue->read_bytes = queue->read_bytes + *data_len;
	}

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return status;
}

/* Write to the audio queue. */
static int audio_queue_write(audio_queue_t *queue, void *data, apr_size_t *data_len)
{
	int status = 0;

	if ((queue == NULL) || (data == NULL) || (data_len == NULL))
		return -1;

	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	if (audio_buffer_write(queue->buffer, data, *data_len) > 0) {
		queue->write_bytes = queue->write_bytes + *data_len;

		if (queue->waiting <= audio_buffer_inuse(queue->buffer)) {
			if (queue->cond != NULL)
				apr_thread_cond_signal(queue->cond);
		}
	} else {
		ast_log(LOG_WARNING, "(%s) audio queue overflow!\n", queue->name);
		*data_len = 0;
		status = -1;
	}

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return status;
}



/* --- PROFILE FUNCTIONS --- */

/* Create a profile. */
static int profile_create(profile_t **profile, const char *name, const char *version, apr_pool_t *pool)
{
	int res = 0;
	profile_t *lprofile = NULL;

	if (profile == NULL)
		return -1;
	else
		*profile = NULL;

	if (pool == NULL)
		return -1;
		
	lprofile = (profile_t *)apr_palloc(pool, sizeof(profile_t));
	if ((lprofile != NULL) && (name != NULL) && (version != NULL)) {
		if ((lprofile->cfg = apr_hash_make(pool)) != NULL) {
			lprofile->name = apr_pstrdup(pool, name);
			lprofile->version = apr_pstrdup(pool, version);
			lprofile->srgs_mime_type = "application/srgs";
			lprofile->srgs_xml_mime_type = "application/srgs+xml";
			lprofile->gsl_mime_type = "application/x-nuance-gsl";
			lprofile->jsgf_mime_type = "application/x-jsgf";
			*profile = lprofile;
		} else
			res = -1;
	} else
		res = -1;

	return res;
}

/* Set specific profile configuration. */
static int process_profile_config(profile_t *profile, const char *param, const char *val, apr_pool_t *pool)
{
	int mine = 1;

	if ((profile == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
		return mine;

	/* Do nothing as it was already set with profile_create. */
	if (strcasecmp(param, "version") == 0)
		return mine;

	if (strcasecmp(param, "jsgf-mime-type") == 0)
		profile->jsgf_mime_type = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "gsl-mime-type") == 0)
		profile->gsl_mime_type = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "srgs-xml-mime-type") == 0)
		profile->srgs_xml_mime_type = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "srgs-mime-type") == 0)
		profile->srgs_mime_type = apr_pstrdup(pool, val);
	else
		mine = 0;

	return mine;
}

/* Set RTP config struct with param, val pair. */
#if UNI_VERSION_AT_LEAST(0,10,0)
static int process_rtp_config(mrcp_client_t *client, mpf_rtp_config_t *rtp_config, mpf_rtp_settings_t *rtp_settings, const char *param, const char *val, apr_pool_t *pool)
#else
static int process_rtp_config(mrcp_client_t *client, mpf_rtp_config_t *rtp_config, const char *param, const char *val, apr_pool_t *pool)
#endif
{
	int mine = 1;

	#if UNI_VERSION_AT_LEAST(0,10,0)
	if ((client == NULL) || (rtp_config == NULL) || (rtp_settings == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
	#else
	if ((client == NULL) || (rtp_config == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
	#endif
		return mine;

	if (strcasecmp(param, "rtp-ip") == 0)
		apt_string_set(&rtp_config->ip, ip_addr_get(val, pool));
	else if (strcasecmp(param, "rtp-ext-ip") == 0)
		apt_string_set(&rtp_config->ext_ip, ip_addr_get(val, pool));
	else if (strcasecmp(param, "rtp-port-min") == 0)
		rtp_config->rtp_port_min = (apr_port_t)atol(val);
	else if (strcasecmp(param, "rtp-port-max") == 0)
		rtp_config->rtp_port_max = (apr_port_t)atol(val);
	else if (strcasecmp(param, "playout-delay") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->jb_config.initial_playout_delay = atol(val);
		#else
		rtp_config->jb_config.initial_playout_delay = atol(val);
		#endif
	else if (strcasecmp(param, "min-playout-delay") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->jb_config.min_playout_delay = atol(val);
		#else
		rtp_config->jb_config.min_playout_delay = atol(val);
		#endif
	else if (strcasecmp(param, "max-playout-delay") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->jb_config.max_playout_delay = atol(val);
		#else
		rtp_config->jb_config.min_playout_delay = atol(val);
		#endif
	else if (strcasecmp(param, "codecs") == 0) {
		/* Make sure that /etc/mrcp.conf contains the desired codec first in the codecs parameter. */

		const mpf_codec_manager_t *codec_manager = mrcp_client_codec_manager_get(client);

		if (codec_manager != NULL) {
			#if UNI_VERSION_AT_LEAST(0,10,0)
			if (!mpf_codec_manager_codec_list_load(codec_manager, &rtp_settings->codec_list, val, pool))
			#else
			if (!mpf_codec_manager_codec_list_load(codec_manager, &rtp_config->codec_list, val, pool))
			#endif
				ast_log(LOG_WARNING, "Unable to load codecs\n");
		}
	} else if (strcasecmp(param, "ptime") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->ptime = (apr_uint16_t)atol(val);
		#else
		rtp_config->ptime = (apr_uint16_t)atol(val);
		#endif
#if UNI_VERSION_AT_LEAST(0,8,0)
	else if (strcasecmp(param, "rtcp") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->rtcp = atoi(val);
		#else
		rtp_config->rtcp = atoi(val);
		#endif
	else if  (strcasecmp(param, "rtcp-bye") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->rtcp_bye_policy = atoi(val);
		#else
		rtp_config->rtcp_bye_policy = atoi(val);
		#endif
	else if (strcasecmp(param, "rtcp-tx-interval") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->rtcp_tx_interval = (apr_uint16_t)atoi(val);
		#else
		rtp_config->rtcp_tx_interval = (apr_uint16_t)atoi(val);
		#endif
	else if (strcasecmp(param, "rtcp-rx-resolution") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		rtp_settings->rtcp_rx_resolution = (apr_uint16_t)atol(val);
		#else
		rtp_config->rtcp_rx_resolution = (apr_uint16_t)atol(val);
		#endif
#endif
	else
		mine = 0;

	return mine;
}

/* Set RTSP client config struct with param, val pair. */
#if UNI_VERSION_AT_LEAST(0,10,0)
static int process_mrcpv1_config(rtsp_client_config_t *config, mrcp_sig_settings_t *sig_settings, const char *param, const char *val, apr_pool_t *pool)
#else
static int process_mrcpv1_config(rtsp_client_config_t *config, const char *param, const char *val, apr_pool_t *pool)
#endif
{
	int mine = 1;

	#if UNI_VERSION_AT_LEAST(0,10,0)
	if ((config == NULL) || (param == NULL) || (sig_settings == NULL) || (val == NULL) || (pool == NULL))
	#else
	if ((config == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
	#endif
		return mine;

	if (strcasecmp(param, "server-ip") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->server_ip = ip_addr_get(val, pool);
		#else
		config->server_ip = ip_addr_get(val, pool);
		#endif
	else if (strcasecmp(param, "server-port") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->server_port = (apr_port_t)atol(val);
		#else
		config->server_port = (apr_port_t)atol(val);
		#endif
	else if (strcasecmp(param, "resource-location") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->resource_location = apr_pstrdup(pool, val);
		#else
		config->resource_location = apr_pstrdup(pool, val);
		#endif
	else if (strcasecmp(param, "sdp-origin") == 0)
		config->origin = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "max-connection-count") == 0)
		config->max_connection_count = atol(val);
	else if (strcasecmp(param, "force-destination") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->force_destination = atoi(val);
		#else
		config->force_destination = atoi(val);
		#endif
	else if ((strcasecmp(param, "speechsynth") == 0) || (strcasecmp(param, "speechrecog") == 0))
		#if UNI_VERSION_AT_LEAST(0,10,0)
		apr_table_set(sig_settings->resource_map, param, val);
		#else
		apr_table_set(config->resource_map, param, val);
		#endif
	else
		mine = 0;

	return mine;
}

/* Set SofiaSIP client config struct with param, val pair. */
#if UNI_VERSION_AT_LEAST(0,10,0)
static int process_mrcpv2_config(mrcp_sofia_client_config_t *config, mrcp_sig_settings_t *sig_settings, const char *param, const char *val, apr_pool_t *pool)
#else
static int process_mrcpv2_config(mrcp_sofia_client_config_t *config, const char *param, const char *val, apr_pool_t *pool)
#endif
{
	int mine = 1;

	#if UNI_VERSION_AT_LEAST(0,10,0)
	if ((config == NULL) || (param == NULL) || (sig_settings == NULL) || (val == NULL) || (pool == NULL))
	#else
	if ((config == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
	#endif
		return mine;

	if (strcasecmp(param, "client-ip") == 0)
		config->local_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param,"client-ext-ip") == 0)
		config->ext_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param,"client-port") == 0)
		config->local_port = (apr_port_t)atol(val);
	else if (strcasecmp(param, "server-ip") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->server_ip = ip_addr_get(val, pool);
		#else
		config->remote_ip = ip_addr_get(val, pool);
		#endif
	else if (strcasecmp(param, "server-port") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->server_port = (apr_port_t)atol(val);
		#else
		config->remote_port = (apr_port_t)atol(val);
		#endif
	else if (strcasecmp(param, "server-username") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->user_name = apr_pstrdup(pool, val);
		#else
		config->remote_user_name = apr_pstrdup(pool, val);
		#endif
	else if (strcasecmp(param, "force-destination") == 0)
		#if UNI_VERSION_AT_LEAST(0,10,0)
		sig_settings->force_destination = atoi(val);
		#else
		config->force_destination = atoi(val);
		#endif
	else if (strcasecmp(param, "sip-transport") == 0)
		config->transport = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "ua-name") == 0)
		config->user_agent_name = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "sdp-origin") == 0)
		config->origin = apr_pstrdup(pool, val);
	else
		mine = 0;

	return mine;
}



/* --- MRCP CLIENT --- */

/* Create an MRCP client.
 *
 * Some code and ideas borrowed from unimrcp-client.c
 * Please check $unimrcp_dir$/platforms/libunimrcp-client/src/unimrcp_client.c
 * when upgrading the UniMRCP library to ensure nothing new needs to be set up.
 */
static mrcp_client_t *mod_unimrcp_client_create(apr_pool_t *mod_pool)
{
	mrcp_client_t *client = NULL;
	apt_dir_layout_t *dir_layout = NULL;
	apr_pool_t *pool = NULL;
	mrcp_resource_factory_t *resource_factory = NULL;
	mpf_codec_manager_t *codec_manager = NULL;
	apr_size_t max_connection_count = 0;
	apt_bool_t offer_new_connection = FALSE;
	mrcp_connection_agent_t *connection_agent = NULL;
	mpf_engine_t *media_engine = NULL;

	if (!globals.profiles) {
		ast_log(LOG_ERROR, "Profiles hash is NULL\n");
		return NULL;
	}

	/* Create the client. */
	if ((dir_layout = apt_default_dir_layout_create("../", mod_pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create directory layout\n");
		return NULL;
	}

	if ((client = mrcp_client_create(dir_layout)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create MRCP client stack\n");
		return NULL;
	}

	if ((pool = mrcp_client_memory_pool_get(client)) == NULL) {
		ast_log(LOG_ERROR, "MRCP client pool is NULL\n");
		return NULL;
	}

#if UNI_VERSION_AT_LEAST(0,8,0)
	mrcp_resource_loader_t *resource_loader = mrcp_resource_loader_create(FALSE, pool);
	if (resource_loader == NULL) {
		ast_log(LOG_ERROR, "Unable to create MRCP resource loader.\n");
		return NULL;
	}

	apt_str_t resource_class_synth;
	apt_str_t resource_class_recog;
	apt_string_set(&resource_class_synth, "speechsynth");
	apt_string_set(&resource_class_recog, "speechrecog");
	mrcp_resource_load(resource_loader, &resource_class_synth);
	mrcp_resource_load(resource_loader, &resource_class_recog);

	resource_factory = mrcp_resource_factory_get(resource_loader);

	if (!mrcp_client_resource_factory_register(client, resource_factory))
		ast_log(LOG_WARNING, "Unable to register MRCP client resource factory\n");
#else
	/* This code works with the official UniMRCP 0.8.0 release, but in the
	 * trunk of UniMRCP the mrcp_default_factory.h has been removed.
	 */
	if ((resource_factory = mrcp_default_factory_create(pool)) != NULL) {
		if (!mrcp_client_resource_factory_register(client, resource_factory))
			ast_log(LOG_WARNING, "Unable to register MRCP client resource factory\n");
	}
#endif

	if ((codec_manager = mpf_engine_codec_manager_create(pool)) != NULL) {
		if (!mrcp_client_codec_manager_register(client, codec_manager))
			ast_log(LOG_WARNING, "Unable to register MRCP client codec manager\n");
	}

	/* Set up MRCPv2 connection agent that will be shared with all profiles. */
	if ((globals.unimrcp_max_connection_count != NULL) && (strlen(globals.unimrcp_max_connection_count) > 0))
		max_connection_count = atoi(globals.unimrcp_max_connection_count);

	if (max_connection_count <= 0)
		max_connection_count = 120;

	if ((globals.unimrcp_offer_new_connection != NULL) && (strlen(globals.unimrcp_offer_new_connection) > 0))
		offer_new_connection = (strcasecmp("true", globals.unimrcp_offer_new_connection) == 0);

	if (offer_new_connection < 0)
		offer_new_connection = 1;

	if ((connection_agent = mrcp_client_connection_agent_create(max_connection_count, offer_new_connection, pool)) != NULL) {
		if (connection_agent != NULL) {
 			if (globals.unimrcp_rx_buffer_size != NULL) {
				apr_size_t rx_buffer_size = (apr_size_t)atol(globals.unimrcp_rx_buffer_size);
				if (rx_buffer_size > 0) {
	 				mrcp_client_connection_rx_size_set(connection_agent, rx_buffer_size);
				}
 			}
 			if (globals.unimrcp_tx_buffer_size != NULL) {
				apr_size_t tx_buffer_size = (apr_size_t)atol(globals.unimrcp_tx_buffer_size);
				if (tx_buffer_size > 0) {
	 				mrcp_client_connection_tx_size_set(connection_agent, tx_buffer_size);
				}
 			}
 		}

		if (!mrcp_client_connection_agent_register(client, connection_agent, "MRCPv2ConnectionAgent"))
			ast_log(LOG_WARNING, "Unable to register MRCP client connection agent\n");
	}

	/* Set up the media engine that will be shared with all profiles. */
	if ((media_engine = mpf_engine_create(pool)) != NULL) {
		unsigned long realtime_rate = 1;

		if (!mpf_engine_scheduler_rate_set(media_engine, realtime_rate))
			ast_log(LOG_WARNING, "Unable to set scheduler rate for MRCP client media engine\n");

		if (!mrcp_client_media_engine_register(client, media_engine, "MediaEngine"))
			ast_log(LOG_WARNING, "Unable to register MRCP client media engine\n");
	}

	if (globals.profiles) {
		apr_hash_index_t *hi;

		for (hi = apr_hash_first(NULL, globals.profiles); hi; hi = apr_hash_next(hi)) {
			const char *k;
			profile_t *v;
			const void *key;
			void *val;

			apr_hash_this(hi, &key, NULL, &val);

			k = (const char *)key;
			v = (profile_t *)val;

			if (v != NULL) {
				ast_log(LOG_DEBUG, "Processing profile %s:%s\n", k, v->version);

				/* A profile is a signaling agent + termination factory + media engine + connection agent (MRCPv2 only). */
				mrcp_sig_agent_t *agent = NULL;
				mpf_termination_factory_t *termination_factory = NULL;
				mrcp_profile_t * mprofile = NULL;
				mpf_rtp_config_t *rtp_config = NULL;
				#if UNI_VERSION_AT_LEAST(0,10,0)
				mpf_rtp_settings_t *rtp_settings = mpf_rtp_settings_alloc(pool);
				mrcp_sig_settings_t *sig_settings = mrcp_signaling_settings_alloc(pool);
				#endif
				profile_t *mod_profile = NULL;

				/* Get profile attributes. */
				const char *name = apr_pstrdup(mod_pool, k);
				const char *version = apr_pstrdup(mod_pool, v->version);

				if ((name == NULL) || (strlen(name) == 0) || (version == NULL) || (strlen(version) == 0)) {
					ast_log(LOG_ERROR, "Profile %s missing name or version attribute\n", k);
					return NULL;
				}

				/* Create RTP config, common to MRCPv1 and MRCPv2. */
				#if UNI_VERSION_AT_LEAST(0,10,0)
				if ((rtp_config = mpf_rtp_config_alloc(pool)) == NULL) {
				#else
				if ((rtp_config = mpf_rtp_config_create(pool)) == NULL) {
				#endif
					ast_log(LOG_ERROR, "Unable to create RTP configuration\n");
					return NULL;
				}

				rtp_config->rtp_port_min = DEFAULT_RTP_PORT_MIN;
				rtp_config->rtp_port_max = DEFAULT_RTP_PORT_MAX;
				apt_string_set(&rtp_config->ip, DEFAULT_LOCAL_IP_ADDRESS);

				if (strcmp("1", version) == 0) {
					/* MRCPv1 configuration. */
					rtsp_client_config_t *config = mrcp_unirtsp_client_config_alloc(pool);

					if (config == NULL) {
						ast_log(LOG_ERROR, "Unable to create RTSP configuration\n");
						return NULL;
					}

					config->origin = DEFAULT_SDP_ORIGIN;
					#if UNI_VERSION_AT_LEAST(0,10,0)
					sig_settings->resource_location = DEFAULT_RESOURCE_LOCATION;
					#else
					config->resource_location = DEFAULT_RESOURCE_LOCATION;
					#endif

					ast_log(LOG_DEBUG, "Loading MRCPv1 profile: %s\n", name);

					apr_hash_index_t *hicfg;

					for (hicfg = apr_hash_first(NULL, v->cfg); hicfg; hicfg = apr_hash_next(hicfg)) {
						const char *param_name;
						const char *param_value;
						const void *keyc;
						void *valc;

						apr_hash_this(hicfg, &keyc, NULL, &valc);

						param_name = (const char *)keyc;
						param_value = (const char *)valc;

						if ((param_name != NULL) && (param_value != NULL)) {
							if (strlen(param_name) == 0) {
								ast_log(LOG_ERROR, "Missing parameter name\n");
								return NULL;
							}

							ast_log(LOG_DEBUG, "Loading parameter %s:%s\n", param_name, param_value);

							#if UNI_VERSION_AT_LEAST(0,10,0)
							if ((!process_mrcpv1_config(config, sig_settings, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, rtp_settings, param_name, param_value, pool)) &&
							#else
							if ((!process_mrcpv1_config(config, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, param_name, param_value, pool)) &&
							#endif
								(!process_profile_config(mod_profile, param_name, param_value, mod_pool))) {
								ast_log(LOG_WARNING, "Unknown parameter %s\n", param_name);
							}
						}
					}

					agent = mrcp_unirtsp_client_agent_create(config, pool);
				} else if (strcmp("2", version) == 0) {
					/* MRCPv2 configuration. */
					mrcp_sofia_client_config_t *config = mrcp_sofiasip_client_config_alloc(pool);
	
					if (config == NULL) {
						ast_log(LOG_ERROR, "Unable to create SIP configuration\n");
						return NULL;
					}

					config->local_ip = DEFAULT_LOCAL_IP_ADDRESS;
					config->local_port = DEFAULT_SIP_LOCAL_PORT;
					#if UNI_VERSION_AT_LEAST(0,10,0)
					sig_settings->server_ip = DEFAULT_REMOTE_IP_ADDRESS;
					sig_settings->server_port = DEFAULT_SIP_REMOTE_PORT;
					#else
					config->remote_ip = DEFAULT_REMOTE_IP_ADDRESS;
					config->remote_port = DEFAULT_SIP_REMOTE_PORT;
					#endif
					config->ext_ip = NULL;
					config->user_agent_name = DEFAULT_SOFIASIP_UA_NAME;
					config->origin = DEFAULT_SDP_ORIGIN;

					ast_log(LOG_DEBUG, "Loading MRCPv2 profile: %s\n", name);

					apr_hash_index_t *hicfg;

					for (hicfg = apr_hash_first(NULL, v->cfg); hicfg; hicfg = apr_hash_next(hicfg)) {
						const char *param_name;
						const char *param_value;
						const void *keyc;
						void *valc;

						apr_hash_this(hicfg, &keyc, NULL, &valc);

						param_name = (const char *)keyc;
						param_value = (const char *)valc;

						if ((param_name != NULL) && (param_value != NULL)) {
							if (strlen(param_name) == 0) {
								ast_log(LOG_ERROR, "Missing parameter name\n");
								return NULL;
							}

							ast_log(LOG_DEBUG, "Loading parameter %s:%s\n", param_name, param_value);

							#if UNI_VERSION_AT_LEAST(0,10,0)
							if ((!process_mrcpv2_config(config, sig_settings, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, rtp_settings, param_name, param_value, pool)) &&
							#else
							if ((!process_mrcpv2_config(config, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, param_name, param_value, pool)) &&
							#endif
								(!process_profile_config(mod_profile, param_name, param_value, mod_pool))) {
								ast_log(LOG_WARNING, "Unknown parameter %s\n", param_name);
							}
						}
					}

					agent = mrcp_sofiasip_client_agent_create(config, pool);
				} else {
					ast_log(LOG_ERROR, "Version must be either \"1\" or \"2\"\n");
					return NULL;
				}

				if ((termination_factory = mpf_rtp_termination_factory_create(rtp_config, pool)) != NULL)
					mrcp_client_rtp_factory_register(client, termination_factory, name);

				#if UNI_VERSION_AT_LEAST(0,10,0)
				if (rtp_settings != NULL)
					mrcp_client_rtp_settings_register(client, rtp_settings, "RTP-Settings");

				if (sig_settings != NULL)
					mrcp_client_signaling_settings_register(client, sig_settings, "Signalling-Settings");
				#endif

				if (agent != NULL)
					mrcp_client_signaling_agent_register(client, agent, name);

				/* Create the profile and register it. */
				#if UNI_VERSION_AT_LEAST(0,10,0)
				if ((mprofile = mrcp_client_profile_create(NULL, agent, connection_agent, media_engine, termination_factory, rtp_settings, sig_settings, pool)) != NULL) {
				#else
				if ((mprofile = mrcp_client_profile_create(NULL, agent, connection_agent, media_engine, termination_factory, pool)) != NULL)
				#endif
					if (!mrcp_client_profile_register(client, mprofile, name))
						ast_log(LOG_WARNING, "Unable to register MRCP client profile\n");
				}
			}
		}
	}

	return client;
}



/* --- MRCP SPEECH CHANNEL --- */

/* Convert channel state to string. */
static const char *speech_channel_state_to_string(speech_channel_state_t state)
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
static const char *speech_channel_type_to_string(speech_channel_type_t type)
{
	switch (type) {
		case SPEECH_CHANNEL_SYNTHESIZER: return "SYNTHESIZER";
		case SPEECH_CHANNEL_RECOGNIZER: return "RECOGNIZER";
		default: return "UNKNOWN";
	}
}

/* Set parameter. */
static int speech_channel_set_param(speech_channel_t *schannel, const char *param, const char *val)
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
static void speech_channel_set_state_unlocked(speech_channel_t *schannel, speech_channel_state_t state)
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
static void speech_channel_set_state(speech_channel_t *schannel, speech_channel_state_t state)
{
	if (schannel != NULL) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		speech_channel_set_state_unlocked(schannel, state);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	}
}

/* Send BARGE-IN-OCCURED. */
static int speech_channel_bargeinoccured(speech_channel_t *schannel) {
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
				ast_log(LOG_WARNING, "(%s) [speech_channel_bargeinoccured] Failed to send BARGE_IN_OCCURRED message\n", schannel->name);
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

static int speech_channel_create(speech_channel_t **schannel, const char *name, speech_channel_type_t type, my_mrcp_application_t *app, const char *codec, apr_uint16_t rate, struct ast_channel *chan)
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
static mpf_termination_t *speech_channel_create_mpf_termination(speech_channel_t *schannel)
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
					schannel->unimrcp_session,                        /* session, termination belongs to */
					&schannel->application->audio_stream_vtable,      /* virtual methods table of audio stream */
                    capabilities,                                     /* capabilities of audio stream */
                    schannel);                                        /* object to associate */

	if (termination == NULL)
		ast_log(LOG_ERROR, "(%s) Unable to create termination\n", schannel->name);

	return termination;
}
#else
static mpf_termination_t *speech_channel_create_mpf_termination(speech_channel_t *schannel)
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
static int speech_channel_destroy(speech_channel_t *schannel)
{
	if (schannel != NULL) {
		ast_log(LOG_DEBUG, "Destroying speech channel: Name=%s, Type=%s, Codec=%s, Rate=%u\n", schannel->name, speech_channel_type_to_string(schannel->type), schannel->codec, schannel->rate);

		if (schannel->mutex)
			apr_thread_mutex_lock(schannel->mutex);

		/* Destroy the channel and session if not already done. */
		if (schannel->state != SPEECH_CHANNEL_CLOSED) {
			if ((schannel->unimrcp_session != NULL) && (schannel->unimrcp_channel != NULL)) {
				if (!mrcp_application_session_terminate(schannel->unimrcp_session))
					ast_log(LOG_WARNING, "(%s) %s unable to terminate application session\n", schannel->name, speech_channel_type_to_string(schannel->type));

				/* if (!mrcp_application_channel_remove(schannel->unimrcp_session, schannel->unimrcp_channel))
					ast_log(LOG_WARNING, "(%s) Unable to remove channel from application\n", schannel->name);
				 */
			}

			if (schannel->cond != NULL)
				apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);
		}

		if (schannel->state != SPEECH_CHANNEL_CLOSED) {
			ast_log(LOG_ERROR, "(%s) Failed to destroy channel.  Continuing\n", schannel->name);
		}

		if (schannel->dtmf_generator != NULL) {
			ast_log(LOG_NOTICE, "(%s) DTMF generator destroyed\n", schannel->name);
                        mpf_dtmf_generator_destroy(schannel->dtmf_generator);
                        schannel->dtmf_generator = NULL;
                }

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		if (schannel->audio_queue != NULL) {
			if (audio_queue_destroy(schannel->audio_queue) != 0)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel audio queue\n",schannel->name);
			else
				ast_log(LOG_NOTICE, "(%s) Audio queue destroyed\n", schannel->name);
		}

		if (schannel->params != NULL)
			apr_hash_clear(schannel->params);

		if (schannel->cond != NULL) {
			if (apr_thread_cond_destroy(schannel->cond) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel condition variable\n", schannel->name);
		}

		if (schannel->mutex != NULL) {
			if (apr_thread_mutex_destroy(schannel->mutex) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy channel condition variable\n", schannel->name);
		}

		if (schannel->pool != NULL)
			apr_pool_destroy(schannel->pool);

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
	} else {
		ast_log(LOG_ERROR, "Speech channel structure pointer is NULL\n");
		return -1;
	}
	
	ast_log(LOG_DEBUG, "Destroyed speech channel complete\n");

	return 0;
}

/* Open the speech channel. */
static int speech_channel_open(speech_channel_t *schannel, profile_t *profile)
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
static int speech_channel_stop(speech_channel_t *schannel)
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
static int speech_channel_read(speech_channel_t *schannel, void *data, apr_size_t *len, int block)
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
static int speech_channel_write(speech_channel_t *schannel, void *data, apr_size_t *len)
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
			if (schannel->stream != NULL)
				
				schannel->dtmf_generator = mpf_dtmf_generator_create(schannel->stream, schannel->pool);
				/* schannel->dtmf_generator = mpf_dtmf_generator_create_ex(schannel->stream, MPF_DTMF_GENERATOR_OUTBAND, 70, 50, schannel->pool); */

				if (schannel->dtmf_generator != NULL)
					ast_log(LOG_NOTICE, "(%s) DTMF generator created\n", schannel->name);
				else
					ast_log(LOG_NOTICE, "(%s) Unable to create DTMF generator\n", schannel->name);

#if UNI_VERSION_AT_LEAST(0,8,0)
			char codec_name[60] = { 0 };
			const mpf_codec_descriptor_t *descriptor;

			/* What sample rate did we negotiate? */
			if (schannel->type == SPEECH_CHANNEL_SYNTHESIZER)
				descriptor = mrcp_application_sink_descriptor_get(channel);
			else
				descriptor = mrcp_application_source_descriptor_get(channel);

			schannel->rate = descriptor->sampling_rate;

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
		return mrcp_application_message_dispatch(&globals.synth.dispatcher, app_message);
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

/* Setup TTS. */
static int synth_load(apr_pool_t *pool)
{
	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	/* Create the synthesizer application and link its callbacks to UniMRCP */
	if ((globals.synth.app = mrcp_application_create(synth_message_handler, (void *)0, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create synthesizer MRCP application\n");
		return -1;
	}

	globals.synth.dispatcher.on_session_update = NULL;
	globals.synth.dispatcher.on_session_terminate = speech_on_session_terminate;
	globals.synth.dispatcher.on_channel_add = speech_on_channel_add;
	globals.synth.dispatcher.on_channel_remove = speech_on_channel_remove;
	globals.synth.dispatcher.on_message_receive = synth_on_message_receive;
	globals.synth.audio_stream_vtable.destroy = NULL;
	globals.synth.audio_stream_vtable.open_rx = NULL;
	globals.synth.audio_stream_vtable.close_rx = NULL;
	globals.synth.audio_stream_vtable.read_frame = NULL;
	globals.synth.audio_stream_vtable.open_tx = NULL;
	globals.synth.audio_stream_vtable.close_tx =  NULL;
	globals.synth.audio_stream_vtable.write_frame = synth_stream_write;

	if (!mrcp_client_application_register(globals.mrcp_client, globals.synth.app, app_synth)) {
		ast_log(LOG_ERROR, "Unable to register synthesizer MRCP application\n");
		if (!mrcp_application_destroy(globals.synth.app))
			ast_log(LOG_WARNING, "Unable to destroy synthesizer MRCP application\n");
		globals.synth.app = NULL;
		return -1;
	}

	/* Create a hash for the synthesizer parameter map. */
	globals.synth.param_id_map = apr_hash_make(pool);

	if (globals.synth.param_id_map != NULL) {
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "jump-size"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_JUMP_SIZE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "kill-on-barge-in"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_KILL_ON_BARGE_IN, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "speaker-profile"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAKER_PROFILE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "completion-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_COMPLETION_CAUSE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "completion-reason"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_COMPLETION_REASON, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "voice-gender"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_GENDER, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "voice-age"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_AGE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "voice-variant"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_VARIANT, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "voice-name"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_VOICE_NAME, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "prosody-volume"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_PROSODY_VOLUME, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "prosody-rate"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_PROSODY_RATE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "speech-marker"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEECH_MARKER, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "speech-language"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEECH_LANGUAGE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "fetch-hint"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FETCH_HINT, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "audio-fetch-hint"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_AUDIO_FETCH_HINT, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "failed-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FAILED_URI, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "failed-uri-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_FAILED_URI_CAUSE, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "speak-restart"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAK_RESTART, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "speak-length"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_SPEAK_LENGTH, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "load-lexicon"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_LOAD_LEXICON, pool));
		apr_hash_set(globals.synth.param_id_map, apr_pstrdup(pool, "lexicon-search-order"), APR_HASH_KEY_STRING, unimrcp_param_id_create(SYNTHESIZER_HEADER_LEXICON_SEARCH_ORDER, pool));
	}

	return 0;
}

/* Shutdown TTS. */
static void synth_shutdown(void)
{
	/* Clear parameter ID map. */
	if (globals.synth.param_id_map != NULL)
		apr_hash_clear(globals.synth.param_id_map);
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

				if (schannel->application->param_id_map != NULL)
					id = (unimrcp_param_id_t *)apr_hash_get(schannel->application->param_id_map, param_name, APR_HASH_KEY_STRING);

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
		if (text_starts_with(text, XML_ID) || text_starts_with(text, SSML_ID))
			apt_string_assign(&generic_header->content_type, MIME_TYPE_SSML_XML, mrcp_message->pool);
		else
			apt_string_assign(&generic_header->content_type, MIME_TYPE_PLAIN_TEXT, mrcp_message->pool);

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



/* --- MRCP ASR --- */

/* Create a grammar object to reference in recognition requests. */
static int grammar_create(grammar_t **grammar, const char *name, grammar_type_t type, const char *data, apr_pool_t *pool)
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
static const char *grammar_type_to_mime(grammar_type_t type, profile_t *profile)
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

/* Check if recognition is complete. */
static int recog_channel_check_results(speech_channel_t *schannel)
{
	int status = 0;
	recognizer_data_t *r;

	if (schannel != NULL) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if ((r = (recognizer_data_t *)schannel->data) != NULL) {
			if ((r->result != NULL) && (strlen(r->result) > 0))
				ast_log(LOG_DEBUG, "(%s) SUCCESS, have result\n", schannel->name);
			else if (r->start_of_input)
				ast_log(LOG_DEBUG, "(%s) SUCCESS, start of input\n", schannel->name);
			else
				status = -1;
		} else {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);
			status = -1;
		}
	} else {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		status = -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}
#if 0
/* Start recognizer's input timers. */
static int recog_channel_start_input_timers(speech_channel_t *schannel)
{   
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if ((schannel->state == SPEECH_CHANNEL_PROCESSING) && (!r->timers_started)) {
		mrcp_message_t *mrcp_message;
		ast_log(LOG_DEBUG, "(%s) Starting input timers\n", schannel->name);

		/* Send START-INPUT-TIMERS to MRCP server. */
		mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_START_INPUT_TIMERS);

		if (mrcp_message == NULL) {
			ast_log(LOG_ERROR, "(%s) Failed to create START-INPUT-TIMERS message\n", schannel->name);
			status = -1;
		} else {
			/* Set it and forget it. */
			mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message);
		}
	}
 
	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}
#endif

/* Flag that input has started. */
static int recog_channel_set_start_of_input(speech_channel_t *schannel)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	r->start_of_input = 1;
	ast_log(LOG_DEBUG, "(%s) start of input\n", schannel->name);

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Set the recognition results. */
static int recog_channel_set_results(speech_channel_t *schannel, const char *result)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if (r->result && (strlen(r->result) > 0)) {
		ast_log(LOG_DEBUG, "(%s) result is already set\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if ((result == NULL) || (strlen(result) == 0)) {
		ast_log(LOG_DEBUG, "(%s) result is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	ast_log(LOG_DEBUG, "(%s) result:\n\n%s\n", schannel->name, result);
	r->result = apr_pstrdup(schannel->pool, result);

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Get the recognition results. */
static int recog_channel_get_results(speech_channel_t *schannel, char **result)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	if (r->result && (strlen(r->result) > 0)) {
		*result = strdup(r->result);
		ast_log(LOG_DEBUG, "(%s) result:\n\n%s\n", schannel->name, *result);
		r->result = NULL;
		r->start_of_input = 0;
	} else if (r->start_of_input) {
		ast_log(LOG_DEBUG, "(%s) start of input\n", schannel->name);
		status = 1;
		r->start_of_input = 0;
	} else
		status = -1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return status;
}

/* Set parameter in a recognizer MRCP header. */
static int recog_channel_set_header(speech_channel_t *schannel, int id, char *val, mrcp_message_t *msg, mrcp_recog_header_t *recog_hdr)
{
	if ((schannel == NULL) || (msg == NULL) || (recog_hdr == NULL))
		return -1;

	switch (id) {
		case RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD:
			recog_hdr->confidence_threshold = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD);
			break;

		case RECOGNIZER_HEADER_SENSITIVITY_LEVEL:
			recog_hdr->sensitivity_level = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SENSITIVITY_LEVEL);
			break;

		case RECOGNIZER_HEADER_SPEED_VS_ACCURACY:
			recog_hdr->speed_vs_accuracy = (float)atof(val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEED_VS_ACCURACY);
			break;

		case RECOGNIZER_HEADER_N_BEST_LIST_LENGTH: {
			int n_best_list_length = atoi(val);
			if (n_best_list_length > 0) {
				recog_hdr->n_best_list_length = n_best_list_length;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_N_BEST_LIST_LENGTH);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid n best list length, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_NO_INPUT_TIMEOUT: {
			int no_input_timeout = atoi(val);
			if (no_input_timeout >= 0) {
				recog_hdr->no_input_timeout = no_input_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_NO_INPUT_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid no input timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_RECOGNITION_TIMEOUT: {
			int recognition_timeout = atoi(val);
			if (recognition_timeout >= 0) {
				recog_hdr->recognition_timeout = recognition_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_RECOGNITION_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid recognition timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_START_INPUT_TIMERS:
			recog_hdr->start_input_timers = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_START_INPUT_TIMERS);
			break;

		case RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT: {
			int speech_complete_timeout = atoi(val);
			if (speech_complete_timeout >= 0) {
				recog_hdr->speech_complete_timeout = speech_complete_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid speech complete timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT: {
			int speech_incomplete_timeout = atoi(val);
			if (speech_incomplete_timeout >= 0) {
				recog_hdr->speech_incomplete_timeout = speech_incomplete_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid speech incomplete timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT: {
			int dtmf_interdigit_timeout = atoi(val);
			if (dtmf_interdigit_timeout >= 0) {
				recog_hdr->dtmf_interdigit_timeout = dtmf_interdigit_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf interdigit timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT: {
			int dtmf_term_timeout = atoi(val);
			if (dtmf_term_timeout >= 0) {
				recog_hdr->dtmf_term_timeout = dtmf_term_timeout;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf term timeout, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_DTMF_TERM_CHAR:
			if (strlen(val) == 1) {
				recog_hdr->dtmf_term_char = *val;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_DTMF_TERM_CHAR);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid dtmf term char, \"%s\"\n", schannel->name, val);
			break;

		case RECOGNIZER_HEADER_SAVE_WAVEFORM:
			recog_hdr->save_waveform = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SAVE_WAVEFORM);
			break;

		case RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL:
			recog_hdr->new_audio_channel = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL);
			break;

		case RECOGNIZER_HEADER_SPEECH_LANGUAGE:
			apt_string_assign(&recog_hdr->speech_language, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_SPEECH_LANGUAGE);
			break;

		case RECOGNIZER_HEADER_RECOGNITION_MODE:
			apt_string_assign(&recog_hdr->recognition_mode, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_RECOGNITION_MODE);
			break;

		case RECOGNIZER_HEADER_HOTWORD_MAX_DURATION: {
			int hotword_max_duration = atoi(val);
			if (hotword_max_duration >= 0) {
				recog_hdr->hotword_max_duration = hotword_max_duration;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_HOTWORD_MAX_DURATION);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid hotword max duration, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_HOTWORD_MIN_DURATION: {
			int hotword_min_duration = atoi(val);
			if (hotword_min_duration >= 0) {
				recog_hdr->hotword_min_duration = hotword_min_duration;
				mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_HOTWORD_MIN_DURATION);
			} else
				ast_log(LOG_WARNING, "(%s) Ignoring invalid hotword min duration, \"%s\"\n", schannel->name, val);
			break;
		}

		case RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER:
			recog_hdr->clear_dtmf_buffer = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER);
			break;

		case RECOGNIZER_HEADER_EARLY_NO_MATCH:
			recog_hdr->early_no_match = !strcasecmp("true", val);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_EARLY_NO_MATCH);
			break;

		case RECOGNIZER_HEADER_INPUT_WAVEFORM_URI:
			apt_string_assign(&recog_hdr->input_waveform_uri, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_INPUT_WAVEFORM_URI);
			break;

		case RECOGNIZER_HEADER_MEDIA_TYPE:
			apt_string_assign(&recog_hdr->media_type, val, msg->pool);
			mrcp_resource_header_property_add(msg, RECOGNIZER_HEADER_MEDIA_TYPE);
			break;

		/* Unsupported headers. */

		/* MRCP server headers. */
		case RECOGNIZER_HEADER_WAVEFORM_URI:
		case RECOGNIZER_HEADER_COMPLETION_CAUSE:
		case RECOGNIZER_HEADER_FAILED_URI:
		case RECOGNIZER_HEADER_FAILED_URI_CAUSE:
		case RECOGNIZER_HEADER_INPUT_TYPE:
		case RECOGNIZER_HEADER_COMPLETION_REASON:

		/* Module handles this automatically. */
		case RECOGNIZER_HEADER_CANCEL_IF_QUEUE:

		/* GET-PARAMS method only. */
		case RECOGNIZER_HEADER_RECOGNIZER_CONTEXT_BLOCK:
		case RECOGNIZER_HEADER_DTMF_BUFFER_TIME:

		/* INTERPRET method only. */
		case RECOGNIZER_HEADER_INTERPRET_TEXT:

		/* Unknown. */
		case RECOGNIZER_HEADER_VER_BUFFER_UTTERANCE:
		default:
			ast_log(LOG_WARNING, "(%s) unsupported RECOGNIZER header( in this module )\n", schannel->name);
			break;
	}

	return 0;
}

/* Set parameters in a recognizer MRCP header. */
static int recog_channel_set_params(speech_channel_t *schannel, mrcp_message_t *msg, mrcp_generic_header_t *gen_hdr, mrcp_recog_header_t *recog_hdr)
{
	if ((schannel != NULL) && (msg != NULL) && (gen_hdr != NULL) && (recog_hdr != NULL)) {
		/* Loop through each param and add to recog header or vendor-specific-params. */
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

				if (schannel->application->param_id_map != NULL)
					id = (unimrcp_param_id_t *)apr_hash_get(schannel->application->param_id_map, param_name, APR_HASH_KEY_STRING);

				if (id) {
					ast_log(LOG_DEBUG, "(%s) %s: %s\n", schannel->name, param_name, param_val);
					recog_channel_set_header(schannel, id->id, param_val, msg, recog_hdr);
				} else {
					apt_str_t apt_param_name = { 0 };
					apt_str_t apt_param_val = { 0 };

					/* This is probably a vendor-specific MRCP param. */
					ast_log(LOG_DEBUG, "(%s) (vendor-specific value) %s: %s\n", schannel->name, param_name, param_val);
					apt_string_set(&apt_param_name, param_name); /* Copy isn't necessary since apt_pair_array_append will do it. */
					apt_string_set(&apt_param_val, param_val);

					if (!gen_hdr->vendor_specific_params) {
						ast_log(LOG_DEBUG, "(%s) creating vendor specific pair array\n", schannel->name);
						gen_hdr->vendor_specific_params = apt_pair_array_create(10, msg->pool);
					}

					apt_pair_array_append(gen_hdr->vendor_specific_params, &apt_param_name, &apt_param_val, msg->pool);
				}
			}
		}
	
		if (gen_hdr->vendor_specific_params) {
			mrcp_generic_header_property_add(msg, GENERIC_HEADER_VENDOR_SPECIFIC_PARAMS);
		}
	} else
		ast_log(LOG_ERROR, "(unknown) [recog_channel_set_params] channel error!\n");

	return 0;
}

/* Flag that the recognizer channel timers are started. */
static int recog_channel_set_timers_started(speech_channel_t *schannel)
{
	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if (schannel->mutex != NULL)
		apr_thread_mutex_lock(schannel->mutex);

	recognizer_data_t *r = (recognizer_data_t *)schannel->data;

	if (r == NULL) {
		ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);

		return -1;
	}

	r->timers_started = 1;

	if (schannel->mutex != NULL)
		apr_thread_mutex_unlock(schannel->mutex);

	return 0;
}

/* Start RECOGNIZE request. */
static int recog_channel_start(speech_channel_t *schannel, const char *name)
{
	int status = 0;
	mrcp_message_t *mrcp_message = NULL;
	mrcp_generic_header_t *generic_header = NULL;
	mrcp_recog_header_t *recog_header = NULL;
	recognizer_data_t *r = NULL;
	char *start_input_timers = NULL;
	const char *mime_type = NULL;
	grammar_t *grammar = NULL;

	if ((schannel != NULL) && (name != NULL)) {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if (schannel->data == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		if ((r = (recognizer_data_t *)schannel->data) == NULL) {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		r->result = NULL;
		r->start_of_input = 0;

		/* Input timers are started by default unless the start-input-timers=false param is set. */
		start_input_timers = (char *)apr_hash_get(schannel->params, "start-input-timers", APR_HASH_KEY_STRING);
		r->timers_started = (start_input_timers == NULL) || (strlen(start_input_timers) == 0) || (strcasecmp(start_input_timers, "false"));

		/* Get the cached grammar. */
		if ((name == NULL) || (strlen(name) == 0))
			grammar = r->last_grammar;
		else {
			grammar = (grammar_t *)apr_hash_get(r->grammars, name, APR_HASH_KEY_STRING);
			r->last_grammar = grammar;
		}

		if (grammar == NULL) {
			if (name)
				ast_log(LOG_ERROR, "(%s) Undefined grammar, %s\n", schannel->name, name);
			else
				ast_log(LOG_ERROR, "(%s) No grammar specified\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Create MRCP message. */
		if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_RECOGNIZE)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Allocate generic header. */
		if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Set Content-Type. */
		if (((mime_type = grammar_type_to_mime(grammar->type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		apt_string_assign(&generic_header->content_type, mime_type, mrcp_message->pool);
		mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_TYPE);

		/* Set Content-ID for inline grammars. */
		if (grammar->type != GRAMMAR_TYPE_URI) {
			apt_string_assign(&generic_header->content_id, grammar->name, mrcp_message->pool);
			mrcp_generic_header_property_add(mrcp_message, GENERIC_HEADER_CONTENT_ID);
		}

		/* Allocate recognizer-specific header. */
		if ((recog_header = (mrcp_recog_header_t *)mrcp_resource_header_prepare(mrcp_message)) == NULL) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* Set Cancel-If-Queue. */
		if (mrcp_message->start_line.version == MRCP_VERSION_2) {
			recog_header->cancel_if_queue = FALSE;
			mrcp_resource_header_property_add(mrcp_message, RECOGNIZER_HEADER_CANCEL_IF_QUEUE);
		}

		/* Set parameters. */
		recog_channel_set_params(schannel, mrcp_message, generic_header, recog_header);

		/* Set message body. */
		apt_string_assign(&mrcp_message->body, grammar->data, mrcp_message->pool);

		/* Empty audio queue and send RECOGNIZE to MRCP server. */
		audio_queue_clear(schannel->audio_queue);

		if (mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message) == FALSE) {
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

/* Load speech recognition grammar. */
static int recog_channel_load_grammar(speech_channel_t *schannel, const char *name, grammar_type_t type, const char *data)
{
	int status = 0;
	grammar_t *g = NULL;
	char ldata[256];

	if ((schannel != NULL) && (name != NULL) && (data != NULL)) {
		ast_log(LOG_DEBUG, "(%s) Loading grammar %s, data = %s\n", schannel->name, name, data);

		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		if (schannel->state != SPEECH_CHANNEL_READY) {
			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		/* If inline, use DEFINE-GRAMMAR to cache it on the server. */
		if (type != GRAMMAR_TYPE_URI) {
			mrcp_message_t *mrcp_message;
			mrcp_generic_header_t *generic_header;
			const char *mime_type;

			/* Create MRCP message. */
			if ((mrcp_message = mrcp_application_message_create(schannel->unimrcp_session, schannel->unimrcp_channel, RECOGNIZER_DEFINE_GRAMMAR)) == NULL) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			/* Set Content-Type and Content-ID in message. */
			if ((generic_header = (mrcp_generic_header_t *)mrcp_generic_header_prepare(mrcp_message)) == NULL) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			if (((mime_type = grammar_type_to_mime(type, schannel->profile)) == NULL) || (strlen(mime_type) == 0)) {
				if (schannel->mutex != NULL)
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

			if (mrcp_application_message_send(schannel->unimrcp_session, schannel->unimrcp_channel, mrcp_message) == FALSE) {
				if (schannel->mutex != NULL)
					apr_thread_mutex_unlock(schannel->mutex);

				return -1;
			}

			if ((schannel->mutex != NULL) && (schannel->cond != NULL))
				apr_thread_cond_timedwait(schannel->cond, schannel->mutex, SPEECH_CHANNEL_TIMEOUT_USEC);

			if (schannel->state != SPEECH_CHANNEL_READY) {
				if (schannel->mutex != NULL)
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

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return status;
}

/* Unload speech recognition grammar. */
static int recog_channel_unload_grammar(speech_channel_t *schannel, const char *grammar_name)
{
	int status = 0;

	if (schannel == NULL) {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return -1;
	}

	if ((grammar_name == NULL) || (strlen(grammar_name) == 0))
		status = -1;
	else {
		if (schannel->mutex != NULL)
			apr_thread_mutex_lock(schannel->mutex);

		recognizer_data_t *r = (recognizer_data_t *)schannel->data;

		if (r == NULL) {
			ast_log(LOG_ERROR, "(%s) Recognizer data struct is NULL\n", schannel->name);

			if (schannel->mutex != NULL)
				apr_thread_mutex_unlock(schannel->mutex);

			return -1;
		}

		ast_log(LOG_DEBUG, "(%s) Unloading grammar %s\n", schannel->name, grammar_name);
		apr_hash_set(r->grammars, grammar_name, APR_HASH_KEY_STRING, NULL);

		if (schannel->mutex != NULL)
			apr_thread_mutex_unlock(schannel->mutex);
	}

	return status;
}

/* Process messages from UniMRCP for the recognizer application. */
static apt_bool_t recog_message_handler(const mrcp_app_message_t *app_message)
{
	/* Call the appropriate callback in the dispatcher function table based on the app_message received. */
	if (app_message != NULL)
		return mrcp_application_message_dispatch(&globals.recog.dispatcher, app_message);
	else {
		ast_log(LOG_ERROR, "(unknown) channel error!\n");
		return TRUE;
	}
}

/* Handle the MRCP responses/events. */
static apt_bool_t recog_on_message_receive(mrcp_application_t *application, mrcp_session_t *session, mrcp_channel_t *channel, mrcp_message_t *message)
{
	speech_channel_t *schannel;

	if (channel != NULL)
		schannel = (speech_channel_t *)mrcp_application_channel_object_get(channel);
	else
		schannel = NULL;

	if ((schannel != NULL) && (application != NULL) && (session != NULL) && (channel != NULL) && (message != NULL)) {
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
						ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d\n", schannel->name,	 message->start_line.status_code);
					else
						ast_log(LOG_DEBUG, "(%s) RECOGNIZE failed: status = %d, completion-cause = %03d\n", schannel->name, message->start_line.status_code, recog_hdr->completion_cause);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				} else if (message->start_line.request_state == MRCP_REQUEST_STATE_PENDING)
					/* RECOGNIZE is queued. */
					ast_log(LOG_DEBUG, "(%s) RECOGNIZE PENDING\n", schannel->name);
				else {
					/* Received unexpected request_state. */
					ast_log(LOG_DEBUG, "(%s) unexpected RECOGNIZE request state: %d\n", schannel->name, message->start_line.request_state);
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
					ast_log(LOG_DEBUG, "(%s) unexpected STOP request state: %d\n", schannel->name, message->start_line.request_state);
					speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
				}
			} else if (message->start_line.method_id == RECOGNIZER_START_INPUT_TIMERS) {
				/* Received response to START-INPUT-TIMERS request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) timers started\n", schannel->name);
						recog_channel_set_timers_started(schannel);
					} else
						ast_log(LOG_DEBUG, "(%s) timers failed to start, status code = %d\n", schannel->name, message->start_line.status_code);
				}
			} else if (message->start_line.method_id == RECOGNIZER_DEFINE_GRAMMAR) {
				/* Received response to DEFINE-GRAMMAR request. */
				if (message->start_line.request_state == MRCP_REQUEST_STATE_COMPLETE) {
					if (message->start_line.status_code >= 200 && message->start_line.status_code <= 299) {
						ast_log(LOG_DEBUG, "(%s) grammar loaded\n", schannel->name);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
					} else {
						ast_log(LOG_DEBUG, "(%s) grammar failed to load, status code = %d\n", schannel->name, message->start_line.status_code);
						speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
					}
				}
			} else {
				/* Received unexpected response. */
				ast_log(LOG_DEBUG, "(%s) unexpected response, method_id = %d\n", schannel->name, (int)message->start_line.method_id);
				speech_channel_set_state(schannel, SPEECH_CHANNEL_ERROR);
			}
		} else if (message->start_line.message_type == MRCP_MESSAGE_TYPE_EVENT) {
			/* Received MRCP event. */
			if (message->start_line.method_id == RECOGNIZER_RECOGNITION_COMPLETE) {
				ast_log(LOG_DEBUG, "(%s) RECOGNITION COMPLETE, Completion-Cause: %03d\n", schannel->name, recog_hdr->completion_cause);
				if (message->body.length > 0) {
					if (message->body.buf[message->body.length - 1] == '\0') {
						recog_channel_set_results(schannel, message->body.buf);
					} else {
						/* string is not null terminated */
						char *result = (char *)apr_palloc(schannel->pool, message->body.length + 1);
						ast_log(LOG_DEBUG, "(%s) Recognition result is not null-terminated.  Appending null terminator\n", schannel->name);
						strncpy(result, message->body.buf, message->body.length);
						result[message->body.length] = '\0';
						recog_channel_set_results(schannel, result);
					}
				} else {
					char completion_cause[512];
					char waveform_uri[256];
					apr_snprintf(completion_cause, sizeof(completion_cause) - 1, "Completion-Cause: %03d", recog_hdr->completion_cause);
					completion_cause[sizeof(completion_cause) - 1] = '\0';
					apr_snprintf(waveform_uri, sizeof(waveform_uri) - 1, "Waveform-URI: %s", recog_hdr->waveform_uri.buf);
					waveform_uri[sizeof(waveform_uri) - 1] = '\0';
					if (recog_hdr->waveform_uri.length > 0) {
						#if defined(ASTERISK162) || defined(ASTERISKSVN)
						strncat(completion_cause,",", 1);
						#else
						strncat(completion_cause,"|", 1);
						#endif
						strncat(completion_cause, waveform_uri, strlen(waveform_uri) );
					}
					recog_channel_set_results(schannel, completion_cause);
					ast_log(LOG_DEBUG, "(%s) No result\n", schannel->name);
				}
				speech_channel_set_state(schannel, SPEECH_CHANNEL_READY);
			} else if (message->start_line.method_id == RECOGNIZER_START_OF_INPUT) {
				ast_log(LOG_DEBUG, "(%s) START OF INPUT\n", schannel->name);
				if (schannel->chan != NULL) {
					ast_log(LOG_DEBUG, "(%s) Stopping playback due to start of input\n", schannel->name);
					ast_stopstream(schannel->chan);
				}
				recog_channel_set_start_of_input(schannel);
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

/* UniMRCP callback requesting stream to be opened. */
static apt_bool_t recog_stream_open(mpf_audio_stream_t* stream, mpf_codec_t *codec)
{
        speech_channel_t* schannel;


	if (stream != NULL)
		schannel = (speech_channel_t*)stream->obj;
	else
		schannel = NULL;

        schannel->stream = stream;

	if ((schannel == NULL) || (stream == NULL))
		ast_log(LOG_ERROR, "(unknown) channel error opening stream!\n");

        return TRUE;
}

/* UniMRCP callback requesting next frame for speech recognition. */
static apt_bool_t recog_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
	speech_channel_t *schannel;

	if (stream != NULL)
		schannel = (speech_channel_t *)stream->obj;
	else
		schannel = NULL;

	if ((schannel != NULL) && (stream != NULL) && (frame != NULL)) {
		if (schannel->dtmf_generator != NULL) {
			if (mpf_dtmf_generator_sending(schannel->dtmf_generator)) {
				ast_log(LOG_NOTICE, "(%s) DTMF frame written\n", schannel->name);
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
	} else
		ast_log(LOG_ERROR, "(unknown) channel error!\n");

	return TRUE;
}

/* Setup ASR. */
static int recog_load(apr_pool_t *pool)
{
	if (pool == NULL) {
		ast_log(LOG_ERROR, "Memory pool is NULL\n");
		return -1;
	}

	/* Create the recognizer application and link its callbacks */
	if ((globals.recog.app = mrcp_application_create(recog_message_handler, (void *)0, pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create recognizer MRCP application\n");
		return -1;
	}

	globals.recog.dispatcher.on_session_update = NULL;
	globals.recog.dispatcher.on_session_terminate = speech_on_session_terminate;
	globals.recog.dispatcher.on_channel_add = speech_on_channel_add;
	globals.recog.dispatcher.on_channel_remove = speech_on_channel_remove;
	globals.recog.dispatcher.on_message_receive = recog_on_message_receive;
	globals.recog.audio_stream_vtable.destroy = NULL;
	globals.recog.audio_stream_vtable.open_rx = recog_stream_open;
	globals.recog.audio_stream_vtable.close_rx = NULL;
	globals.recog.audio_stream_vtable.read_frame = recog_stream_read;
	globals.recog.audio_stream_vtable.open_tx = NULL;
	globals.recog.audio_stream_vtable.close_tx = NULL;
	globals.recog.audio_stream_vtable.write_frame = NULL;

	if (!mrcp_client_application_register(globals.mrcp_client, globals.recog.app, app_recog)) {
		ast_log(LOG_ERROR, "Unable to register recognizer MRCP application\n");
		if (!mrcp_application_destroy(globals.recog.app))
			ast_log(LOG_WARNING, "Unable to destroy recognizer MRCP application\n");
		globals.recog.app = NULL;
		return -1;
	}

	/* Create a hash for the recognizer parameter map. */
	globals.recog.param_id_map = apr_hash_make(pool);

	if (globals.recog.param_id_map != NULL) {
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "confidence-threshold"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "sensitivity-level"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SENSITIVITY_LEVEL, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "speed-vs-accuracy"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEED_VS_ACCURACY, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "n-best-list-length"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_N_BEST_LIST_LENGTH, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "no-input-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_NO_INPUT_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "recognition-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNITION_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "waveform-url"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_WAVEFORM_URI, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "completion-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_COMPLETION_CAUSE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "recognizer-context-block"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNIZER_CONTEXT_BLOCK, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "start-input-timers"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_START_INPUT_TIMERS, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "speech-complete-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "speech-incomplete-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_INCOMPLETE_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "dtmf-interdigit-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_INTERDIGIT_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "dtmf-term-timeout"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_TERM_TIMEOUT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "dtmf-term-char"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_TERM_CHAR, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "failed-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_FAILED_URI, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "failed-uri-cause"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_FAILED_URI_CAUSE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "save-waveform"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SAVE_WAVEFORM, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "new-audio-channel"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_NEW_AUDIO_CHANNEL, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "speech-language"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_SPEECH_LANGUAGE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "input-type"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INPUT_TYPE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "input-waveform-uri"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INPUT_WAVEFORM_URI, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "completion-reason"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_COMPLETION_REASON, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "media-type"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_MEDIA_TYPE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "ver-buffer-utterance"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_VER_BUFFER_UTTERANCE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "recognition-mode"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_RECOGNITION_MODE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "cancel-if-queue"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CANCEL_IF_QUEUE, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "hotword-max-duration"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_HOTWORD_MAX_DURATION, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "hotword-min-duration"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_HOTWORD_MIN_DURATION, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "interpret-text"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_INTERPRET_TEXT, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "dtmf-buffer-time"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_DTMF_BUFFER_TIME, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "clear-dtmf-buffer"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_CLEAR_DTMF_BUFFER, pool));
		apr_hash_set(globals.recog.param_id_map, apr_pstrdup(pool, "early-no-match"), APR_HASH_KEY_STRING, unimrcp_param_id_create(RECOGNIZER_HEADER_EARLY_NO_MATCH, pool));
	}

	return 0;
}

/* Shutdown ASR. */
static void recog_shutdown(void)
{
	/* Clear parameter ID map. */
	if (globals.recog.param_id_map != NULL)
		apr_hash_clear(globals.recog.param_id_map);
}



/* --- ASTERISK SPECIFIC CONFIGURATION --- */

static int load_config(void)
{
	#ifndef ASTERISK14
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(MRCP_CONFIG, config_flags);
	#else
	struct ast_config *cfg = ast_config_load(MRCP_CONFIG);
	#endif
	const char *cat = NULL;
	struct ast_variable *var;
	const char *value = NULL;

	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", MRCP_CONFIG);
		return -1;
	}
	#if defined(ASTERISKSVN) || defined(ASTERISK162)
	else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " MRCP_CONFIG " is in an invalid format, aborting\n");
		return -1;
	}
	#endif

	globals_clear();
	globals_default();

	if ((value = ast_variable_retrieve(cfg, "general", "default-tts-profile")) != NULL) {
		ast_log(LOG_DEBUG, "general.default-tts-profile=%s\n",  value);
		globals.unimrcp_default_synth_profile = apr_pstrdup(globals.pool, value);
	} else {
		ast_log(LOG_ERROR, "Unable to load genreal.default-tts-profile from config file " MRCP_CONFIG ", aborting\n");
	   	ast_config_destroy(cfg);
		return -1;
	}
	if ((value = ast_variable_retrieve(cfg, "general", "default-asr-profile")) != NULL) {
		ast_log(LOG_DEBUG, "general.default-asr-profile=%s\n",  value);
		globals.unimrcp_default_recog_profile = apr_pstrdup(globals.pool, value);
	} else {
		ast_log(LOG_ERROR, "Unable to load genreal.default-asr-profile from config file " MRCP_CONFIG ", aborting\n");
	   	ast_config_destroy(cfg);
		return -1;
	}
	if ((value = ast_variable_retrieve(cfg, "general", "log-level")) != NULL) {
		ast_log(LOG_DEBUG, "general.log-level=%s\n",  value);
		globals.unimrcp_log_level = apr_pstrdup(globals.pool, value);
	}
	if ((value = ast_variable_retrieve(cfg, "general", "max-connection-count")) != NULL) {
		ast_log(LOG_DEBUG, "general.max-connection-count=%s\n",  value);
		globals.unimrcp_max_connection_count = apr_pstrdup(globals.pool, value);
	}
	if ((value = ast_variable_retrieve(cfg, "general", "offer-new-connection")) != NULL) {
		ast_log(LOG_DEBUG, "general.offer-new-connection=%s\n",  value);
		globals.unimrcp_offer_new_connection = apr_pstrdup(globals.pool, value);
	}
	if ((value = ast_variable_retrieve(cfg, "general", "rx-buffer-size")) != NULL) {
		ast_log(LOG_DEBUG, "general.rx-buffer-size=%s\n",  value);
		globals.unimrcp_rx_buffer_size = apr_pstrdup(globals.pool, value);
	}
	if ((value = ast_variable_retrieve(cfg, "general", "tx-buffer-size")) != NULL) {
		ast_log(LOG_DEBUG, "general.tx-buffer-size=%s\n",  value);
		globals.unimrcp_tx_buffer_size = apr_pstrdup(globals.pool, value);
	}

	while ((cat = ast_category_browse(cfg, cat)) != NULL) {
		if (strcasecmp(cat, "general") != 0) {
			if ((value = ast_variable_retrieve(cfg, cat, "version")) != NULL) {
				profile_t *mod_profile = NULL;

				if (profile_create(&mod_profile, cat, value, globals.pool) == 0) {
					apr_hash_set(globals.profiles, apr_pstrdup(globals.pool, mod_profile->name), APR_HASH_KEY_STRING, mod_profile);

					for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
						ast_log(LOG_DEBUG, "%s.%s=%s\n", cat, var->name, var->value);
						apr_hash_set(mod_profile->cfg, apr_pstrdup(globals.pool, var->name), APR_HASH_KEY_STRING, apr_pstrdup(globals.pool, var->value));
					}
				} else
					ast_log(LOG_WARNING, "Unable to create a profile for %s\n", cat);
			} else
				ast_log(LOG_WARNING, "Category %s does not have a version variable defined\n", cat);
		}
	}

	ast_config_destroy(cfg);

	return 0;
}



/* --- ASTERISK/MRCP LOGGING --- */

/* Translate log level string to enum. */
static apt_log_priority_e str_to_log_level(const char *level)
{
	if (strcasecmp(level, "EMERGENCY") == 0)
		return APT_PRIO_EMERGENCY;
	else if (strcasecmp(level, "ALERT") == 0)
		return APT_PRIO_ALERT;
	else if (strcasecmp(level, "CRITICAL") == 0)
		return APT_PRIO_CRITICAL;
	else if (strcasecmp(level, "ERROR") == 0)
		return APT_PRIO_ERROR;
	else if (strcasecmp(level, "WARNING") == 0)
		return APT_PRIO_WARNING;
	else if (strcasecmp(level, "NOTICE") == 0)
		return APT_PRIO_NOTICE;
	else if (strcasecmp(level, "INFO") == 0)
		return APT_PRIO_INFO;
	else if (strcasecmp(level, "DEBUG") == 0)
		return APT_PRIO_DEBUG;
	else
		return APT_PRIO_DEBUG;
}

/* Connects UniMRCP logging to Asterisk. */
static apt_bool_t unimrcp_log(const char *file, int line, const char *id, apt_log_priority_e priority, const char *format, va_list arg_ptr)
{
	/* Same size as MAX_LOG_ENTRY_SIZE in UniMRCP apt_log.c. */
	char log_message[4096] = { 0 };

	if (strlen(format) == 0)
		return TRUE;

	/* Assume apr_vsnprintf supports format extensions required by UniMRCP. */ 
	apr_vsnprintf(log_message, sizeof(log_message) - 1, format, arg_ptr);
	log_message[sizeof(log_message) - 1] = '\0';

	switch(priority) {
		case APT_PRIO_EMERGENCY:
			ast_log(LOG_WARNING, "%s\n", log_message);
			break;
		case APT_PRIO_ALERT:
			ast_log(LOG_WARNING, "%s\n", log_message);
			break;
		case APT_PRIO_CRITICAL:
			 ast_log(LOG_WARNING, "%s\n", log_message);
			break;
		case APT_PRIO_ERROR:
			ast_log(LOG_ERROR, "%s\n", log_message);
			break;
		case APT_PRIO_WARNING:
			ast_log(LOG_WARNING, "%s\n", log_message);
			break;
		case APT_PRIO_NOTICE:
			ast_log(LOG_NOTICE, "%s\n", log_message);
			break;
		case APT_PRIO_INFO:
			ast_log(LOG_DEBUG, "%s\n", log_message);
			break;
		case APT_PRIO_DEBUG:
			ast_log(LOG_DEBUG, "%s\n", log_message);
			break;
		default:
			ast_log(LOG_DEBUG, "%s\n", log_message);
			break;
	}

	return TRUE;
}



/* --- ASTERISK MODULE FUNCTIONS --- */

/* To use a specific codec between the e.g. SIP phone/telephone channel, Asterisk, UniMRCP and the MRCP server:
 *
 * 1. Set the correct codec allow/disallow in /etc/asterisk/sip.conf
 * 2. Set the desired codec first in the list of the codecs parameter in /etc/asterisk/mrcp.conf
 * 3. Make sure that the MRCP server supports the codec.
 *
 * This will result in minimal codec translation between the different servers. We only support Linear PCM,
 * 16-bit per sample, 8000 samples/sec, as well as PCMU and PCMA. All other Asterisk codecs will default
 * to Linear PCM. This is because sdp_resource_discovery_string_generate in modules/mrcp-sofiasip/src/mrcp_sdp.c
 * of UniMRCP only does service discovery for PCMU, PCMA and L16, which means we'll only get one of these three
 * from the MRCP server.
 */

static const char* codec_to_str(format_t codec) {
	switch(codec) {
		/*! G.723.1 compression */
		case AST_FORMAT_G723_1: return "L16";
		/*! GSM compression */
		case AST_FORMAT_GSM: return "L16";
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return "PCMU";
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return "PCMA";
		/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
		case AST_FORMAT_G726_AAL2: return "L16";
		/*! ADPCM (IMA) */
		case AST_FORMAT_ADPCM: return "L16"; 
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: return "L16";
		/*! LPC10, 180 samples/frame */
		case AST_FORMAT_LPC10: return "L16";
		/*! G.729A audio */
		case AST_FORMAT_G729A: return "L16";
		/*! SpeeX Free Compression */
		case AST_FORMAT_SPEEX: return "L16";
		/*! iLBC Free Compression */
		case AST_FORMAT_ILBC: return "L16"; 
		/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
		case AST_FORMAT_G726: return "L16";
		/*! G.722 */
		case AST_FORMAT_G722: return "L16";
		#if defined(ASTERISKSVN) || defined(ASTERISK162)
		/*! G.722.1 (also known as Siren7, 32kbps assumed) */
		case AST_FORMAT_SIREN7: return "L16";
		/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
		case AST_FORMAT_SIREN14: return "L16";
		#endif
		#ifndef ASTERISK14
		/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
		case AST_FORMAT_SLINEAR16: return "L16";
		#endif
		default: return "L16";
	}
}

static int codec_to_bytes_per_sample(format_t codec) {
	switch(codec) {
		/*! G.723.1 compression */
		case AST_FORMAT_G723_1: return 2;
		/*! GSM compression */
		case AST_FORMAT_GSM: return 2;
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return 1;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return 1;
		/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
		case AST_FORMAT_G726_AAL2: return 2;
		/*! ADPCM (IMA) */
		case AST_FORMAT_ADPCM: return 2; 
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: return 2;
		/*! LPC10, 180 samples/frame */
		case AST_FORMAT_LPC10: return 2;
		/*! G.729A audio */
		case AST_FORMAT_G729A: return 2;
		/*! SpeeX Free Compression */
		case AST_FORMAT_SPEEX: return 2;
		/*! iLBC Free Compression */
		case AST_FORMAT_ILBC: return 2; 
		/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
		case AST_FORMAT_G726: return 2;
		/*! G.722 */
		case AST_FORMAT_G722: return 2;
		#if defined(ASTERISKSVN) || defined(ASTERISK162)
		/*! G.722.1 (also known as Siren7, 32kbps assumed) */
		case AST_FORMAT_SIREN7: return 2;
		/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
		case AST_FORMAT_SIREN14: return 2;
		#endif
		#ifndef ASTERISK14
		/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
		case AST_FORMAT_SLINEAR16: return 2;
		#endif
		default: return 2;
	}
}

static format_t codec_to_format(format_t codec) {
	switch(codec) {
		/*! G.723.1 compression */
		case AST_FORMAT_G723_1: return AST_FORMAT_SLINEAR;
		/*! GSM compression */
		case AST_FORMAT_GSM: return AST_FORMAT_SLINEAR;
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: return AST_FORMAT_ULAW;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: return AST_FORMAT_ALAW;
		/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
		case AST_FORMAT_G726_AAL2: return AST_FORMAT_SLINEAR;
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
		/*! G.722 */
		case AST_FORMAT_G722: return AST_FORMAT_SLINEAR;
		#if defined(ASTERISKSVN) || defined(ASTERISK162)
		/*! G.722.1 (also known as Siren7, 32kbps assumed) */
		case AST_FORMAT_SIREN7: return AST_FORMAT_SLINEAR;
		/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
		case AST_FORMAT_SIREN14: return AST_FORMAT_SLINEAR;
		#endif
		#ifndef ASTERISK14
		/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
		case AST_FORMAT_SLINEAR16: return AST_FORMAT_SLINEAR;
		#endif
		default: return AST_FORMAT_SLINEAR;
	}
}

#if defined(ASTERISKSVN)
static int app_synth_exec(struct ast_channel *chan, const char *data)
#else
static int app_synth_exec(struct ast_channel *chan, void *data)
#endif
{
	int samplerate = 8000;
	/* int framesize = DEFAULT_FRAMESIZE; */
	int framesize = codec_to_bytes_per_sample(chan->rawwriteformat) * (DEFAULT_FRAMESIZE / 2);
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
	profile_t *profile = NULL;
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
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	if (!ast_strlen_zero(option_filename))
		fp = fopen(option_filename, "wb");

	apr_snprintf(name, sizeof(name) - 1, "TTS-%lu", (unsigned long int)speech_channel_number);
	name[sizeof(name) - 1] = '\0';

	/* if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_SYNTHESIZER, &globals.synth, "L16", samplerate, chan) != 0) { */
	if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_SYNTHESIZER, &globals.synth, codec_to_str(chan->rawwriteformat), samplerate, chan) != 0) {
		res = -1;
		goto done;
	}

	if ((option_profile != NULL) && (!ast_strlen_zero(option_profile))) {
		if (strcasecmp(option_profile, "default") == 0)
			profile_name = globals.unimrcp_default_synth_profile;
		else
			profile_name = option_profile;
	} else
		profile_name = globals.unimrcp_default_synth_profile;
	profile = (profile_t *)apr_hash_get(globals.profiles, profile_name, APR_HASH_KEY_STRING);
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

	int owriteformat = chan->writeformat;

	/* if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) { */
	if (ast_set_write_format(chan, codec_to_format(chan->rawwriteformat)) < 0) {
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
					#if defined(ASTERISKSVN)
					fr.subclass.codec = codec_to_format(chan->rawwriteformat);
					#else
					fr.subclass = codec_to_format(chan->rawwriteformat);
					#endif
					fr.datalen = len;
					/* fr.samples = len / 2; */
					fr.samples = len / codec_to_bytes_per_sample(chan->rawwriteformat);
					#if defined(ASTERISKSVN) || defined(ASTERISK162) || defined(ASTERISK161) 
					fr.data.ptr = buffer;
					#else
					fr.data = buffer;
					#endif
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
						next = ast_tvadd(next, ast_samp2tv(framesize / codec_to_bytes_per_sample(chan->rawwriteformat), samplerate));
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
						#if defined(ASTERISKSVN)
						dtmfkey = f->subclass.integer;
						#else
						dtmfkey =  f->subclass;
						#endif

						ast_log(LOG_DEBUG, "User pressed a key (%d)\n", dtmfkey);
						if (option_interrupt && strchr(option_interrupt, dtmfkey)) {
							res = dtmfkey;
							dobreak = 0;
						}

						ast_log(LOG_DEBUG, "(%s) sending BARGE-IN-OCCURED\n", schannel->name);

						if (speech_channel_bargeinoccured(schannel) != 0) {
							ast_log(LOG_ERROR, "(%s) Failed to send BARGE-IN-OCCURED\n", schannel->name);
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

	if (owriteformat)
		ast_set_write_format(chan, owriteformat);

	if (fp != NULL)
		fclose(fp);

	speech_channel_stop(schannel);
	speech_channel_destroy(schannel);

done:
	return res;
}

#if defined(ASTERISKSVN)
static int app_recog_exec(struct ast_channel *chan, const char *data)
#else
static int app_recog_exec(struct ast_channel *chan, void *data)
#endif
{
	int samplerate = 8000;
	int dtmf_enable = 0;
	struct ast_frame *f = NULL;
	apr_size_t len;
	int rres = 0;
	speech_channel_t *schannel = NULL;
	const char *profile_name = NULL;
	profile_t *profile = NULL;
	apr_uint32_t speech_channel_number = get_next_speech_channel_number();
	char name[200] = { 0 };
	int waitres = 0;
	int res = 0;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(grammar);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (grammar[,options])\n", app_recog);
		return -1;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);
	char option_confidencethresh[64] = { 0 };
	char option_senselevel[64] = { 0 };
	char option_profile[256] = { 0 };
	char option_interrupt[64] = { 0 };
	char option_filename[384] = { 0 };
	char option_timeout[64] = { 0 };
	char option_bargein[32] = { 0 };
	char option_inputwaveuri[384] = { 0 };
	char option_earlynomatch[16] = { 0 };
	char option_cleardtmfbuf[16] = { 0 };
	char option_hotwordmin[64] = { 0 };
	char option_hotwordmax[64] = { 0 };
	char option_recogmode[128] = { 0 };
	char option_newaudioc[16] = { 0 };
	char option_savewave[16] = { 0 };
	char option_dtmftermc[16] = { 0 };
	char option_dtmftermt[64] = { 0 };
	char option_dtmfdigitt[64] = { 0 };
	char option_speechit[64] = { 0 };
	char option_speechct[64] = { 0 };
	char option_startinputt[16] = { 0 };
	char option_noinputt[64] = { 0 };
	char option_nbest[64] = { 0 };
	char option_speedvsa[64] = { 0 };
	char option_mediatype[64] = { 0 };
	int speech = 0;
	struct timeval start = { 0, 0 };
	struct timeval detection_start = { 0, 0 };
	int min = 100;
	struct ast_dsp *dsp = NULL;

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
				if (strcasecmp(key, "ct") == 0) {
					strncpy(option_confidencethresh, value, sizeof(option_confidencethresh) - 1);
					option_confidencethresh[sizeof(option_confidencethresh) - 1] = '\0';
				} else if (strcasecmp(key, "sva") == 0) {
					strncpy(option_speedvsa, value, sizeof(option_speedvsa) - 1);
					option_speedvsa[sizeof(option_speedvsa) - 1] = '\0';
				} else if (strcasecmp(key, "nb") == 0) {
					strncpy(option_nbest, value, sizeof(option_nbest) - 1);
					option_nbest[sizeof(option_nbest) - 1] = '\0';
				} else if (strcasecmp(key, "nit") == 0) {
					strncpy(option_noinputt, value, sizeof(option_noinputt) - 1);
					option_noinputt[sizeof(option_noinputt) - 1] = '\0';
				} else if (strcasecmp(key, "sit") == 0) {
					strncpy(option_startinputt, value, sizeof(option_startinputt) - 1);
					option_startinputt[sizeof(option_startinputt) - 1] = '\0';
				} else if (strcasecmp(key, "sct") == 0) {
					strncpy(option_speechct, value, sizeof(option_speechct) - 1);
					option_speechct[sizeof(option_speechct) - 1] = '\0';
				} else if (strcasecmp(key, "sint") == 0) {
					strncpy(option_speechit, value, sizeof(option_speechit) - 1);
					option_speechit[sizeof(option_speechit) - 1] = '\0';
				} else if (strcasecmp(key, "dit") == 0) {
					strncpy(option_dtmfdigitt, value, sizeof(option_dtmfdigitt) - 1);
					option_dtmfdigitt[sizeof(option_dtmfdigitt) - 1] = '\0';
				} else if (strcasecmp(key, "dtt") == 0) {
					strncpy(option_dtmftermt, value, sizeof(option_dtmftermt) - 1);
					option_dtmftermt[sizeof(option_dtmftermt) - 1] = '\0';
				} else if (strcasecmp(key, "dttc") == 0) {
					strncpy(option_dtmftermc, value, sizeof(option_dtmftermc) - 1);
					option_dtmftermc[sizeof(option_dtmftermc) - 1] = '\0';
				} else if (strcasecmp(key, "sw") == 0) {
					strncpy(option_savewave, value, sizeof(option_savewave) - 1);
					option_savewave[sizeof(option_savewave) - 1] = '\0';
				} else if (strcasecmp(key, "nac") == 0) {
					strncpy(option_newaudioc, value, sizeof(option_newaudioc) - 1);
					option_newaudioc[sizeof(option_newaudioc) - 1] = '\0';
				} else if (strcasecmp(key, "rm") == 0) {
					strncpy(option_recogmode, value, sizeof(option_recogmode) - 1);
					option_recogmode[sizeof(option_recogmode) - 1] = '\0';
				} else if (strcasecmp(key, "hmaxd") == 0) {
					strncpy(option_hotwordmax, value, sizeof(option_hotwordmax) - 1);
					option_hotwordmax[sizeof(option_hotwordmax) - 1] = '\0';
				} else if (strcasecmp(key, "hmind") == 0) {
					strncpy(option_hotwordmin, value, sizeof(option_hotwordmin) - 1);
					option_hotwordmin[sizeof(option_hotwordmin) - 1] = '\0';
				} else if (strcasecmp(key, "cdb") == 0) {
					strncpy(option_cleardtmfbuf, value, sizeof(option_cleardtmfbuf) - 1);
					option_cleardtmfbuf[sizeof(option_cleardtmfbuf) - 1] = '\0';
				} else if (strcasecmp(key, "enm") == 0) {
					strncpy(option_earlynomatch, value, sizeof(option_earlynomatch) - 1);
					option_earlynomatch[sizeof(option_earlynomatch) - 1] = '\0';
				} else if (strcasecmp(key, "iwu") == 0) {
					strncpy(option_inputwaveuri, value, sizeof(option_inputwaveuri) - 1);
					option_inputwaveuri[sizeof(option_inputwaveuri) - 1] = '\0';
				} else if (strcasecmp(key, "sl") == 0) {
					strncpy(option_senselevel, value, sizeof(option_senselevel) - 1);
					option_senselevel[sizeof(option_senselevel) - 1] = '\0';
				} else if (strcasecmp(key, "mt") == 0) {
					strncpy(option_mediatype, value, sizeof(option_mediatype) - 1);
					option_mediatype[sizeof(option_mediatype) - 1] = '\0';
				} else if (strcasecmp(key, "p") == 0) {
					strncpy(option_profile, value, sizeof(option_profile) - 1);
					option_profile[sizeof(option_profile) - 1] = '\0';
				} else if (strcasecmp(key, "i") == 0) {
					strncpy(option_interrupt, value, sizeof(option_interrupt) - 1);
					option_interrupt[sizeof(option_interrupt) - 1] = '\0';
				} else if (strcasecmp(key, "f") == 0) {
					strncpy(option_filename, value, sizeof(option_filename) - 1);
					option_filename[sizeof(option_filename) - 1] = '\0';
				} else if (strcasecmp(key, "t") == 0) {
					strncpy(option_timeout, value, sizeof(option_timeout) - 1);
					option_timeout[sizeof(option_timeout) - 1] = '\0';
				} else if (strcasecmp(key, "b") == 0) {
					strncpy(option_bargein, value, sizeof(option_bargein) - 1);
					option_bargein[sizeof(option_bargein) - 1] = '\0';
				}
			}
			if (token != NULL) {
				strncpy(tempstr, token + 1, sizeof(tempstr) - 1);
				tempstr[sizeof(tempstr) - 1] = '\0';
			}
		} while (token != NULL);
	}

	int bargein = 1;
	if (!ast_strlen_zero(option_bargein)) {
		bargein = atoi(option_bargein);
		if ((bargein < 0) || (bargein > 2))
			bargein = 1;
	}

	if (!ast_strlen_zero(option_profile)) {
		ast_log(LOG_NOTICE, "Profile to use: %s\n", option_profile);
	}
	if (!ast_strlen_zero(args.grammar)) {
		ast_log(LOG_NOTICE, "Grammar to recognize with: %s\n", args.grammar);
	}
	if (!ast_strlen_zero(option_filename)) {
		ast_log(LOG_NOTICE, "Filename to play: %s\n", option_filename);
	}
	if (!ast_strlen_zero(option_timeout)) {
		ast_log(LOG_NOTICE, "Recognition timeout: %s\n", option_timeout);
	}
	if (!ast_strlen_zero(option_bargein)) {
		ast_log(LOG_NOTICE, "Barge-in: %s\n", option_bargein);
	}
	if (!ast_strlen_zero(option_confidencethresh)) {
		ast_log(LOG_NOTICE, "Confidence threshold: %s\n", option_confidencethresh);
	}
	if (!ast_strlen_zero(option_senselevel)) {
		ast_log(LOG_NOTICE, "Sensitivity-level: %s\n", option_senselevel);
	}
	if (!ast_strlen_zero(option_inputwaveuri)) {
		ast_log(LOG_NOTICE, "Input wave URI: %s\n", option_inputwaveuri);
	}
	if (!ast_strlen_zero(option_earlynomatch)) {
		ast_log(LOG_NOTICE, "Early-no-match: %s\n", option_earlynomatch);
	}
	if (!ast_strlen_zero(option_cleardtmfbuf)) {
		ast_log(LOG_NOTICE, "Clear DTMF buffer: %s\n", option_cleardtmfbuf);
	}
	if (!ast_strlen_zero(option_hotwordmin)) {
		ast_log(LOG_NOTICE, "Hotword min delay: %s\n", option_hotwordmin);
	}
	if (!ast_strlen_zero(option_hotwordmax)) {
		ast_log(LOG_NOTICE, "Hotword max delay: %s\n", option_hotwordmax);
	}
	if (!ast_strlen_zero(option_recogmode)) {
		ast_log(LOG_NOTICE, "Recognition Mode: %s\n", option_recogmode);
	}
	if (!ast_strlen_zero(option_newaudioc)) {
		ast_log(LOG_NOTICE, "New-audio-channel: %s\n", option_newaudioc);
	}
	if (!ast_strlen_zero(option_savewave)) {
		ast_log(LOG_NOTICE, "Save waveform: %s\n", option_savewave);
	}
	if (!ast_strlen_zero(option_dtmftermc)) {
		ast_log(LOG_NOTICE, "DTMF term char: %s\n", option_dtmftermc);
	}
	if (!ast_strlen_zero(option_dtmftermt)) {
		ast_log(LOG_NOTICE, "DTMF terminate timeout: %s\n", option_dtmftermt);
	}
	if (!ast_strlen_zero(option_dtmfdigitt)) {
		ast_log(LOG_NOTICE, "DTMF digit terminate timeout: %s\n", option_dtmfdigitt);
	}
	if (!ast_strlen_zero(option_speechit)) {
		ast_log(LOG_NOTICE, "Speech incomplete timeout : %s\n", option_speechit);
	}
	if (!ast_strlen_zero(option_speechct)) {
		ast_log(LOG_NOTICE, "Speech complete timeout: %s\n", option_speechct);
	}
	if (!ast_strlen_zero(option_startinputt)) {
		ast_log(LOG_NOTICE, "Start-input timeout: %s\n", option_startinputt);
	}
	if (!ast_strlen_zero(option_noinputt)) {
		ast_log(LOG_NOTICE, "No-input timeout: %s\n", option_noinputt);
	}
	if (!ast_strlen_zero(option_nbest)) {
		ast_log(LOG_NOTICE, "N-best list length: %s\n", option_nbest);
	}
	if (!ast_strlen_zero(option_speedvsa)) {
		ast_log(LOG_NOTICE, "Speed vs accuracy: %s\n", option_speedvsa);
	}
	if (!ast_strlen_zero(option_mediatype)) {
		ast_log(LOG_NOTICE, "Media Type: %s\n", option_mediatype);
	}

	if (strlen(option_interrupt) > 0) {
		dtmf_enable = 1;

		if (strcasecmp(option_interrupt, "any") == 0) {
			strncpy(option_interrupt, AST_DIGIT_ANY, sizeof(option_interrupt) - 1);
			option_interrupt[sizeof(option_interrupt) - 1] = '\0';
		} else if (strcasecmp(option_interrupt, "none") == 0)
			dtmf_enable = 2;
	}
	ast_log(LOG_NOTICE, "DTMF enable: %d\n", dtmf_enable);

	/* Answer if it's not already going. */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	apr_snprintf(name, sizeof(name) - 1, "ASR-%lu", (unsigned long int)speech_channel_number);
	name[sizeof(name) - 1] = '\0';

	/* if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_RECOGNIZER, &globals.recog, "L16", samplerate, chan) != 0) { */
	if (speech_channel_create(&schannel, name, SPEECH_CHANNEL_RECOGNIZER, &globals.recog, codec_to_str(chan->rawreadformat), samplerate, chan) != 0) {
		res = -1;
		goto done;
	}

	if ((option_profile != NULL) && (!ast_strlen_zero(option_profile))) {
		if (strcasecmp(option_profile, "default") == 0)
			profile_name = globals.unimrcp_default_recog_profile;
		else
			profile_name = option_profile;
	} else
		profile_name = globals.unimrcp_default_recog_profile;
	profile = (profile_t *)apr_hash_get(globals.profiles, profile_name, APR_HASH_KEY_STRING);
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

	if (!ast_strlen_zero(option_timeout))
		speech_channel_set_param(schannel, "recognition-timeout", option_timeout);
	if (!ast_strlen_zero(option_confidencethresh))
		speech_channel_set_param(schannel, "confidence-threshold", option_confidencethresh);
	if (!ast_strlen_zero(option_inputwaveuri))
		speech_channel_set_param(schannel, "input-wave-uri", option_inputwaveuri);
	if (!ast_strlen_zero(option_earlynomatch))
		speech_channel_set_param(schannel, "earlynomatch",option_earlynomatch);
	if (!ast_strlen_zero(option_cleardtmfbuf))
		speech_channel_set_param(schannel, "clear-dtmf-buffer", option_cleardtmfbuf);
	if (!ast_strlen_zero(option_hotwordmin))
		speech_channel_set_param(schannel, "hotword-min-duration", option_hotwordmin);
	if (!ast_strlen_zero(option_hotwordmax))
		speech_channel_set_param(schannel, "hotword-max-duration", option_hotwordmax);
	if (!ast_strlen_zero(option_recogmode))
		speech_channel_set_param(schannel, "recognition-mode", option_recogmode);
	if (!ast_strlen_zero(option_newaudioc))
		speech_channel_set_param(schannel, "new-audio-channel", option_newaudioc);
	if (!ast_strlen_zero(option_savewave))
		speech_channel_set_param(schannel, "save-waveform", option_savewave);
	if (!ast_strlen_zero(option_dtmftermc))
		speech_channel_set_param(schannel, "dtmf-term-char", option_dtmftermc);
	if (!ast_strlen_zero(option_dtmftermt))
		speech_channel_set_param(schannel, "dtmf-term-timeout", option_dtmftermt);
	if (!ast_strlen_zero(option_dtmfdigitt))
		speech_channel_set_param(schannel, "dtmf-interdigit-timeout", option_dtmfdigitt);
	if (!ast_strlen_zero(option_speechit))
		speech_channel_set_param(schannel, "speech-incomplete-timeout", option_speechit);
	if (!ast_strlen_zero(option_speechct))
		speech_channel_set_param(schannel, "speech-complete-timeout", option_speechct);
	if (!ast_strlen_zero(option_startinputt))
		speech_channel_set_param(schannel, "start-input-timers", option_startinputt);
	if (!ast_strlen_zero(option_noinputt))
		speech_channel_set_param(schannel, "no-input-timeout", option_noinputt);
	if (!ast_strlen_zero(option_nbest))
		speech_channel_set_param(schannel, "n-best-list-length", option_nbest);
	if (!ast_strlen_zero(option_speedvsa))
		speech_channel_set_param(schannel, "speed-vs-accuracy", option_speedvsa);
	if (!ast_strlen_zero(option_mediatype))
		speech_channel_set_param(schannel, "media-type", option_mediatype);
	if (!ast_strlen_zero(option_senselevel))
		speech_channel_set_param(schannel, "sensitivity-level", option_senselevel);
		
	int ireadformat = chan->readformat;
	/* if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) { */
	if (ast_set_read_format(chan, codec_to_format(chan->rawreadformat)) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format to signed linear\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}
	
	grammar_type_t tmp_grammar;
	if ((text_starts_with( args.grammar, HTTP_ID)) || (text_starts_with( args.grammar, HTTPS_ID)) || (text_starts_with( args.grammar, BUILTIN_ID))) {
		tmp_grammar = GRAMMAR_TYPE_URI;
	} else {
		tmp_grammar = GRAMMAR_TYPE_SRGS_XML;
	}

	if (recog_channel_load_grammar(schannel, name, tmp_grammar, args.grammar) != 0) {
		ast_log(LOG_ERROR, "Unable to load grammar\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}

	int resf = -1;
	struct ast_filestream* fs = NULL;
	off_t filelength = 0;

	ast_stopstream(chan);

	/* Open file, get file length, seek to begin, apply and play. */ 
	if (!ast_strlen_zero(option_filename)) {
		if ((fs = ast_openstream(chan, option_filename, chan->language)) == NULL) {
			ast_log(LOG_WARNING, "ast_openstream failed on %s for %s\n", chan->name, option_filename);
		} else {
			if (ast_seekstream(fs, -1, SEEK_END) == -1) {
				ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", chan->name, option_filename);
			} else {
				filelength = ast_tellstream(fs);
				ast_log(LOG_NOTICE, "file length:%"APR_OFF_T_FMT"\n", filelength);
			}

			if (ast_seekstream(fs, 0, SEEK_SET) == -1) {
				ast_log(LOG_WARNING, "ast_seekstream failed on %s for %s\n", chan->name, option_filename);
			} else if (ast_applystream(chan, fs) == -1) {
				ast_log(LOG_WARNING, "ast_applystream failed on %s for %s\n", chan->name, option_filename);
			} else if (ast_playstream(fs) == -1) {
				ast_log(LOG_WARNING, "ast_playstream failed on %s for %s\n", chan->name, option_filename);
			}
		}

		if (bargein == 0) {
			if (!resf)
				res = ast_waitstream(chan, "");
		}
	}

	unsigned long int marka = 0;
	unsigned long int markb = 0;

	typedef struct _DSP_FRAME_DATA_T_ {
		struct ast_frame* f;
		int speech;
	} dsp_frame_data_t;

	dsp_frame_data_t **dsp_frame_data = NULL;
	dsp_frame_data_t* myFrame;

	unsigned long int dsp_frame_data_len = DSP_FRAME_ARRAY_SIZE;
	unsigned long int dsp_frame_count = 0;
	int dtmfkey = -1;

	waitres = 0;

	/* Speech detection start. */
	if (bargein == 2) {	
		struct ast_frame *f1;
		dsp_frame_data = (dsp_frame_data_t**)malloc(sizeof(dsp_frame_data_t*) * DSP_FRAME_ARRAY_SIZE);

		if (dsp_frame_data != NULL)
			memset(dsp_frame_data,0,sizeof(dsp_frame_data_t*) * DSP_FRAME_ARRAY_SIZE);

		int freeframe = 0;

		if (dsp_frame_data == NULL) {
			ast_log(LOG_ERROR, "Unable to allocate frame array\n");
			res = -1;
		} else if ((dsp = (struct ast_dsp*)ast_dsp_new()) == NULL) {
			ast_log(LOG_ERROR, "Unable to allocate DSP!\n");
			res = -1;
		} else {
			ast_log(LOG_DEBUG,"Entering speech START detection\n");
			int ssdres = 1;	
			/* Only start to transmit frames as soon as voice has been detected. */
			detection_start = ast_tvnow();
	
			while (((waitres = ast_waitfor(chan, 100)) >= 0)) {
				if (waitres == 0)
					continue;

				freeframe = 0; /* By default free frame. */
				f1 = ast_read(chan);

				if (!f1 || !fs) {
					ssdres = -1;
					break;
				}
	
				/* Break if prompt has finished - don't care about analysis time which is in effect equal to prompt length. */
				if ((filelength != 0) && ( ast_tellstream(fs) >= filelength)) {
					ast_log(LOG_NOTICE, "prompt has finished playing, moving on.\n");
					ast_frfree(f1);
					break;
				}
	
				if (f1->frametype == AST_FRAME_VOICE) {
					myFrame = (dsp_frame_data_t*)malloc(sizeof(dsp_frame_data_t));

					if (myFrame == NULL) {
						ast_log(LOG_ERROR, "Failed to allocate a frame\n");
						ast_frfree(f1);
						break;
					}

					/* Store voice frame. */
					if (dsp_frame_count + 1 > dsp_frame_data_len) {
						dsp_frame_data_t** tmpData;
						tmpData = (dsp_frame_data_t**)realloc(dsp_frame_data, sizeof(dsp_frame_data_t*) * (dsp_frame_count + 1));

						if (tmpData != NULL) {
							dsp_frame_data = tmpData;
							dsp_frame_data_len++;
						} else {
							ast_log(LOG_ERROR, "Failure in storing frames, streaming directly\n");
							ast_frfree(f1);
							free(myFrame);
							break; /* Break out of detection loop, so one can rather just stream. */
						}
					}

					myFrame->f = f1;
					myFrame->speech = 0;

					dsp_frame_count++;
					dsp_frame_data[dsp_frame_count - 1] = myFrame;
					freeframe = 1; /* Do not free the frame, as it is stored */

					/* Continue analysis. */	
					int totalsilence;
					ssdres = ast_dsp_silence(dsp, f1, &totalsilence);
					int ms = 0;

					if (ssdres) { /* Glitch detection or detection after a small word. */
						if (speech) {
							/* We've seen speech in a previous frame. */
							/* We had heard some talking. */
							ms = ast_tvdiff_ms(ast_tvnow(), start);

							if (ms > min) {
								ast_log(LOG_DEBUG, "Found qualified token of %d ms\n", ms);
								markb = dsp_frame_count;
								myFrame->speech = 1;
								ast_stopstream(schannel->chan);
								break;
							} else {
								markb =0; marka =0;
								ast_log(LOG_DEBUG, "Found unqualified token of %d ms\n", ms);
								speech = 0;
							}
						}

						myFrame->speech = 0;
					} else {
						if (speech) {
							ms = ast_tvdiff_ms(ast_tvnow(), start);
						} else {
							/* Heard some audio, mark the begining of the token. */
							start = ast_tvnow();
							ast_log(LOG_DEBUG, "Start of voice token!\n");
							marka = dsp_frame_count;
						}

						speech = 1;
						myFrame->speech = 1;

						if (ms > min) {
							ast_log(LOG_DEBUG, "Found qualified speech token of %d ms\n", ms);
							markb = dsp_frame_count;
							ast_stopstream(schannel->chan);
							break;
						}
					}
				} else if (f1->frametype == AST_FRAME_VIDEO) {
					/* Ignore. */
				} else if ((dtmf_enable != 0) && (f1->frametype == AST_FRAME_DTMF)) {
					#if defined(ASTERISKSVN)
					dtmfkey = f1->subclass.integer;
					#else
					dtmfkey =  f1->subclass;
					#endif
					ast_log(LOG_DEBUG, "ssd: User pressed DTMF key (%d)\n", dtmfkey);
					break;
				}

				if (freeframe == 0)
					ast_frfree(f1); 
			}
		}
	}
	/* End of speech detection. */

	int i = 0;
	if (((dtmf_enable == 1) && (dtmfkey != -1)) || (waitres < 0)) {
		/* Skip as we have to return to specific dialplan extension, or a error occurred on the channel */
	} else {
		ast_log(LOG_NOTICE, "Recognizing\n");

		if (recog_channel_start(schannel, name) == 0) {
			if ((dtmfkey != -1) && (schannel->dtmf_generator != NULL)) {
				char digits[2];

				digits[0] = (char)dtmfkey;
				digits[1] = '\0';

				ast_log(LOG_NOTICE, "(%s) DTMF barge-in digit queued (%s)\n", schannel->name, digits);
				mpf_dtmf_generator_enqueue(schannel->dtmf_generator, digits);
				dtmfkey = -1;
			}	

			/* Playback buffer of frames captured during. */
			int pres = 0;

			if (dsp_frame_data != NULL) {
				if ((bargein == 2) && (markb != 0)) {
					for (i = marka; i < markb; ++i) {
						if ((dsp_frame_data[i] != NULL) && (dsp_frame_data[i]->speech == 1)) {
							myFrame = dsp_frame_data[i];
							len = myFrame->f->datalen;
							#if defined(ASTERISKSVN) || defined(ASTERISK162) || defined(ASTERISK161)
							rres = speech_channel_write(schannel, (void *)(myFrame->f->data.ptr), &len);
							#else
							rres = speech_channel_write(schannel, (void *)(myFrame->f->data), &len);	
							#endif
						}

						if (rres != 0)
							break;
					}

					if (pres != 0)
						ast_log(LOG_ERROR,"Could not transmit the playback (of SPEECH START DETECT)\n");
				}

				for (i = 0; i < dsp_frame_count; i++) {
					myFrame = dsp_frame_data[i];
					if (myFrame != NULL) {
						ast_frfree(myFrame->f); 
				free(dsp_frame_data[i]); 
					}
				}
			free(dsp_frame_data);
			}

			/* Continue with recognition. */
			while (((waitres = ast_waitfor(chan, 100)) >= 0)) {
				if (waitres == 0)
					continue;

				f = ast_read(chan);

				if (!f) {
					res = -1;
					break;
				}

				if (f->frametype == AST_FRAME_VOICE) {
					len = f->datalen;
					#if defined(ASTERISKSVN) || defined(ASTERISK162) || defined(ASTERISK161)
					rres = speech_channel_write(schannel, (void *)(f->data.ptr), &len);
					#else
					rres = speech_channel_write(schannel, (void *)(f->data), &len);	
					#endif
					if (rres != 0)
						break;
				} else if (f->frametype == AST_FRAME_VIDEO) {
					/* Ignore. */
				} else if ((dtmf_enable != 0) && (f->frametype == AST_FRAME_DTMF)) {
					#if defined(ASTERISKSVN)
					dtmfkey = f->subclass.integer;
					#else
					dtmfkey =  f->subclass;
					#endif

					ast_log(LOG_DEBUG, "User pressed DTMF key (%d)\n", dtmfkey);
					if (dtmf_enable == 2) { /* Send dtmf frame to ASR engine. */
						if (schannel->dtmf_generator != NULL) {
							char digits[2];

							digits[0] = (char)dtmfkey;
							digits[1] = '\0';

							ast_log(LOG_NOTICE, "(%s) DTMF digit queued (%s)\n", schannel->name, digits);
							mpf_dtmf_generator_enqueue(schannel->dtmf_generator, digits);
							dtmfkey = -1;
						}
					} else if (dtmf_enable == 1) { /* Stop streaming and return DTMF value to the dialplan if within i chars. */
						if (strchr(option_interrupt, dtmfkey) || (strcmp(option_interrupt,"any")))
							break ; 

						/* Continue if not an i-key. */
					}
				}

				if (f != NULL)
					ast_frfree(f);
			}
		} else {
			ast_log(LOG_ERROR, "Unable to start recognition\n");
			res = -1;
		}
		if (!f) {
			ast_log(LOG_NOTICE, "Got hangup\n");
			res = -1;
		} else {
			if (bargein != 0) {
				if (!resf)
					res = ast_waitstream(chan, "");
			}
		}

		char* result = NULL;

		if (recog_channel_check_results(schannel) == 0) {
			if (recog_channel_get_results(schannel, &result) == 0) {
				ast_log(LOG_NOTICE, "Result=|%s|\n", result);
			} else {
				ast_log(LOG_ERROR, "Unable to retrieve result\n");
			}
		}

		if (result != NULL)
			pbx_builtin_setvar_helper(chan, "RECOG_RESULT", result);
		else
			pbx_builtin_setvar_helper(chan, "RECOG_RESULT", "");
	}

	if ((dtmf_enable == 1) && (dtmfkey != -1) && (res != -1))
		res = dtmfkey;

	if (recog_channel_unload_grammar(schannel, name) != 0) {
		ast_log(LOG_ERROR, "Unable to unload grammar\n");
		res = -1;
		speech_channel_stop(schannel);
		speech_channel_destroy(schannel);
		goto done;
	}

	if (ireadformat)
		ast_set_read_format(chan, ireadformat);

	speech_channel_stop(schannel);
	speech_channel_destroy(schannel);
	ast_stopstream(chan);
	
done:
	return res;
}

static int reload(void)
{
	return 0;
}


static int unload_module(void)
{
	int res = 0;

	/* First unregister the applications so no more calls arrive. */
	res = ast_unregister_application(app_synth);
	res |= ast_unregister_application(app_recog);
	
	/* Shut down the recognizer and synthesizer interfaces. */
	synth_shutdown();
	recog_shutdown();

	/* Stop the MRCP client stack. */
	if (globals.mrcp_client != NULL) {
		if (!mrcp_client_shutdown(globals.mrcp_client))
			ast_log(LOG_WARNING, "Unable to shutdown MRCP client stack processing\n");
		else
			ast_log(LOG_DEBUG, "MRCP client stack processing shutdown\n");

		if (!mrcp_client_destroy(globals.mrcp_client))
			ast_log(LOG_WARNING, "Unable to destroy MRCP client stack\n");
		else
			ast_log(LOG_DEBUG, "MRCP client stack destroyed\n");

		globals.mrcp_client = NULL;
	}

	if (!apt_log_instance_destroy())
		ast_log(LOG_WARNING, "Unable to destroy UniMRCP logger instance\n");

	/* Destroy globals. */
	globals_destroy();

	if ((res == 0) && (apr_initialized != 0)) {
		apr_terminate();
		apr_initialized = 0;
	}

	return res;
}

static int load_module(void)
{
	int res = 0;

	if (apr_initialized == 0) {
		if (apr_initialize() != APR_SUCCESS) {
			ast_log(LOG_ERROR, "Unable to initialize APR\n");
			apr_terminate();
			apr_initialized = 0;
			return AST_MODULE_LOAD_DECLINE;
		} else {
			ast_log(LOG_DEBUG, "APR initialized\n");
			apr_initialized = 1;
		}
	}

	/* Initialize globals. */
	if (globals_init() != 0) {
		ast_log(LOG_DEBUG, "Unable to initialize globals\n");
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Load the configuration file mrcp.conf. */
	if (load_config() != 0) {
		ast_log(LOG_DEBUG, "Unable to load configuration\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Link UniMRCP logs to Asterisk. */
	ast_log(LOG_NOTICE, "UniMRCP log level = %s\n", globals.unimrcp_log_level);
	if (apt_log_instance_create(APT_LOG_OUTPUT_NONE, str_to_log_level(globals.unimrcp_log_level), globals.pool) == FALSE) {
		/* Already created. */
		apt_log_priority_set(str_to_log_level(globals.unimrcp_log_level));
	}
	apt_log_ext_handler_set(unimrcp_log);

	/* Create the MRCP client. */
	if ((globals.mrcp_client = mod_unimrcp_client_create(globals.pool)) == NULL) {
		ast_log(LOG_ERROR, "Failed to create MRCP client\n");
		if (!apt_log_instance_destroy())
			ast_log(LOG_WARNING, "Unable to destroy UniMRCP logger instance\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Create the synthesizer interface. */
	if (synth_load(globals.pool) != 0) {
		ast_log(LOG_ERROR, "Failed to create TTS interface\n");
		if (!mrcp_client_destroy(globals.mrcp_client))
			ast_log(LOG_WARNING, "Unable to destroy MRCP client stack\n");
		else
			ast_log(LOG_DEBUG, "MRCP client stack destroyed\n");
		globals.mrcp_client = NULL;
		if (!apt_log_instance_destroy())
			ast_log(LOG_WARNING, "Unable to destroy UniMRCP logger instance\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	} 

	/* Create the recognizer interface. */
	if (recog_load(globals.pool) != 0) {
		synth_shutdown();
		ast_log(LOG_ERROR, "Failed to create ASR interface\n");
		if (!mrcp_client_destroy(globals.mrcp_client))
			ast_log(LOG_WARNING, "Unable to destroy MRCP client stack\n");
		else
			ast_log(LOG_DEBUG, "MRCP client stack destroyed\n");
		globals.mrcp_client = NULL;
		if (!apt_log_instance_destroy())
			ast_log(LOG_WARNING, "Unable to destroy UniMRCP logger instance\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Start the client stack. */
	if (!mrcp_client_start(globals.mrcp_client)) {
		synth_shutdown();
		recog_shutdown();
		ast_log(LOG_ERROR, "Failed to start MRCP client stack processing\n");
		if (!mrcp_client_destroy(globals.mrcp_client))
			ast_log(LOG_WARNING, "Unable to destroy MRCP client stack\n");
		else
			ast_log(LOG_DEBUG, "MRCP client stack destroyed\n");
		globals.mrcp_client = NULL;
		if (!apt_log_instance_destroy())
			ast_log(LOG_WARNING, "Unable to destroy UniMRCP logger instance\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	#if defined(ASTERISKSVN) || defined(ASTERISK162)
	/* Register the applications. */
	res = ast_register_application_xml(app_synth, app_synth_exec);
	res |= ast_register_application_xml(app_recog, app_recog_exec);
	#else 
	res = ast_register_application(app_synth, app_synth_exec, synthsynopsis, synthdescrip);
	res |= ast_register_application(app_recog, app_recog_exec, recogsynopsis, recogdescrip);
	#endif

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MRCP suite of applications",
	.load = load_module,
	.unload = unload_module,
	.reload = reload
);

/* TO DO:
 *
 * ( ) 1. Barge-in support from Asterisk (used when MRCP server doesn't support it) for ASR
 * ( ) 2. Support for other codecs, fallback to LPCM if MRCP server doesn't support codec
 * ( ) 3. Better handling of errors
 * ( ) 4. Documentation
 *        ( ) install guide ( ), configuration guide ( ), user guide ( ), doxygen documentation ( ), application console+help ( ), etc.
 * ( ) 5. Fetching of grammar, SSML, etc. as URI - support for http, https, ftp, file, odbc, etc. using CURL - flag to indicate if MRCP server should fetch or if we should and then inline the result
 * ( ) 6. Caching of prompts for TTS, functions in console to manage cache, config for settings, etc. - cache to memory, file system or database
 * ( ) 7. Caching of grammar, SSML, etc. - TTS cache, SSML cache, etc.
 * ( ) 8. Example applications
 * ( ) 9. Packaging into a libmrcp library with callbacks for Asterisk specific features
 * ( ) 10. Load tests, look at robustness, load, unexpected things such as killing server in request, etc.
 * ( ) 11. Packaged unimrcpserver with Flite, Festival, Sphinx, HTK, PocketSphinx, RealSpeak modules
 * ( ) 12. Resources/applications for Speaker Verification, Speaker Recognition, Speech Recording
 *
 * NOTE: If you want DTMF recognised, remember to set "codecs = PCMU PCMA L16/96/8000 PCMU/97/16000 telephone-event/101/8000" as telephone-event is important
 */

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */

