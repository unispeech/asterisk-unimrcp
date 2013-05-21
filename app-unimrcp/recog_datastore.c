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

#include <stdlib.h>
#include "recog_datastore.h"
#include "asterisk/pbx.h"
#include "apt_nlsml_doc.h"
#include "apt_pool.h"

/*** DOCUMENTATION
	<function name="RECOG_CONFIDENCE" language="en_US">
		<synopsis>
			Get the confidence score of an interpretation.
		</synopsis>
		<syntax>
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the confidence score of the specified interpretation.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</function>
	<function name="RECOG_GRAMMAR" language="en_US">
		<synopsis>
			Get the matched grammar of an interpretation.
		</synopsis>
		<syntax>
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the matched grammar of the specified interpretation.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</function>
	<function name="RECOG_INPUT" language="en_US">
		<synopsis>
			Get the spoken input.
		</synopsis>
		<syntax>
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the spoken input.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</function>
	<function name="RECOG_INSTANCE" language="en_US">
		<synopsis>
			Get the interpreted instance.
		</synopsis>
		<syntax argsep="/">
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
			<parameter name="instance_number" required="false">
				<para>The parameter instance_number specifies the index in the list of instances for a particular interpretation.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the interpreted instance.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
		</see-also>
	</function>
 ***/

/* The structure which holds recognition result */
struct recog_data_t {
	apr_pool_t     *pool;   /* memory pool */
	nlsml_result_t *result; /* parsed NLSMl result */
	const char     *name;   /* associated channel name */
};

typedef struct recog_data_t recog_data_t;

/* Helper function used by datastores to destroy the recognition data structure upon hangup */
static void recog_data_destroy(void *data)
{
	recog_data_t *recog_data = (recog_data_t*) data;

	if (!recog_data || !recog_data->pool) {
		return;
	}

	ast_log(LOG_DEBUG, "Destroy recog datastore on %s\n", recog_data->name);
	apr_pool_destroy(recog_data->pool);
	return;
}

/* Static structure for datastore information */
static const struct ast_datastore_info recog_datastore = {
	.type = "mrcprecog",
	.destroy = recog_data_destroy
};

/* Set result into recog datastore */
int recog_datastore_result_set(struct ast_channel *chan, const char *result)
{
	recog_data_t *recog_data = NULL;
	struct ast_datastore *datastore;
	
	datastore = ast_channel_datastore_find(chan, &recog_datastore, NULL);
	if (datastore) {
		recog_data = datastore->data;
	}
	else {
		apr_pool_t *pool;
		ast_log(LOG_DEBUG, "Create recog datastore on %s\n", ast_channel_name(chan));
		datastore = ast_datastore_alloc(&recog_datastore, NULL);
		if (!datastore) {
			ast_log(LOG_ERROR, "Unable to create recog datastore on %s\n", ast_channel_name(chan));
			return -1;
		}

		if ((pool = apt_pool_create()) == NULL) {
			ast_datastore_free(datastore);
			ast_log(LOG_ERROR, "Unable to create memory pool for recog datastore on %s\n", ast_channel_name(chan));
			return -1;
		}

		recog_data = apr_palloc(pool, sizeof(recog_data_t));
		recog_data->pool = pool;
		recog_data->result = NULL;
		recog_data->name = apr_pstrdup(pool, ast_channel_name(chan));

		datastore->data = recog_data;
		ast_channel_datastore_add(chan, datastore);
	}

	if (!recog_data) {
		ast_log(LOG_ERROR, "Unable to retrieve data from recog datastore on %s\n", ast_channel_name(chan));
		return -1;
	}

	recog_data->result = nlsml_result_parse(result, strlen(result), recog_data->pool);
	return 0;
}

/* Helper function used to find the recognition data structure attached to a channel */
static recog_data_t* recog_datastore_find(struct ast_channel *chan)
{
	recog_data_t *recog_data = NULL;
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &recog_datastore, NULL);
	if (datastore) {
		recog_data = datastore->data;
	}

	return recog_data;
}

/* Helper function used to find an interpretation by specified nbest alternative */
static nlsml_interpretation_t* recog_interpretation_find(recog_data_t *recog_data, const char *nbest_num)
{
	int index = 0;
	nlsml_interpretation_t *interpretation;

	if(!recog_data || !recog_data->result)
		return NULL;

	if (nbest_num)
		index = atoi(nbest_num);

	interpretation = nlsml_first_interpretation_get(recog_data->result);
	while (interpretation) {
		if (index == 0)
			break;

		index --;
		interpretation = nlsml_next_interpretation_get(recog_data->result, interpretation);
	}

	return interpretation;
}

