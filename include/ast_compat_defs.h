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

#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "apr.h"
#include "apr_pools.h"

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
#if AST_VERSION_AT_LEAST(13,0,0)
#include "asterisk/format_cache.h"
#endif
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

#if AST_VERSION_AT_LEAST(13,0,0)
static APR_INLINE ast_format_compat* ast_get_speechformat(ast_format_compat *raw_format, apr_pool_t *pool)
{
	if(raw_format == ast_format_ulaw || raw_format == ast_format_alaw)
		return raw_format;
	return ast_format_slin;
}
static APR_INLINE const char* format_to_str(const ast_format_compat *format)
{
	if(format == ast_format_ulaw)
		return "PCMU";
	if(format == ast_format_alaw)
		return "PCMA";
	return "L16";
}
static APR_INLINE int format_to_bytes_per_sample(const ast_format_compat *format)
{
	if(format == ast_format_ulaw || format == ast_format_alaw)
		return 1;
	/* linear */
	return 2;
}
#else
static APR_INLINE ast_format_compat* ast_get_speechformat(ast_format_compat *raw_format, apr_pool_t *pool)
{
	ast_format_compat *speech_format = apr_palloc(pool, sizeof(ast_format_compat));
	ast_format_clear(speech_format);
	switch(raw_format->id) {
		/*! Raw mu-law and A-law data (G.711) */
		case AST_FORMAT_ULAW:
		case AST_FORMAT_ALAW:
			speech_format->id = raw_format->id;
			break;
		default:
			speech_format->id = AST_FORMAT_SLINEAR;
	}
	return speech_format;
}
static APR_INLINE const char* format_to_str(const ast_format_compat *format)
{
	const char *str;
	switch(format->id) {
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: str = "PCMU"; break;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: str = "PCMA"; break;
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: str = "L16"; break;
		/*! Use Raw 16-bit Signed Linear (8000 Hz) PCM for the rest */
		default: str = "L16";
	}
	return str;
}
static APR_INLINE int format_to_bytes_per_sample(const ast_format_compat *format)
{
	int bps;
	switch(format->id) {
		/*! Raw mu-law data (G.711) */
		case AST_FORMAT_ULAW: bps = 1; break;
		/*! Raw A-law data (G.711) */
		case AST_FORMAT_ALAW: bps = 1; break;
		/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
		case AST_FORMAT_SLINEAR: bps = 2; break;
		/*! Use Raw 16-bit Signed Linear (8000 Hz) PCM for the rest */
		default: bps = 2;
	}
	return bps;
}
#endif

#if AST_VERSION_AT_LEAST(13,0,0)
#elif AST_VERSION_AT_LEAST(10,0,0)
static APR_INLINE void ast_channel_set_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_set_read_format(chan, format);
}
static APR_INLINE void ast_channel_set_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_set_write_format(chan, format);
}
#else /* <= 1.8 */
static APR_INLINE void ast_channel_set_readformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_set_read_format(chan, format->id);
}
static APR_INLINE void ast_channel_set_writeformat(struct ast_channel *chan, ast_format_compat *format)
{
	ast_set_write_format(chan, format->id);
}
#endif

#if AST_VERSION_AT_LEAST(11,0,0)
static APR_INLINE ast_format_compat* ast_channel_get_speechreadformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat *raw_format = ast_channel_rawreadformat(chan);
	return ast_get_speechformat(raw_format, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_speechwriteformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat *raw_format = ast_channel_rawwriteformat(chan);
	return ast_get_speechformat(raw_format, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_readformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return ast_channel_readformat(chan);
}
static APR_INLINE ast_format_compat* ast_channel_get_writeformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return ast_channel_writeformat(chan);
}
#elif AST_VERSION_AT_LEAST(10,0,0)
static APR_INLINE ast_format_compat* ast_channel_get_speechreadformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return ast_get_speechformat(chan->rawreadformat, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_speechwriteformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return ast_get_speechformat(chan->rawwriteformat, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_readformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return chan->readformat;
}
static APR_INLINE ast_format_compat* ast_channel_get_writeformat(struct ast_channel *chan, apr_pool_t *pool)
{
	return chan->writeformat;
}
#else /* <= 1.8 */
static APR_INLINE ast_format_compat* ast_channel_get_speechreadformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat raw_format;
	ast_format_clear(&raw_format);
	raw_format.id = chan->rawreadformat;
	return ast_get_speechformat(&raw_format, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_speechwriteformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat raw_format;
	ast_format_clear(&raw_format);
	raw_format.id = chan->rawwriteformat;
	return ast_get_speechformat(&raw_format, pool);
}
static APR_INLINE ast_format_compat* ast_channel_get_readformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat *format = apr_palloc(pool, sizeof(ast_format_compat));
	ast_format_clear(format);
	format->id = chan->readformat;
	return format;
}
static APR_INLINE ast_format_compat* ast_channel_get_writeformat(struct ast_channel *chan, apr_pool_t *pool)
{
	ast_format_compat *format = apr_palloc(pool, sizeof(ast_format_compat));
	ast_format_clear(format);
	format->id = chan->writeformat;
	return format;
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
#if AST_VERSION_AT_LEAST(1,6,1)
	return (void *) (f->data.ptr);
#else
	return (void *)(f->data);
#endif
}

static APR_INLINE void ast_frame_set_data(struct ast_frame *f, void *data)
{
#if AST_VERSION_AT_LEAST(1,6,1)
	f->data.ptr = data;
#else
	f->data = data;
#endif
}

static APR_INLINE void ast_frame_set_format(struct ast_frame *f, ast_format_compat *format)
{
#if AST_VERSION_AT_LEAST(13,0,0)
	f->subclass.format = format;
#elif AST_VERSION_AT_LEAST(10,0,0)
	ast_format_copy(&f->subclass.format, format);
#elif AST_VERSION_AT_LEAST(1,8,0)
	f->subclass.codec = format->id;
#else
	f->subclass = format->id;
#endif
}

/**
 * Backward compatible URI encode function.
 */
static APR_INLINE char *ast_uri_encode_http(const char *string, char *outbuf, int buflen)
{
#if AST_VERSION_AT_LEAST(10,0,0)
	return ast_uri_encode(string, outbuf, buflen, ast_uri_http);
#else
	return ast_uri_encode(string, outbuf, buflen, 1);
#endif
}

#endif /* AST_COMPAT_DEFS_H */
