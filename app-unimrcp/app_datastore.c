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
#include <apr_hash.h>
#include "app_datastore.h"
#include "asterisk/pbx.h"
#include "apt_pool.h"
#ifdef WITH_AST_JSON
#include "asterisk/json.h"
typedef struct ast_json ast_json;
typedef struct ast_json_error ast_json_error;
#endif

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
			<ref type="function">RECOG_INPUT_MODE</ref>
			<ref type="function">RECOG_INPUT_CONFIDENCE</ref>
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
			<ref type="function">RECOG_INPUT_MODE</ref>
			<ref type="function">RECOG_INPUT_CONFIDENCE</ref>
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
			<ref type="function">RECOG_INPUT_MODE</ref>
			<ref type="function">RECOG_INPUT_CONFIDENCE</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</function>
	<function name="RECOG_INPUT_MODE" language="en_US">
		<synopsis>
			Get the mode of an input.
		</synopsis>
		<syntax>
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the mode of the specified input.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INPUT_CONFIDENCE</ref>
			<ref type="function">RECOG_INSTANCE</ref>
		</see-also>
	</function>
	<function name="RECOG_INPUT_CONFIDENCE" language="en_US">
		<synopsis>
			Get the confidence score of an input.
		</synopsis>
		<syntax>
			<parameter name="nbest_number" required="false">
				<para>The parameter nbest_number specifies the index in the list of interpretations sorted best-first.
				This parameter defaults to 0, if not specified.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the confidence score of the specified input.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INPUT_MODE</ref>
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
			<parameter name="path" required="false">
				<para>The parameter path specifies a particular nested element to return, if used.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns the interpreted instance.</para>
		</description>
		<see-also>
			<ref type="function">RECOG_CONFIDENCE</ref>
			<ref type="function">RECOG_GRAMMAR</ref>
			<ref type="function">RECOG_INPUT</ref>
			<ref type="function">RECOG_INPUT_MODE</ref>
			<ref type="function">RECOG_INPUT_CONFIDENCE</ref>
		</see-also>
	</function>
 ***/
 
/* Helper function to destroy application session */
static void app_session_destroy(app_session_t *app_session)
{
	if(app_session) {
		if (app_session->synth_channel) {
			speech_channel_destroy(app_session->synth_channel);
		}

		if (app_session->recog_channel) {
			speech_channel_destroy(app_session->recog_channel);
		}
	}
}

/* Helper function used by datastores to destroy the application data structure upon hangup */
static void app_datastore_destroy(void *data)
{
	app_datastore_t *app_data = (app_datastore_t*) data;
	if (!app_data || !app_data->pool) {
		return;
	}

	void *val;
	apr_hash_index_t *it = apr_hash_first(app_data->pool, app_data->session_table);
	for(; it; it = apr_hash_next(it)) {
		apr_hash_this(it, NULL, NULL, &val);
		app_session_destroy(val);
	}

	ast_log(LOG_DEBUG, "Destroy app datastore on %s\n", app_data->name);
	apr_pool_destroy(app_data->pool);
}

/* Static structure for datastore information */
static const struct ast_datastore_info app_unimrcp_datastore = {
	.type = "app_unimrcp",
	.destroy = app_datastore_destroy
};

app_datastore_t* app_datastore_get(struct ast_channel *chan)
{
	app_datastore_t *app_datastore = NULL;
	struct ast_datastore *datastore;
	
	datastore = ast_channel_datastore_find(chan, &app_unimrcp_datastore, NULL);
	if (datastore) {
		app_datastore = datastore->data;
	}
	else {
		apr_pool_t *pool;
		ast_log(LOG_DEBUG, "Create app datastore on %s\n", ast_channel_name(chan));
		datastore = ast_datastore_alloc(&app_unimrcp_datastore, NULL);
		if (!datastore) {
			ast_log(LOG_ERROR, "Unable to create app datastore on %s\n", ast_channel_name(chan));
			return NULL;
		}

		if ((pool = apt_pool_create()) == NULL) {
			ast_datastore_free(datastore);
			ast_log(LOG_ERROR, "Unable to create memory pool for app datastore on %s\n", ast_channel_name(chan));
			return NULL;
		}

		app_datastore = apr_palloc(pool, sizeof(app_datastore_t));
		app_datastore->pool = pool;
		app_datastore->chan = chan;
		app_datastore->session_table = apr_hash_make(pool);
		app_datastore->name = apr_pstrdup(pool, ast_channel_name(chan));
		app_datastore->last_recog_entry = NULL;

		datastore->data = app_datastore;
		ast_channel_datastore_add(chan, datastore);
	}
	
	return app_datastore;
}

