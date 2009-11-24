/* 
 * Asterisk UniMRCP Speech Module
 *
 * Copyright (C) 2009, Arsen Chaloyan  <achaloyan@gmail.com>
 *
 */

/*** MODULEINFO
	<depend>unimrcp</depend>
 ***/

#include "asterisk.h"
#define AST_MODULE "res_speech_unimrcp" 
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/config.h"
#include "asterisk/frame.h"
#include "asterisk/dsp.h"
#include "asterisk/speech.h"


/** \brief Load module */
static int load_module(void)
{
	ast_log(LOG_NOTICE, "Load UniMRCP module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

/** \brief Unload module */
static int unload_module(void)
{
	ast_log(LOG_NOTICE, "Unload UniMRCP module\n");
	return 0;
}


AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "UniMRCP Speech Engine");
