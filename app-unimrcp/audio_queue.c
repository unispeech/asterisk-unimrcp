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

#include "audio_queue.h"
#include "apt_pool.h"


/* Default audio buffer size:
 *
 * 8000 samples/sec * 2 bytes/sample (16-bit) * 1 second = 16000 bytes
 * 16000 samples/sec * 2 bytes/sample (16-bit) * 1 second = 32000 bytes
 *
 * Make provision for 16kHz sample rates with 16-bit samples, 1 second audio.
 */
#define AUDIO_QUEUE_SIZE						(16000 * 2)

#define AUDIO_QUEUE_READ_TIMEOUT_USEC			(30 * 1000000)

/* --- AUDIO BUFFER --- */

static int audio_buffer_create(audio_buffer_t **buffer, apr_size_t max_len)
{
	apr_pool_t *pool;
	audio_buffer_t *new_buffer;

	if (buffer == NULL)
		return -1;
	else
		*buffer = NULL;

	if ((pool = apt_pool_create()) == NULL)
		return -1;

	if ((max_len > 0) && ((new_buffer = apr_palloc(pool, sizeof(audio_buffer_t))) != NULL) && ((new_buffer->data = apr_palloc(pool, max_len)) != NULL)) {
		new_buffer->datalen = max_len;
		new_buffer->pool = pool;
		new_buffer->used = 0;
		*buffer = new_buffer;
		return 0;
	}

	apr_pool_destroy(pool);
	return -1;
}

static void audio_buffer_destroy(audio_buffer_t *buffer)
{
	if (buffer != NULL && buffer->pool != NULL) {
		apr_pool_destroy(buffer->pool);
	}
}

static apr_size_t audio_buffer_inuse(audio_buffer_t *buffer)
{
	if (buffer != NULL)
		return buffer->used;
	else
		return 0;
}

static apr_size_t audio_buffer_read(audio_buffer_t *buffer, void *data, apr_size_t datalen)
{
	apr_size_t reading = 0;

	if ((buffer == NULL) || (buffer->data == NULL) || (data == NULL) || (datalen == 0))
		return 0;

	if (buffer->used < 1) {
		buffer->used = 0;
		return 0;
	} else if (buffer->used >= datalen)
		reading = datalen;
	else
		reading = buffer->used;

	memcpy(data, buffer->data, reading);
	buffer->used = buffer->used - reading;
	memmove(buffer->data, buffer->data + reading, buffer->used);

	return reading;
}

static apr_size_t audio_buffer_write(audio_buffer_t *buffer, const void *data, apr_size_t datalen)
{
	apr_size_t freespace;

	if ((buffer == NULL) || (buffer->data == NULL) || (data == NULL))
		return 0;

	if (datalen == 0)
		return buffer->used;

	freespace = buffer->datalen - buffer->used;

	if (freespace < datalen)
		return 0;

	memcpy(buffer->data + buffer->used, data, datalen);
	buffer->used = buffer->used + datalen;

	return buffer->used;
}

static void audio_buffer_zero(audio_buffer_t *buffer)
{
	if (buffer != NULL)
		buffer->used = 0;
}

/* --- AUDIO QUEUE --- */

/* Empty the queue. */
int audio_queue_clear(audio_queue_t *queue)
{
	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	if (queue->buffer != NULL)
		audio_buffer_zero(queue->buffer);

	if (queue->cond != NULL)
		apr_thread_cond_signal(queue->cond);

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return 0;
}

