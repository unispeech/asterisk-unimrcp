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

#ifndef RECOG_DATASTORE_H
#define RECOG_DATASTORE_H

/* Asterisk includes. */
#include "ast_compat_defs.h"
#include "asterisk/module.h"

/* Register custom dialplan functions */
int recog_datastore_functions_register(struct ast_module *mod);

/* Unregister custom dialplan functions */
int recog_datastore_functions_unregister();

/* Set result into recog datastore */
int recog_datastore_result_set(struct ast_channel *chan, const char *result);

#endif /* RECOG_DATASTORE_H */
