/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
/* libavs -- simple sync engine
 *
 * Voice Messaging
 */

#include <string.h>
#include <re.h>
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_flowmgr.h"
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_string.h"
#include "avs_engine.h"
#include "module.h"
#include "engine.h"
#include "event.h"
#include "user.h"
#include "conv.h"
#include "call.h"

struct engine_call_data {
	struct flowmgr *flowmgr;
};

/*** init handler
 */

static int init_handler(void)
{
	int err = 0;

	return err;
}


/*** alloc handler
 */

static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
    int err = 0;
    
	return err;
}


/*** close handler
 */

static void close_handler(void)
{
	
}

static void player_status_handler(bool is_playing, unsigned int cur_time_ms, unsigned int file_length_ms, void *arg)
{
    if(is_playing){
        debug("cur_time_ms = %d \n", cur_time_ms);
    } else {
        debug("Player stopped ! \n");
    }
}

/*** engine_call_module
 */

struct engine_module engine_voice_message_module = {
	.name = "voice_message",
	.inith = init_handler,
	.alloch = alloc_handler,
	.closeh = close_handler,
};


/*** engine_voice_message_start_rec
 */

int engine_voice_message_start_rec(struct engine *engine, const char fileNameUTF8[1024])
{
    if (!engine || !engine->call)
		return EINVAL;

	return flowmgr_vm_start_record(engine->call->flowmgr, fileNameUTF8);
}


/*** engine_voice_message_stop_rec
 */

int engine_voice_message_stop_rec(struct engine *engine)
{
	if (!engine || !engine->call)
		return EINVAL;

	return flowmgr_vm_stop_record(engine->call->flowmgr);
}

/*** engine_voice_message_start_play
 */

int engine_voice_message_start_play(struct engine *engine, const char fileNameUTF8[1024])
{
    if (!engine || !engine->call)
        return EINVAL;
    
    return flowmgr_vm_start_play(engine->call->flowmgr, fileNameUTF8, 0, &player_status_handler, NULL);
}


/*** engine_voice_message_stop_play
 */

int engine_voice_message_stop_play(struct engine *engine)
{
    if (!engine || !engine->call)
        return EINVAL;
    
    return flowmgr_vm_stop_play(engine->call->flowmgr);
}
