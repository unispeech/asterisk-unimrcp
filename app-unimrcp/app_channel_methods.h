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


/* The enumeration of application options (excluding the MRCP params). */
enum mrcprcogverif_option_flags {
	MRCPRECOGVERIF_PROFILE             = (1 << 0),
	MRCPRECOGVERIF_INTERRUPT           = (1 << 1),
	MRCPRECOGVERIF_FILENAME            = (1 << 2),
	MRCPRECOGVERIF_BARGEIN             = (1 << 3),
	MRCPRECOGVERIF_GRAMMAR_DELIMITERS  = (1 << 4),
	MRCPRECOGVERIF_EXIT_ON_PLAYERROR   = (1 << 5),
	MRCPRECOGVERIF_URI_ENCODED_RESULTS = (1 << 6),
	MRCPRECOGVERIF_OUTPUT_DELIMITERS   = (1 << 7),
	MRCPRECOGVERIF_INPUT_TIMERS        = (1 << 8),
	MRCPRECOGVERIF_PERSISTENT_LIFETIME = (1 << 9),
	MRCPRECOGVERIF_DATASTORE_ENTRY     = (1 << 10),
	MRCPRECOGVERIF_INSTANCE_FORMAT     = (1 << 11),
	MRCPRECOGVERIF_BUF_HND             = (1 << 12)
};

/* The enumeration of option arguments. */
enum mrcprecogverif_option_args {
	OPT_ARG_PROFILE              = 0,
	OPT_ARG_SYNTH_PROFILE        = 1,
	OPT_ARG_INTERRUPT            = 2,
	OPT_ARG_FILENAME             = 3,
	OPT_ARG_BARGEIN              = 4,
	OPT_ARG_GRAMMAR_DELIMITERS   = 5,
	OPT_ARG_EXIT_ON_PLAYERROR    = 6,
	OPT_ARG_URI_ENCODED_RESULTS  = 7,
	OPT_ARG_OUTPUT_DELIMITERS    = 8,
	OPT_ARG_INPUT_TIMERS         = 9,
	OPT_ARG_PERSISTENT_LIFETIME  = 10,
	OPT_ARG_DATASTORE_ENTRY      = 11,
	OPT_ARG_INSTANCE_FORMAT      = 12,
	OPT_ARG_BUF_HND              = 13,
	OPT_ARG_STOP_BARGED_SYNTH    = 14,

	/* This MUST be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE           = 15
};

struct mrcprecogverif_options_t {
	apr_hash_t *synth_hfs;
	apr_hash_t *recog_hfs;
	apr_hash_t *verif_session_hfs;
	apr_hash_t *verif_hfs;
	apr_hash_t *syn_vendor_par_list;
	apr_hash_t *rec_vendor_par_list;
	apr_hash_t *ver_vendor_par_list;

	int         flags;
	const char *params[OPT_ARG_ARRAY_SIZE];
};

typedef struct mrcprecogverif_options_t mrcprecogverif_options_t;

int channel_start_input_timers(speech_channel_t *schannel, mrcp_method_id method_id);
int channel_set_start_of_input(speech_channel_t *schannel);
int channel_set_results(speech_channel_t *schannel, int completion_cause, const apt_str_t *result,
						const apt_str_t *waveform_uri);
int channel_get_completion_cause(speech_channel_t *schannel, const char **completion_cause);
int channel_get_results(speech_channel_t *schannel, const char **completion_cause,
						const char **result, const char **waveform_uri);
int channel_set_timers_started(speech_channel_t *schannel);
int recog_channel_start(speech_channel_t *schannel, const char *name, int start_input_timers,
						mrcprecogverif_options_t *options);
int recog_channel_load_grammar(speech_channel_t *schannel, const char *name, grammar_type_t type,
						const char *data);
int verif_channel_start(speech_channel_t *schannel, const char *name, int start_input_timers,
						mrcprecogverif_options_t *options);
