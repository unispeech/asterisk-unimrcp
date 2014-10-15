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
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"

#define DEFAULT_UNIMRCP_MAX_CONNECTION_COUNT	120
#define DEFAULT_UNIMRCP_OFFER_NEW_CONNECTION	1
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


/* Global variables. */
ast_mrcp_globals_t globals;

static void globals_null(void)
{
	/* Set all variables to NULL so that checks work as expected. */
	globals.pool = NULL;
	globals.unimrcp_max_connection_count = NULL;
	globals.unimrcp_offer_new_connection = NULL;
	globals.unimrcp_rx_buffer_size = NULL;
	globals.unimrcp_tx_buffer_size = NULL;
	globals.unimrcp_request_timeout = NULL;
	globals.unimrcp_default_synth_profile = NULL;
	globals.unimrcp_default_recog_profile = NULL;
	globals.unimrcp_log_level = NULL;
	globals.mrcp_client = NULL;
	globals.apps = NULL;
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
			ast_mrcp_profile_t *v;
			void *val;

			apr_hash_this(hi, &key, NULL, &val);
	
			k = (const char *)key;
			v = (ast_mrcp_profile_t *)val;

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

	if (globals.apps != NULL) {
		apr_hash_clear(globals.apps);
	}
}

static void globals_default(void)
{
	/* Initialize some of the variables with default values. */
	globals.unimrcp_max_connection_count = NULL;
	globals.unimrcp_offer_new_connection = NULL;
	globals.unimrcp_log_level = DEFAULT_UNIMRCP_LOG_LEVEL;
	globals.speech_channel_number = 0;
}

void globals_destroy(void)
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

