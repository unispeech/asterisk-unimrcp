#include <string.h>

#include "apt_pool.h"
#include <libxml/parser.h>

char * result_parser_get_attr(apr_pool_t *pool, const unsigned char *xml, const char* node_name, const char* attr_name) {
	xmlDoc *document;
	xmlNode  *root, *first_child, *node, *node1;
	xmlChar* value;
	char *buf = NULL;

	document = xmlParseDoc(xml);
	root = xmlDocGetRootElement(document);

	first_child = root->children;
	for (node = first_child; (node && !buf); node = node->next) {
		for(node1 = node->children; (node1 && !buf); node1 = node1->next) {
			if (strlen(node_name) != strlen((char*) node1->name))
				continue;
			if (!strncmp((char*) node1->name, node_name, strlen((char*) node1->name))) {
				xmlAttr* attribute = node1->properties;
				while(attribute && attribute->name && attribute->children && !buf) {
					value = xmlNodeListGetString(node->doc, attribute->children, 1);
					if (strlen(attr_name) != strlen((char*) attribute->name)) {
						attribute = attribute->next;
						continue;
					}
					if (!strncmp((char*) attribute->name, attr_name,strlen((char*) attribute->name))) {
						int len = strlen(value);
						buf = apr_palloc(pool, len);
						memcpy(buf, value, len);
						buf[len] = 0;
					}
					xmlFree(value);
					attribute = attribute->next;
				}
			}
		}
	}

	xmlFreeDoc(document);

	return buf;
}