/* Helper function used to find an instance by specified nbest alternative and index */
static nlsml_instance_t* recog_instance_find(recog_data_t *recog_data, const char *num)
{
	int interpretation_index = 0;
	int instance_index = 0;
	nlsml_interpretation_t *interpretation;
	nlsml_instance_t *instance;

	if (!recog_data || !recog_data->result)
		return NULL;

	if (num) {
		char *tmp = NULL;
		if ((tmp = strchr(num, '/'))) {
			*tmp++ = '\0';
			interpretation_index = atoi(num);
			instance_index = atoi(tmp);
		} else {
			instance_index = atoi(num);
		}
	}

	interpretation = nlsml_first_interpretation_get(recog_data->result);
	while (interpretation) {
		if (interpretation_index == 0)
			break;

		interpretation_index --;
		interpretation = nlsml_next_interpretation_get(recog_data->result, interpretation);
	}

	if(!interpretation)
		return NULL;

	instance = nlsml_interpretation_first_instance_get(interpretation);
	while (instance) {
		if (instance_index == 0)
			break;

		instance_index --;
		instance = nlsml_interpretation_next_instance_get(interpretation, instance);
	}

	return instance;
}

/* RECOG_CONFIDENCE() Dialplan Function */
static int recog_confidence(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	recog_data_t *recog_data = recog_datastore_find(chan);
	nlsml_interpretation_t *interpretation = recog_interpretation_find(recog_data, data);
	char tmp[128];

	if (!interpretation)
		return -1;

	snprintf(tmp, sizeof(tmp), "%.2f", nlsml_interpretation_confidence_get(interpretation));
	ast_copy_string(buf, tmp, len);
	return 0;
}

static struct ast_custom_function recog_confidence_function = {
	.name = "RECOG_CONFIDENCE",
	.read = recog_confidence,
	.write = NULL,
};

/* RECOG_GRAMMAR() Dialplan Function */
static int recog_grammar(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	recog_data_t *recog_data = recog_datastore_find(chan);
	nlsml_interpretation_t *interpretation = recog_interpretation_find(recog_data, data);
	const char *grammar;

	if (!interpretation)
		return -1;

	grammar = nlsml_interpretation_grammar_get(interpretation);
	if(!grammar)
		return -1;

	ast_copy_string(buf, grammar, len);
	return 0;
}

static struct ast_custom_function recog_grammar_function = {
	.name = "RECOG_GRAMMAR",
	.read = recog_grammar,
	.write = NULL,
};

/* RECOG_INPUT() Dialplan Function */
static int recog_input(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	recog_data_t *recog_data = recog_datastore_find(chan);
	nlsml_interpretation_t *interpretation = recog_interpretation_find(recog_data, data);
	nlsml_input_t *input;
	const char *text;

	if (!interpretation)
		return -1;

	input = nlsml_interpretation_input_get(interpretation);
	if(!input)
		return -1;

	text = nlsml_input_content_generate(input, recog_data->pool);
	if(!text)
		return -1;

	ast_copy_string(buf, text, len);
	return 0;
}

static struct ast_custom_function recog_input_function = {
	.name = "RECOG_INPUT",
	.read = recog_input,
	.write = NULL,
};

/* RECOG_INSTANCE() Dialplan Function */
static int recog_instance(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	recog_data_t *recog_data = recog_datastore_find(chan);
	nlsml_instance_t *instance = recog_instance_find(recog_data, data);
	const char *text;

	if (!instance)
		return -1;

	text = nlsml_instance_content_generate(instance, recog_data->pool);
	if(!text)
		return -1;

	ast_copy_string(buf, text, len);
	return 0;
}

static struct ast_custom_function recog_instance_function = {
	.name = "RECOG_INSTANCE",
	.read = recog_instance,
	.write = NULL,
};

/* Register custom dialplan functions */
int recog_datastore_functions_register(struct ast_module *mod)
{
	int res = 0;

	res |= __ast_custom_function_register(&recog_confidence_function, mod);
	res |= __ast_custom_function_register(&recog_grammar_function, mod);
	res |= __ast_custom_function_register(&recog_input_function, mod);
	res |= __ast_custom_function_register(&recog_instance_function, mod);

	return res;
}

/* Unregister custom dialplan functions */
int recog_datastore_functions_unregister(struct ast_module *mod)
{
	int res = 0;

	res |= ast_custom_function_unregister(&recog_confidence_function);
	res |= ast_custom_function_unregister(&recog_grammar_function);
	res |= ast_custom_function_unregister(&recog_input_function);
	res |= ast_custom_function_unregister(&recog_instance_function);

	return res;
}
