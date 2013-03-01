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

#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H

#include <apr_general.h>
#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>

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


/* --- AUDIO QUEUE --- */

/* Empty the queue. */
int audio_queue_clear(audio_queue_t *queue);

/* Destroy the audio queue. */
int audio_queue_destroy(audio_queue_t *queue);

/* Create the audio queue. */
int audio_queue_create(audio_queue_t **audio_queue, const char *name);

/* Read from the audio queue. */
apr_status_t audio_queue_read(audio_queue_t *queue, void *data, apr_size_t *data_len, int block);

/* Write to the audio queue. */
int audio_queue_write(audio_queue_t *queue, void *data, apr_size_t *data_len);

#endif /* AUDIO_QUEUE_H */
