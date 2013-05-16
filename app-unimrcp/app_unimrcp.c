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
 * \author\verbatim Arsen Chaloyan <achaloyan@gmail.com> \endverbatim
 * 
 * MRCP suite of applications
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
	<depend>unimrcp</depend>
	<depend>apr</depend>
 ***/

/* Asterisk includes. */
#include "ast_compat_defs.h"

#if AST_VERSION_AT_LEAST(1,4,0)
#define AST_COMPAT_STATIC static
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")
#else  /* 1.2 */
#define AST_MODULE_LOAD_DECLINE -1
#define AST_COMPAT_STATIC
#endif

#if !AST_VERSION_AT_LEAST(1,6,0)
#include <stdlib.h>
#include <stdio.h>
#endif

#define AST_MODULE "app_unimrcp"

#include "asterisk/module.h"
#include "asterisk/config.h"

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"
#include "recog_datastore.h"

/* The configuration file to read. */
#define MRCP_CONFIG "mrcp.conf"

static int apr_initialized = 0;

/* MRCPSynth application. */ 
int load_mrcpsynth_app();
int unload_mrcpsynth_app();

/* MRCPRecog application. */ 
int load_mrcprecog_app();
int unload_mrcprecog_app();

/* SynthAndRecog application. */ 
int load_synthandrecog_app();
int unload_synthandrecog_app();

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
		case APT_PRIO_ALERT:
		case APT_PRIO_CRITICAL:
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
		case APT_PRIO_DEBUG:
			ast_log(LOG_DEBUG, "%s\n", log_message);
			break;
		default:
			ast_log(LOG_DEBUG, "%s\n", log_message);
			break;
	}

	return TRUE;
}

AST_COMPAT_STATIC int load_module(void)
{
	int res = 0;
	apr_hash_index_t *hi;

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
#if AST_VERSION_AT_LEAST(1,6,0)
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(MRCP_CONFIG, config_flags);
#else
	struct ast_config *cfg = ast_config_load(MRCP_CONFIG);
#endif
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", MRCP_CONFIG);
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_mrcp_config(cfg) != 0) {
		ast_log(LOG_DEBUG, "Unable to load configuration\n");
		globals_destroy();
		apr_terminate();
		apr_initialized = 0;
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Link UniMRCP logs to Asterisk. */
	ast_log(LOG_NOTICE, "UniMRCP log level = %s\n", globals.unimrcp_log_level);
	apt_log_priority_e log_priority = apt_log_priority_translate(globals.unimrcp_log_level);
	if (apt_log_instance_create(APT_LOG_OUTPUT_NONE, log_priority, globals.pool) == FALSE) {
		/* Already created. */
		apt_log_priority_set(log_priority);
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
	
	/* Load the applications. */
	load_mrcpsynth_app();
	load_mrcprecog_app();
	load_synthandrecog_app();

	/* Start the client stack. */
	if (!mrcp_client_start(globals.mrcp_client)) {
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

	/* Register the applications. */
	for (hi = apr_hash_first(NULL, globals.apps); hi; hi = apr_hash_next(hi)) {
		const void *key;
		void *val;
		const char *name;
		ast_mrcp_application_t *application;

		apr_hash_this(hi, &key, NULL, &val);

		name = (const char *) key;
		application = (ast_mrcp_application_t *) val;

#if AST_VERSION_AT_LEAST(1,6,2)
		res |= ast_register_application_xml(name, application->exec);
#else 
		res |= ast_register_application(name, application->exec, application->synopsis, application->description);
#endif
	}

	/* Register the custom functions. */
	res |= recog_datastore_functions_register(ast_module_info->self);

	return res;
}

AST_COMPAT_STATIC int unload_module(void)
{
	int res = 0;
	apr_hash_index_t *hi;

	/* First unregister the applications so no more calls arrive. */
	for (hi = apr_hash_first(NULL, globals.apps); hi; hi = apr_hash_next(hi)) {
		const void *key;
		const char *name;

		apr_hash_this(hi, &key, NULL, NULL);

		name = (const char *) key;

		res |= ast_unregister_application(name);
	}

	/* Unregister the custom functions. */
	res |= recog_datastore_functions_unregister();

	/* Unload the applications. */
	unload_mrcpsynth_app();
	unload_mrcprecog_app();
	unload_synthandrecog_app();

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

AST_COMPAT_STATIC int reload(void)
{
	return 0;
}

#if AST_VERSION_AT_LEAST(1,4,0)
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MRCP suite of applications",
	.load = load_module,
	.unload = unload_module,
	.reload = reload
);
#endif

/* TO DO:
 *
 * ( ) 1. Support for other codecs, fallback to LPCM if MRCP server doesn't support codec
 * ( ) 2. Documentation
 *        ( ) install guide ( ), configuration guide ( ), user guide ( ), doxygen documentation ( ), application console+help ( ), etc.
 * ( ) 3. Fetching of grammar, SSML, etc. as URI - support for http, https, ftp, file, odbc, etc. using CURL - flag to indicate if MRCP server should fetch or if we should and then inline the result
 * ( ) 4. Caching of prompts for TTS, functions in console to manage cache, config for settings, etc. - cache to memory, file system or database
 * ( ) 5. Caching of grammar, SSML, etc. - TTS cache, SSML cache, etc.
 * ( ) 6. Example applications
 * ( ) 7. Packaging into a libmrcp library with callbacks for Asterisk specific features
 * ( ) 8. Resources/applications for Speaker Verification, Speaker Recognition, Speech Recording
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