int globals_init(void)
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

	/* Create a hash for the applications. */
	if ((globals.apps = apr_hash_make(globals.pool)) == NULL) {
		ast_log(LOG_ERROR, "Unable to create applications hash\n");
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

/* Return the next number to assign the channel. */
apr_uint32_t get_next_speech_channel_number(void)
{
	apr_uint32_t num;

	if (globals.mutex) apr_thread_mutex_lock(globals.mutex);

	num = globals.speech_channel_number;

	if (globals.speech_channel_number == APR_UINT32_MAX)
		globals.speech_channel_number = 0;
	else
		globals.speech_channel_number++;

	if (globals.mutex) apr_thread_mutex_unlock(globals.mutex);

	return num;
}

ast_mrcp_profile_t* get_synth_profile(const char *option_profile)
{
	const char *profile_name = NULL;
	if (option_profile != NULL) {
		if (strcasecmp(option_profile, "default") == 0)
			profile_name = globals.unimrcp_default_synth_profile;
		else
			profile_name = option_profile;
	} else
		profile_name = globals.unimrcp_default_synth_profile;

	return (ast_mrcp_profile_t *)apr_hash_get(globals.profiles, profile_name, APR_HASH_KEY_STRING);
}

ast_mrcp_profile_t* get_recog_profile(const char *option_profile)
{
	const char *profile_name = NULL;
	if (option_profile != NULL) {
		if (strcasecmp(option_profile, "default") == 0)
			profile_name = globals.unimrcp_default_recog_profile;
		else
			profile_name = option_profile;
	} else
		profile_name = globals.unimrcp_default_recog_profile;

	return (ast_mrcp_profile_t *)apr_hash_get(globals.profiles, profile_name, APR_HASH_KEY_STRING);
}



/* --- PROFILE FUNCTIONS --- */

/* Create a profile. */
int profile_create(ast_mrcp_profile_t **profile, const char *name, const char *version, apr_pool_t *pool)
{
	int res = 0;
	ast_mrcp_profile_t *lprofile = NULL;

	if (profile == NULL)
		return -1;
	else
		*profile = NULL;

	if (pool == NULL)
		return -1;
		
	lprofile = (ast_mrcp_profile_t *)apr_palloc(pool, sizeof(ast_mrcp_profile_t));
	if ((lprofile != NULL) && (name != NULL) && (version != NULL)) {
		if ((lprofile->cfg = apr_hash_make(pool)) != NULL) {
			lprofile->name = apr_pstrdup(pool, name);
			lprofile->version = apr_pstrdup(pool, version);
			lprofile->srgs_mime_type = "application/srgs";
			lprofile->srgs_xml_mime_type = "application/srgs+xml";
			lprofile->gsl_mime_type = "application/x-nuance-gsl";
			lprofile->jsgf_mime_type = "application/x-jsgf";
			lprofile->ssml_mime_type = "application/ssml+xml";
			*profile = lprofile;
		} else
			res = -1;
	} else
		res = -1;

	return res;
}

/* Set specific profile configuration. */
static int process_profile_config(ast_mrcp_profile_t *profile, const char *param, const char *val, apr_pool_t *pool)
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
	else if (strcasecmp(param, "ssml-mime-type") == 0)
		profile->ssml_mime_type = apr_pstrdup(pool, val);
	else
		mine = 0;

	return mine;
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

/* Set RTP config struct with param, val pair. */
static int process_rtp_config(mrcp_client_t *client, mpf_rtp_config_t *rtp_config, mpf_rtp_settings_t *rtp_settings, const char *param, const char *val, apr_pool_t *pool)
{
	int mine = 1;

	if ((client == NULL) || (rtp_config == NULL) || (rtp_settings == NULL) || (param == NULL) || (val == NULL) || (pool == NULL))
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
		rtp_settings->jb_config.initial_playout_delay = atol(val);
	else if (strcasecmp(param, "min-playout-delay") == 0)
		rtp_settings->jb_config.min_playout_delay = atol(val);
	else if (strcasecmp(param, "max-playout-delay") == 0)
		rtp_settings->jb_config.max_playout_delay = atol(val);
	else if (strcasecmp(param, "codecs") == 0) {
		/* Make sure that /etc/mrcp.conf contains the desired codec first in the codecs parameter. */
		const mpf_codec_manager_t *codec_manager = mrcp_client_codec_manager_get(client);
		if (codec_manager != NULL) {
			if (!mpf_codec_manager_codec_list_load(codec_manager, &rtp_settings->codec_list, val, pool))
				ast_log(LOG_WARNING, "Unable to load codecs\n");
		}
	} else if (strcasecmp(param, "ptime") == 0)
		rtp_settings->ptime = (apr_uint16_t)atol(val);
	else if (strcasecmp(param, "rtcp") == 0)
		rtp_settings->rtcp = atoi(val);
	else if  (strcasecmp(param, "rtcp-bye") == 0)
		rtp_settings->rtcp_bye_policy = atoi(val);
	else if (strcasecmp(param, "rtcp-tx-interval") == 0)
		rtp_settings->rtcp_tx_interval = (apr_uint16_t)atoi(val);
	else if (strcasecmp(param, "rtcp-rx-resolution") == 0)
		rtp_settings->rtcp_rx_resolution = (apr_uint16_t)atol(val);
	else
		mine = 0;

	return mine;
}

/* Set RTSP client config struct with param, val pair. */
static int process_mrcpv1_config(rtsp_client_config_t *config, mrcp_sig_settings_t *sig_settings, const char *param, const char *val, apr_pool_t *pool)
{
	int mine = 1;

	if ((config == NULL) || (param == NULL) || (sig_settings == NULL) || (val == NULL) || (pool == NULL))
		return mine;

	if (strcasecmp(param, "server-ip") == 0)
		sig_settings->server_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param, "server-port") == 0)
		sig_settings->server_port = (apr_port_t)atol(val);
	else if (strcasecmp(param, "resource-location") == 0)
		sig_settings->resource_location = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "sdp-origin") == 0)
		config->origin = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "max-connection-count") == 0)
		config->max_connection_count = atol(val);
	else if (strcasecmp(param, "force-destination") == 0)
		sig_settings->force_destination = atoi(val);
	else if ((strcasecmp(param, "speechsynth") == 0) || (strcasecmp(param, "speechrecog") == 0))
		apr_table_set(sig_settings->resource_map, param, val);
	else
		mine = 0;

	return mine;
}

