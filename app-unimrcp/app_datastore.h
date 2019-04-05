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

#ifndef APP_DATASTORE_H
#define APP_DATASTORE_H

/* Asterisk includes. */
#include "ast_compat_defs.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"

/* UniMRCP includes. */
#include "ast_unimrcp_framework.h"
#include "audio_queue.h"
#include "speech_channel.h"
#include "apt_nlsml_doc.h"

#define DEFAULT_DATASTORE_ENTRY "_default"

/* The enumeration of session lifetimes. */
enum app_session_lifetime {
	APP_SESSION_LIFETIME_DYNAMIC,     /* session is created and destroy per each request */
	APP_SESSION_LIFETIME_PERSISTENT   /* session is created on demand, reused and destroy with Asterisk channel */
};

/* The application session. */
struct app_session_t {
	apr_pool_t            *pool;               /* memory pool */
	int                    lifetime;           /* session lifetime */
	apr_uint32_t           schannel_number;    /* speech channel number */
	speech_channel_t      *recog_channel;      /* recognition channel */
	speech_channel_t      *synth_channel;      /* synthesis channel, if any */
	ast_format_compat     *readformat;         /* old read format, to be restored */
	ast_format_compat     *rawreadformat;      /* old raw read format, to be restored (>= Asterisk 13) */
	ast_format_compat     *writeformat;        /* old write format, to be restored */
	ast_format_compat     *rawwriteformat;     /* old raw write format, to be restored (>= Asterisk 13) */
	ast_format_compat     *nreadformat;        /* new read format used for recognition */
	ast_format_compat     *nwriteformat;       /* new write format used for synthesis */
	apr_array_header_t    *prompts;            /* list of prompt items */
	int                    cur_prompt;         /* current prompt index */
	struct ast_filestream *filestream;         /* filestream, if any */
	off_t                  max_filelength;     /* max file length used with file playing, if any */
	int                    it_policy;          /* input timers policy (sar_it_policies) */
	nlsml_result_t        *nlsml_result;       /* parsed NLSML result */
};

typedef struct app_session_t app_session_t;

/* The structure holding the application data store */
struct app_datastore_t {
	apr_pool_t           *pool;             /* memory pool */
	struct ast_channel   *chan;             /* asterisk channel */
	apr_hash_t           *session_table;    /* session table (const char*, app_session_t*) */
	const char           *name;             /* associated channel name */
	const char           *last_recog_entry; /* entry of last recognition session to get results for */
};

typedef struct app_datastore_t app_datastore_t;

/* Register custom dialplan functions */
int app_datastore_functions_register(struct ast_module *mod);

/* Unregister custom dialplan functions */
int app_datastore_functions_unregister();

/* Get application data from datastore */
app_datastore_t* app_datastore_get(struct ast_channel *chan);

/* Add application session to datastore */
app_session_t* app_datastore_session_add(app_datastore_t* datastore, const char *entry);

#endif /* APP_DATASTORE_H */