/* Destroy the audio queue. */
int audio_queue_destroy(audio_queue_t *queue)
{
	if (queue != NULL) {
		char *name = queue->name;
		if ((name == NULL) || (strlen(name) == 0))
			name = "";

		if (queue->buffer != NULL) {
			audio_buffer_zero(queue->buffer);
			audio_buffer_destroy(queue->buffer);
			queue->buffer = NULL;
		}

		if (queue->cond != NULL) {
			if (apr_thread_cond_destroy(queue->cond) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy audio queue condition variable\n", name);

			queue->cond = NULL;
		}

		if (queue->mutex != NULL) {
			if (apr_thread_mutex_destroy(queue->mutex) != APR_SUCCESS)
				ast_log(LOG_WARNING, "(%s) Unable to destroy audio queue mutex\n", name);

			queue->mutex = NULL;
		}

		queue->name = NULL;
		queue->read_bytes = 0;
		queue->waiting = 0;
		queue->write_bytes = 0;

		ast_log(LOG_DEBUG, "(%s) Audio queue destroyed\n", name);
		if (queue->pool != NULL) {
			apr_pool_destroy(queue->pool);
		}
	}

	return 0;
}

/* Create the audio queue. */
int audio_queue_create(audio_queue_t **audio_queue, const char *name)
{
	int status = 0;
	audio_queue_t *laudio_queue = NULL;
	char *lname = "";
	apr_pool_t *pool;

	if (audio_queue == NULL)
		return -1;
	else
		*audio_queue = NULL;

	if ((pool = apt_pool_create()) == NULL)
		return -1;

	if ((name == NULL) || (strlen(name) == 0))
		lname = "";
	else
		lname = apr_pstrdup(pool, name);
	if (lname == NULL)
		lname = "";

	if ((laudio_queue = (audio_queue_t *)apr_palloc(pool, sizeof(audio_queue_t))) == NULL) {
		ast_log(LOG_ERROR, "(%s) Unable to create audio queue\n", lname);
		return -1;
	} else {
		laudio_queue->buffer = NULL;
		laudio_queue->cond = NULL;
		laudio_queue->mutex = NULL;
		laudio_queue->name = lname;
		laudio_queue->pool = pool;
		laudio_queue->read_bytes = 0;
		laudio_queue->waiting = 0;
		laudio_queue->write_bytes = 0;

		if (audio_buffer_create(&laudio_queue->buffer, AUDIO_QUEUE_SIZE) != 0) {
			ast_log(LOG_ERROR, "(%s) Unable to create audio queue buffer\n", laudio_queue->name);
			status = -1;
		} else if (apr_thread_mutex_create(&laudio_queue->mutex, APR_THREAD_MUTEX_UNNESTED, pool) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) Unable to create audio queue mutex\n", laudio_queue->name);
			status = -1;
		} else if (apr_thread_cond_create(&laudio_queue->cond, pool) != APR_SUCCESS) {
			ast_log(LOG_ERROR, "(%s) Unable to create audio queue condition variable\n", laudio_queue->name);
			status = -1;
		} else {
			*audio_queue = laudio_queue;
			ast_log(LOG_DEBUG, "(%s) Audio queue created\n", laudio_queue->name);
		}
	}

	if (status != 0)
		audio_queue_destroy(laudio_queue);

	return status;
}

/* Read from the audio queue. */
apr_status_t audio_queue_read(audio_queue_t *queue, void *data, apr_size_t *data_len, int block)
{
	apr_size_t requested;
	int status = 0;

	if ((queue == NULL) || (data == NULL) || (data_len == NULL))
		return -1;
	else
		requested = *data_len;

	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	/* Wait for data, if allowed. */
	if (block != 0) {
		if (audio_buffer_inuse(queue->buffer) < requested) {
			queue->waiting = requested;

			if ((queue->mutex != NULL) && (queue->cond != NULL))
				apr_thread_cond_timedwait(queue->cond, queue->mutex, AUDIO_QUEUE_READ_TIMEOUT_USEC);

		}

		queue->waiting = 0;
	}

	if (audio_buffer_inuse(queue->buffer) < requested)
		requested = audio_buffer_inuse(queue->buffer);

	if (requested == 0) {
		*data_len = 0;
		status = -1;
	} else {
		/* Read the data. */
		*data_len = audio_buffer_read(queue->buffer, data, requested);
		queue->read_bytes = queue->read_bytes + *data_len;
	}

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return status;
}

/* Write to the audio queue. */
int audio_queue_write(audio_queue_t *queue, void *data, apr_size_t *data_len)
{
	int status = 0;

	if ((queue == NULL) || (data == NULL) || (data_len == NULL))
		return -1;

	if (queue->mutex != NULL)
		apr_thread_mutex_lock(queue->mutex);

	if (audio_buffer_write(queue->buffer, data, *data_len) > 0) {
		queue->write_bytes = queue->write_bytes + *data_len;

		if (queue->waiting <= audio_buffer_inuse(queue->buffer)) {
			if (queue->cond != NULL)
				apr_thread_cond_signal(queue->cond);
		}
	} else {
		ast_log(LOG_WARNING, "(%s) Audio queue overflow!\n", queue->name);
		*data_len = 0;
		status = -1;
	}

	if (queue->mutex != NULL)
		apr_thread_mutex_unlock(queue->mutex);

	return status;
}
