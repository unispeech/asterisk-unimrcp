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
 * \brief MRCP message process
 *
 * \author\verbatim Fabiano Zaruch Chinasso <fzaruch@cpqd.com.br> \endverbatim
 * 
 * MRCP message process
 * \ingroup applications
 */

/* Asterisk includes. */
#include "ast_compat_defs.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/app.h"

/* UniMRCP includes. */
#include "app_datastore.h"

apt_bool_t mrcp_on_message_receive(mrcp_application_t *application, mrcp_session_t *session,
					mrcp_channel_t *channel, mrcp_message_t *message);
apt_bool_t recog_on_message_receive(mrcp_application_t *application, mrcp_session_t *session,
					mrcp_channel_t *channel, mrcp_message_t *message);
apt_bool_t verif_on_message_receive(mrcp_application_t *application, mrcp_session_t *session,
					mrcp_channel_t *channel, mrcp_message_t *message);

apt_bool_t speech_on_channel_add(mrcp_application_t *application, mrcp_session_t *session,
					mrcp_channel_t *channel, mrcp_sig_status_code_e status);
apt_bool_t speech_on_session_terminate(mrcp_application_t *application, mrcp_session_t *session,
					mrcp_sig_status_code_e status);

apt_bool_t stream_open(mpf_audio_stream_t* stream, mpf_codec_t *codec);
apt_bool_t stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame);