/* Set SofiaSIP client config struct with param, val pair. */
static int process_mrcpv2_config(mrcp_sofia_client_config_t *config, mrcp_sig_settings_t *sig_settings, const char *param, const char *val, apr_pool_t *pool)
{
	int mine = 1;

	if ((config == NULL) || (param == NULL) || (sig_settings == NULL) || (val == NULL) || (pool == NULL))
		return mine;

	if (strcasecmp(param, "client-ip") == 0)
		config->local_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param,"client-ext-ip") == 0)
		config->ext_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param,"client-port") == 0)
		config->local_port = (apr_port_t)atol(val);
	else if (strcasecmp(param, "server-ip") == 0)
		sig_settings->server_ip = ip_addr_get(val, pool);
	else if (strcasecmp(param, "server-port") == 0)
		sig_settings->server_port = (apr_port_t)atol(val);
	else if (strcasecmp(param, "server-username") == 0)
		sig_settings->user_name = apr_pstrdup(pool, val);
	else if (strcasecmp(param, "force-destination") == 0)
		sig_settings->force_destination = atoi(val);
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
mrcp_client_t *mod_unimrcp_client_create(apr_pool_t *mod_pool)
{
	mrcp_client_t *client = NULL;
	apt_dir_layout_t *dir_layout = NULL;
	apr_pool_t *pool = NULL;
	mrcp_resource_factory_t *resource_factory = NULL;
	mpf_codec_manager_t *codec_manager = NULL;
	apr_size_t max_connection_count = 0;
	apt_bool_t offer_new_connection = FALSE;
	mrcp_connection_agent_t *shared_connection_agent = NULL;
	mpf_engine_t *shared_media_engine = NULL;

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

	if ((codec_manager = mpf_engine_codec_manager_create(pool)) != NULL) {
		if (!mrcp_client_codec_manager_register(client, codec_manager))
			ast_log(LOG_WARNING, "Unable to register MRCP client codec manager\n");
	}

	/* Set up MRCPv2 connection agent that will be shared with all profiles. */
	if ((globals.unimrcp_max_connection_count != NULL) && (strlen(globals.unimrcp_max_connection_count) > 0))
		max_connection_count = atoi(globals.unimrcp_max_connection_count);

	if (max_connection_count <= 0)
		max_connection_count = DEFAULT_UNIMRCP_MAX_CONNECTION_COUNT;

	if (globals.unimrcp_offer_new_connection != NULL) {
		if (strcasecmp(globals.unimrcp_offer_new_connection, "true") == 0 || atoi(globals.unimrcp_offer_new_connection) == 1)
			offer_new_connection = TRUE;
	}
	else {
		offer_new_connection = DEFAULT_UNIMRCP_OFFER_NEW_CONNECTION;
	}

	if ((shared_connection_agent = mrcp_client_connection_agent_create("MRCPv2ConnectionAgent", max_connection_count, offer_new_connection, pool)) != NULL) {
		if (shared_connection_agent != NULL) {
 			if (globals.unimrcp_rx_buffer_size != NULL) {
				apr_size_t rx_buffer_size = (apr_size_t)atol(globals.unimrcp_rx_buffer_size);
				if (rx_buffer_size > 0) {
	 				mrcp_client_connection_rx_size_set(shared_connection_agent, rx_buffer_size);
				}
 			}
 			if (globals.unimrcp_tx_buffer_size != NULL) {
				apr_size_t tx_buffer_size = (apr_size_t)atol(globals.unimrcp_tx_buffer_size);
				if (tx_buffer_size > 0) {
	 				mrcp_client_connection_tx_size_set(shared_connection_agent, tx_buffer_size);
				}
 			}
			if (globals.unimrcp_request_timeout != NULL) {
				apr_size_t request_timeout = (apr_size_t)atol(globals.unimrcp_request_timeout);
				if (request_timeout > 0) {
	 				mrcp_client_connection_timeout_set(shared_connection_agent, request_timeout);
				}
			}
 		}

		if (!mrcp_client_connection_agent_register(client, shared_connection_agent))
			ast_log(LOG_WARNING, "Unable to register MRCP client connection agent\n");
	}

	/* Set up the media engine that will be shared with all profiles. */
	if ((shared_media_engine = mpf_engine_create("MediaEngine", pool)) != NULL) {
		unsigned long realtime_rate = 1;

		if (!mpf_engine_scheduler_rate_set(shared_media_engine, realtime_rate))
			ast_log(LOG_WARNING, "Unable to set scheduler rate for MRCP client media engine\n");

		if (!mrcp_client_media_engine_register(client, shared_media_engine))
			ast_log(LOG_WARNING, "Unable to register MRCP client media engine\n");
	}

	if (globals.profiles) {
		apr_hash_index_t *hi;

		for (hi = apr_hash_first(NULL, globals.profiles); hi; hi = apr_hash_next(hi)) {
			const char *k;
			ast_mrcp_profile_t *v;
			const void *key;
			void *val;

			apr_hash_this(hi, &key, NULL, &val);

			k = (const char *)key;
			v = (ast_mrcp_profile_t *)val;

			if (v != NULL) {
				ast_log(LOG_DEBUG, "Processing profile %s:%s\n", k, v->version);

				/* A profile is a signaling agent + termination factory + media engine + connection agent (MRCPv2 only). */
				mrcp_sig_agent_t *agent = NULL;
				mpf_termination_factory_t *termination_factory = NULL;
				mrcp_profile_t * mprofile = NULL;
				mpf_rtp_config_t *rtp_config = NULL;
				mpf_rtp_settings_t *rtp_settings = mpf_rtp_settings_alloc(pool);
				mrcp_sig_settings_t *sig_settings = mrcp_signaling_settings_alloc(pool);
				ast_mrcp_profile_t *mod_profile = NULL;
				mrcp_connection_agent_t *connection_agent = NULL;
				mpf_engine_t *media_engine = shared_media_engine;

				/* Get profile attributes. */
				const char *name = apr_pstrdup(mod_pool, k);
				const char *version = apr_pstrdup(mod_pool, v->version);

				if ((name == NULL) || (strlen(name) == 0) || (version == NULL) || (strlen(version) == 0)) {
					ast_log(LOG_ERROR, "Profile %s missing name or version attribute\n", k);
					return NULL;
				}

				/* Create RTP config, common to MRCPv1 and MRCPv2. */
				if ((rtp_config = mpf_rtp_config_alloc(pool)) == NULL) {
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
					if (globals.unimrcp_request_timeout != NULL) {
						config->request_timeout = (apr_size_t)atol(globals.unimrcp_request_timeout);
					}
					sig_settings->resource_location = DEFAULT_RESOURCE_LOCATION;

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

							if ((!process_mrcpv1_config(config, sig_settings, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, rtp_settings, param_name, param_value, pool)) &&
								(!process_profile_config(mod_profile, param_name, param_value, mod_pool))) {
								ast_log(LOG_WARNING, "Unknown parameter %s\n", param_name);
							}
						}
					}

					agent = mrcp_unirtsp_client_agent_create(name, config, pool);
				} else if (strcmp("2", version) == 0) {
					/* MRCPv2 configuration. */
					mrcp_sofia_client_config_t *config = mrcp_sofiasip_client_config_alloc(pool);
	
					if (config == NULL) {
						ast_log(LOG_ERROR, "Unable to create SIP configuration\n");
						return NULL;
					}

					config->local_ip = DEFAULT_LOCAL_IP_ADDRESS;
					config->local_port = DEFAULT_SIP_LOCAL_PORT;
					sig_settings->server_ip = DEFAULT_REMOTE_IP_ADDRESS;
					sig_settings->server_port = DEFAULT_SIP_REMOTE_PORT;
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

							if ((!process_mrcpv2_config(config, sig_settings, param_name, param_value, pool)) &&
								(!process_rtp_config(client, rtp_config, rtp_settings, param_name, param_value, pool)) &&
								(!process_profile_config(mod_profile, param_name, param_value, mod_pool))) {
								ast_log(LOG_WARNING, "Unknown parameter %s\n", param_name);
							}
						}
					}

					agent = mrcp_sofiasip_client_agent_create(name, config, pool);
					connection_agent = shared_connection_agent;
				} else {
					ast_log(LOG_ERROR, "Version must be either \"1\" or \"2\"\n");
					return NULL;
				}

				if ((termination_factory = mpf_rtp_termination_factory_create(rtp_config, pool)) != NULL)
					mrcp_client_rtp_factory_register(client, termination_factory, name);

				if (rtp_settings != NULL)
					mrcp_client_rtp_settings_register(client, rtp_settings, "RTP-Settings");

				if (sig_settings != NULL)
					mrcp_client_signaling_settings_register(client, sig_settings, "Signalling-Settings");

				if (agent != NULL)
					mrcp_client_signaling_agent_register(client, agent);

				/* Create the profile and register it. */
				if ((mprofile = mrcp_client_profile_create(NULL, agent, connection_agent, media_engine, termination_factory, rtp_settings, sig_settings, pool)) != NULL) {
					if (!mrcp_client_profile_register(client, mprofile, name))
						ast_log(LOG_WARNING, "Unable to register MRCP client profile\n");
				}
			}
		}
	}

	return client;
}

