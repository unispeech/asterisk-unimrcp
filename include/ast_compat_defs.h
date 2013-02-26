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

#ifndef AST_COMPAT_DEFS_H
#define AST_COMPAT_DEFS_H

/*! \file
 *
 * \brief Asterisk compatibility includes and definitions
 *
 * \author Arsen Chaloyan arsen.chaloyan@unimrcp.org
 * 
 * \ingroup applications
 */

#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "apr.h"

/**
 * Check at compile time if the Asterisk version is at least a certain level.
 */
#define AST_VERSION_AT_LEAST(major,minor,patch)                             \
(((major) < ASTERISK_MAJOR_VERSION)                                         \
 || ((major) == ASTERISK_MAJOR_VERSION && (minor) < ASTERISK_MINOR_VERSION) \
 || ((major) == ASTERISK_MAJOR_VERSION && (minor) == ASTERISK_MINOR_VERSION \
     && (patch) <= ASTERISK_PATCH_VERSION))

/**
 * Check at compile time if the Asterisk version is equal to the specified 
 * major.minor version.
 */
#define AST_VERSION_EQUAL(major,minor)                                      \
	((major) == ASTERISK_MAJOR_VERSION && (minor) == ASTERISK_MINOR_VERSION)


/**
 * Backward compatible type definition for application data parameter.
 */
#if AST_VERSION_AT_LEAST(1,8,0)
typedef const char * ast_app_data;
#else
typedef void * ast_app_data;
#endif

/**
 * Channel accessors available since Asterisk 11.
 */
#if !AST_VERSION_AT_LEAST(11,0,0)
static APR_INLINE enum ast_channel_state ast_channel_state(const struct ast_channel *chan)
{
	return chan->_state;
}
static APR_INLINE const char *ast_channel_language(const struct ast_channel *chan)
{
	return chan->language;
}
static APR_INLINE const char *ast_channel_name(const struct ast_channel *chan)
{
	return chan->name;
}
#endif


/**
 * Backward compatible media format definition and utility functions.
 */
#if AST_VERSION_AT_LEAST(10,0,0)
#include "asterisk/format.h"
typedef struct ast_format ast_format_compat;
#else /* <= 1.8 */
struct ast_format_compat {
#if AST_VERSION_AT_LEAST(1,8,0)
	format_t id; /* 1.8 */
#else
	int id;      /* < 1.8 */
#endif
}; 
typedef struct ast_format_compat ast_format_compat;

static APR_INLINE void ast_format_clear(ast_format_compat *format)
{
	format->id = 0;
}
#endif

#if AST_VERSION_AT_LEAST(10,0,0)
static APR_INLINE int ast_channel_set_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	return ast_set_read_format(chan, format);
}
static APR_INLINE int ast_channel_set_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	return ast_set_write_format(chan, format);
}
#else /* <= 1.8 */
static APR_INLINE int ast_channel_set_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	return ast_set_read_format(chan, format->id);
}
static APR_INLINE int ast_channel_set_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	return ast_set_write_format(chan, format->id);
}
#endif

#if AST_VERSION_AT_LEAST(11,0,0)
static APR_INLINE void ast_channel_get_rawreadformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, ast_channel_rawreadformat(chan));
}
static APR_INLINE void ast_channel_get_rawwriteformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, ast_channel_rawwriteformat(chan));
}
static APR_INLINE void ast_channel_get_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, ast_channel_readformat(chan));
}
static APR_INLINE void ast_channel_get_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, ast_channel_writeformat(chan));
}
#elif AST_VERSION_AT_LEAST(10,0,0)
static APR_INLINE void ast_channel_get_rawreadformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, &chan->rawreadformat);
}
static APR_INLINE void ast_channel_get_rawwriteformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, &chan->rawwriteformat);
}
static APR_INLINE void ast_channel_get_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, &chan->readformat);
}
static APR_INLINE void ast_channel_get_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_format_copy(format, &chan->writeformat);
}
#else /* <= 1.8 */
static APR_INLINE void ast_channel_get_rawreadformat(struct ast_channel *chan, ast_format_compat *format)
{
	format->id = chan->rawreadformat;
}
static APR_INLINE void ast_channel_get_rawwriteformat(struct ast_channel *chan, ast_format_compat *format)
{
	format->id = chan->rawwriteformat;
}
static APR_INLINE void ast_channel_get_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	format->id = chan->readformat;
}
static APR_INLINE void ast_channel_get_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	format->id = chan->writeformat;
}
#endif


/**
 * Backward compatible frame accessors.
 */
static APR_INLINE int ast_frame_get_dtmfkey(struct ast_frame *f)
{
#if AST_VERSION_AT_LEAST(1,8,0)
	return f->subclass.integer;
#else
	return f->subclass;
#endif
}

static APR_INLINE void* ast_frame_get_data(const struct ast_frame *f)
{
#if AST_VERSION_AT_LEAST(1,6,0)
	return (void *) (f->data.ptr);
#else
	return (void *)(f->data);
#endif
}

static APR_INLINE void ast_frame_set_data(struct ast_frame *f, void *data)
{
#if AST_VERSION_AT_LEAST(1,6,0)
	f->data.ptr = data;
#else
	f->data = data;
#endif
}

static APR_INLINE void ast_frame_set_format(struct ast_frame *f, const ast_format_compat *format)
{
#if AST_VERSION_AT_LEAST(10,0,0)
	ast_format_copy(&f->subclass.format, format);
#elif AST_VERSION_AT_LEAST(1,8,0)
	f->subclass.codec = format->id;
#else
	f->subclass = format->id;
#endif
}

#endif /* AST_COMPAT_DEFS_H */