app_session_t* app_datastore_session_add(app_datastore_t* app_datastore, const char *entry)
{
	app_session_t *session;
	if (!app_datastore || !entry)
		return NULL;
	
	session = apr_hash_get(app_datastore->session_table, entry, APR_HASH_KEY_STRING);
	if (session) {
		ast_log(LOG_DEBUG, "Ref entry %s from datastore on %s\n", entry, ast_channel_name(app_datastore->chan));
	}
	else {
		session = apr_palloc(app_datastore->pool, sizeof(app_session_t));

		session->pool = app_datastore->pool;
		session->schannel_number = get_next_speech_channel_number();
		session->lifetime = APP_SESSION_LIFETIME_DYNAMIC;
		session->recog_channel = NULL;
		session->synth_channel = NULL;
		session->readformat = NULL;
		session->rawreadformat = NULL;
		session->writeformat = NULL; 
		session->rawwriteformat = NULL;
		session->nreadformat = NULL;
		session->nwriteformat = NULL;
		session->nlsml_result = NULL;
		session->stop_barged_synth = FALSE;
		session->instance_format = NLSML_INSTANCE_FORMAT_XML;
		session->replace_new_lines = 0;
		ast_log(LOG_DEBUG, "Add entry %s to datastore on %s\n", entry, ast_channel_name(app_datastore->chan));
		apr_hash_set(app_datastore->session_table, entry, APR_HASH_KEY_STRING, session);
	}

	session->prompts = NULL;
	session->cur_prompt = 0;
	session->filestream = NULL;
	session->max_filelength = 0;
	session->it_policy = 0;
	return session;
}

/* Helper function used to find the application data structure attached to a channel */
static app_session_t* app_datastore_session_find(struct ast_channel *chan)
{
	app_datastore_t *app_datastore = NULL;
	struct ast_datastore *datastore;
	
	datastore = ast_channel_datastore_find(chan, &app_unimrcp_datastore, NULL);
	if (datastore) {
		app_datastore = datastore->data;
	}
	
	if (!app_datastore) {
		ast_log(LOG_ERROR, "Unable to find app datastore on %s\n", ast_channel_name(chan));
		return NULL;
	}

	if (!app_datastore->last_recog_entry) {
		ast_log(LOG_ERROR, "Unable to find last session in app datastore on %s\n", ast_channel_name(chan));
		return NULL;
	}

	app_session_t *session = apr_hash_get(app_datastore->session_table, app_datastore->last_recog_entry, APR_HASH_KEY_STRING);
	if (!session) {
		ast_log(LOG_ERROR, "Unable to find entry %s in app datastore on %s\n", app_datastore->last_recog_entry, ast_channel_name(chan));
		return NULL;
	}
	
	return session;
}

/* Helper function used to find an interpretation by specified nbest alternative */
static nlsml_interpretation_t* recog_interpretation_find(app_session_t *app_session, const char *nbest_num)
{
	int index = 0;
	nlsml_interpretation_t *interpretation;

	if(!app_session || !app_session->nlsml_result)
		return NULL;

	if (nbest_num)
		index = atoi(nbest_num);

	interpretation = nlsml_first_interpretation_get(app_session->nlsml_result);
	while (interpretation) {
		if (index == 0)
			break;

		index --;
		interpretation = nlsml_next_interpretation_get(app_session->nlsml_result, interpretation);
	}

	return interpretation;
}