/* --- ASTERISK SPECIFIC CONFIGURATION --- */

int load_mrcp_config(const char *filename, const char *who_asked)
{
	const char *cat = NULL;
	struct ast_variable *var;
	const char *value = NULL;

#if AST_VERSION_AT_LEAST(1,6,0)
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load2(filename, who_asked, config_flags);
#else
	struct ast_config *cfg = ast_config_load(filename);
#endif
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", filename);
		return -1;
	}
#if AST_VERSION_AT_LEAST(1,6,2)
	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format\n", filename);
		return -1;
	}
#endif

	globals_clear();
	globals_default();

	if ((value = ast_variable_retrieve(cfg, "general", "default-tts-profile")) != NULL) {
		ast_log(LOG_DEBUG, "general.default-tts-profile=%s\n",  value);
		globals.unimrcp_default_synth_profile = apr_pstrdup(globals.pool, value);
	} else {
		ast_log(LOG_ERROR, "Unable to load genreal.default-tts-profile from config file, aborting\n");
	   	ast_config_destroy(cfg);
		return -1;
	}
	if ((value = ast_variable_retrieve(cfg, "general", "default-asr-profile")) != NULL) {
		ast_log(LOG_DEBUG, "general.default-asr-profile=%s\n",  value);
		globals.unimrcp_default_recog_profile = apr_pstrdup(globals.pool, value);
	} else {
		ast_log(LOG_ERROR, "Unable to load genreal.default-asr-profile from config file, aborting\n");
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
	if ((value = ast_variable_retrieve(cfg, "general", "request-timeout")) != NULL) {
		ast_log(LOG_DEBUG, "general.request-timeout=%s\n",  value);
		globals.unimrcp_request_timeout = apr_pstrdup(globals.pool, value);
	}

	while ((cat = ast_category_browse(cfg, cat)) != NULL) {
		if (strcasecmp(cat, "general") != 0) {
			if ((value = ast_variable_retrieve(cfg, cat, "version")) != NULL) {
				ast_mrcp_profile_t *mod_profile = NULL;

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
