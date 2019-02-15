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
 * Calling
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
#include "avs_trace.h"
#include "avs_engine.h"
#include "avs_cert.h"
#include "module.h"
#include "engine.h"
#include "event.h"
#include "user.h"
#include "conv.h"
#include "call.h"


struct engine_call_data {
	struct flowmgr *flowmgr;	
	struct engine_module_state *state;

	engine_call_shutdown_h *shuth;
	void *shuth_arg;
};


/*** engine_get_flowmgr
 */

struct flowmgr *engine_get_flowmgr(struct engine *engine)
{
	if (!engine || !engine->call)
		return NULL;

	return engine->call->flowmgr;
}

void engine_call_set_shutdown_handler(struct engine *engine,
				      engine_call_shutdown_h *shuth,
				      void *arg)
{
	engine->call->shuth = shuth;
	engine->call->shuth_arg = arg;
}


/*** init handler
 */

static int init_handler(void)
{
	int err;

	if (str_isset(engine_get_msys())) {

		err = flowmgr_init(engine_get_msys());
		if (err)
			return err;
	}

	return 0;
}


/*** alloc handler
 */

static void engine_call_data_destructor(void *arg)
{
	struct engine_call_data *data = arg;

	mem_deref(data->flowmgr);
}


static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	struct engine_call_data *data;
	int err;

	data = mem_zalloc(sizeof(*data), engine_call_data_destructor);
	if (!data)
		return ENOMEM;

	err = flowmgr_alloc(&data->flowmgr, NULL,
			    NULL, engine);
	if (err)
		goto out;

	engine->call = data;
	engine->call->state = state;
	list_append(&engine->modulel, &state->le, state);

 out:
	if (err) {
		mem_deref(state);
		mem_deref(data);
	}
	return err;
}


/*** close handler
 */

static void close_handler(void)
{
	flowmgr_close();
}


void engine_call_shutdown(struct engine *engine)
{
	engine->call->state->state = ENGINE_STATE_DEAD;
	engine_shutdown_handler(engine);
}


static void shutdown_handler(struct engine *engine,
			     struct engine_module_state *state)
{
	info("call: shutdown handler\n");
	
	if (engine->call->shuth)
		engine->call->shuth(engine->call->shuth_arg);
	else
		engine->call->state->state = ENGINE_STATE_DEAD;
}

/*** engine_call_module
 */

struct engine_module engine_call_module = {
	.name = "call",
	.inith = init_handler,
	.alloch = alloc_handler,
	.closeh = close_handler,
	.shuth = shutdown_handler,	
};