/* Helper function used to find an instance by specified nbest alternative and index */
static nlsml_instance_t* recog_instance_find(app_session_t *app_session, const char *num, const char **path)
{
	int interpretation_index = 0;
	int instance_index = 0;
	nlsml_interpretation_t *interpretation;
	nlsml_instance_t *instance;

	if (!app_session || !app_session->nlsml_result)
		return NULL;

	if (path) {
		*path = NULL;
	}

	if (num) {
		char *tmp = NULL;
		if ((tmp = strchr(num, '/'))) {
			*tmp++ = '\0';
			interpretation_index = atoi(num);
			instance_index = atoi(tmp);
			if ((tmp = strchr(tmp, '/'))) {
				*tmp++ = '\0';
				if (path) {
					*path = tmp;
				}
			}
		} else {
			instance_index = atoi(num);
		}
	}

	interpretation = nlsml_first_interpretation_get(app_session->nlsml_result);
	while (interpretation) {
		if (interpretation_index == 0)
			break;

		interpretation_index --;
		interpretation = nlsml_next_interpretation_get(app_session->nlsml_result, interpretation);
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
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;
	
	nlsml_interpretation_t *interpretation = recog_interpretation_find(app_session, data);
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
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;

	nlsml_interpretation_t *interpretation = recog_interpretation_find(app_session, data);
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
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;

	nlsml_interpretation_t *interpretation = recog_interpretation_find(app_session, data);
	nlsml_input_t *input;
	const char *text;

	if (!interpretation)
		return -1;

	input = nlsml_interpretation_input_get(interpretation);
	if(!input)
		return -1;

	text = nlsml_input_content_generate(input, app_session->pool);
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

/* RECOG_INPUT_MODE() Dialplan Function */
static int recog_input_mode(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;
	
	nlsml_interpretation_t *interpretation = recog_interpretation_find(app_session, data);
	nlsml_input_t *input;
	const char *mode;

	if (!interpretation)
		return -1;

	input = nlsml_interpretation_input_get(interpretation);
	if(!input)
		return -1;

	mode = nlsml_input_mode_get(input);
	if(!mode)
		return -1;

	ast_copy_string(buf, mode, len);
	return 0;
}

static struct ast_custom_function recog_input_mode_function = {
	.name = "RECOG_INPUT_MODE",
	.read = recog_input_mode,
	.write = NULL,
};

/* RECOG_INPUT_CONFIDENCE() Dialplan Function */
static int recog_input_confidence(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;
	
	nlsml_interpretation_t *interpretation = recog_interpretation_find(app_session, data);
	nlsml_input_t *input;
	char tmp[128];

	if (!interpretation)
		return -1;

	input = nlsml_interpretation_input_get(interpretation);
	if(!input)
		return -1;

	snprintf(tmp, sizeof(tmp), "%.2f", nlsml_input_confidence_get(input));
	ast_copy_string(buf, tmp, len);
	return 0;
}

static struct ast_custom_function recog_input_confidence_function = {
	.name = "RECOG_INPUT_CONFIDENCE",
	.read = recog_input_confidence,
	.write = NULL,
};

/* Helper recursive function used to find a nested element based on specified path */
static const apr_xml_elem* recog_instance_find_elem(const apr_xml_elem *elem, const char **path)
{
	char *tmp;
	if ((tmp = strchr(*path, '/'))) {
		*tmp++ = '\0';
	}

	const apr_xml_elem *child_elem;
	for (child_elem = elem->first_child; child_elem; child_elem = child_elem->next) {
		if (strcasecmp(child_elem->name, *path) == 0) {
			if (tmp) {
				*path = tmp;
				return recog_instance_find_elem(child_elem, path);
			}
			return child_elem;
		}
	}
	return NULL;
}

static int recog_instance_replace_char(char *str, char find_char, char replace_char)
{
	char *ptr = str;
	int n = 0;
	while ((ptr = strchr(ptr, find_char)) != NULL) {
		*ptr++ = replace_char;
		n++;
	}
	return n;
}

/* Helper function used to process XML data in NLSML instance */
static int recog_instance_process_xml(app_session_t *app_session, nlsml_instance_t *instance, const char *path, const char **text)
{
	const apr_xml_elem *child_elem;
	const apr_xml_elem *elem = nlsml_instance_elem_get(instance);
	if (!elem)
		return -1;
		
	child_elem = recog_instance_find_elem(elem, &path);
	if(child_elem) {
		apr_size_t size;
		apr_xml_to_text(app_session->pool, child_elem, APR_XML_X2T_INNER, NULL, NULL, text, &size);
	}
	return 0;
}

#ifdef WITH_AST_JSON
/* Helper recursive function used to find a nested object based on specified path */
static ast_json* recog_instance_find_json_object(ast_json *json, const char **path)
{
	char *tmp;
	if ((tmp = strchr(*path, '/'))) {
		*tmp++ = '\0';
	}
	
	ast_json *child_json = NULL;
	if (ast_json_typeof(json) == AST_JSON_ARRAY) {
		int index = atoi(*path);
		child_json = ast_json_array_get(json, index);
	}
	else {
		child_json = ast_json_object_get(json, *path);
	}

	if (!child_json){
		ast_log(LOG_DEBUG, "No such JSON object %s\n", *path);
		child_json = ast_json_null();
	}
	
	if (tmp) {
		*path = tmp;
		return recog_instance_find_json_object(child_json, path);
	}
	
	return child_json;
}

/* Helper function used to process JSON data in NLSML instance */
static int recog_instance_process_json(app_session_t *app_session, nlsml_instance_t *instance, const char *path, const char **text)
{
	const char *json_string = nlsml_instance_content_generate(instance, app_session->pool);
	if (!json_string) {
		return -1;
	}
	
	ast_json_error error;
	ast_json *child_json;
	ast_json *json = ast_json_load_string(json_string, &error);
	if (!json) {
		ast_log(LOG_ERROR, "Unable to load JSON: %s\n", error.text);
		return -1;
	}
	
	char* buf = NULL;
	child_json = recog_instance_find_json_object(json, &path);
	if (child_json) {
		switch (ast_json_typeof(child_json))
		{
			case AST_JSON_NULL:
				buf = apr_pstrdup(app_session->pool, "null");
				break;
			case AST_JSON_TRUE:
				buf = apr_pstrdup(app_session->pool, "true");
				break;
			case AST_JSON_FALSE:
				buf = apr_pstrdup(app_session->pool, "false");
				break;
			case AST_JSON_INTEGER:
				buf = apr_psprintf(app_session->pool, "%ld", ast_json_integer_get(child_json));
				break;
			case AST_JSON_REAL:
				buf = apr_psprintf(app_session->pool, "%.3f", ast_json_real_get(child_json));
				break;
			case AST_JSON_STRING:
			{
				const char *str = ast_json_string_get(child_json);
				if (str)
					buf = apr_pstrdup(app_session->pool, str);
				break;
			}
			case AST_JSON_OBJECT:
			case AST_JSON_ARRAY:
			{
				char *str = ast_json_dump_string(child_json);
				if (str) {
					buf = apr_pstrdup(app_session->pool, str);
					ast_json_free(str);
				}
				break;
			}
			default:
				break;
		}
	}
	
	if (buf) {
		*text = buf;
	}
	
	return 0;
}
#else
static int recog_instance_process_json(app_session_t *app_session, nlsml_instance_t *instance, const char *path, const char **text)
{
	ast_log(LOG_NOTICE, "JSON support is not available\n");
	return -1;
}
#endif

/* RECOG_INSTANCE() Dialplan Function */
static int recog_instance(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	app_session_t *app_session = app_datastore_session_find(chan);
	if(!app_session)
		return -1;

	const char *path = NULL;
	nlsml_instance_t *instance = recog_instance_find(app_session, data, &path);
	if (!instance)
		return -1;

	const char *text = NULL;
	if (path) {
		if (app_session->instance_format == NLSML_INSTANCE_FORMAT_XML) {
			recog_instance_process_xml(app_session, instance, path, &text);
		}
		else if (app_session->instance_format == NLSML_INSTANCE_FORMAT_JSON) {
			recog_instance_process_json(app_session, instance, path, &text);
		}
	}
	else {
		text = nlsml_instance_content_generate(instance, app_session->pool);
	}
	if(!text)
		return -1;

	ast_copy_string(buf, text, len);
	if (app_session->replace_new_lines) {
		recog_instance_replace_char(buf, '\n', app_session->replace_new_lines);
	}
	return 0;
}

static struct ast_custom_function recog_instance_function = {
	.name = "RECOG_INSTANCE",
	.read = recog_instance,
	.write = NULL,
};

/* Register custom dialplan functions */
int app_datastore_functions_register(struct ast_module *mod)
{
	int res = 0;

	res |= __ast_custom_function_register(&recog_confidence_function, mod);
	res |= __ast_custom_function_register(&recog_grammar_function, mod);
	res |= __ast_custom_function_register(&recog_input_function, mod);
	res |= __ast_custom_function_register(&recog_input_mode_function, mod);
	res |= __ast_custom_function_register(&recog_input_confidence_function, mod);
	res |= __ast_custom_function_register(&recog_instance_function, mod);

	return res;
}

/* Unregister custom dialplan functions */
int app_datastore_functions_unregister(struct ast_module *mod)
{
	int res = 0;

	res |= ast_custom_function_unregister(&recog_confidence_function);
	res |= ast_custom_function_unregister(&recog_grammar_function);
	res |= ast_custom_function_unregister(&recog_input_function);
	res |= ast_custom_function_unregister(&recog_input_mode_function);
	res |= ast_custom_function_unregister(&recog_input_confidence_function);
	res |= ast_custom_function_unregister(&recog_instance_function);

	return res;
}
