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

#include <assert.h>
#include <re.h>
#include "avs_log.h"
#include "avs_store.h"
#include "avs_engine.h"
#include "engine.h"
#include "sync.h"


/*** engine_sync_register
 */

static void step_destructor(void *data)
{
	struct engine_sync_step *step = data;

	list_unlink(&step->le);
}


int engine_sync_register(struct engine *engine, const char *name,
			  float prio, engine_sync_h *synch)
{
	struct engine_sync_step *step;
	struct le *le;

	if (!engine || !name || !synch)
		return EINVAL;

	step = mem_zalloc(sizeof(*step), step_destructor);
	if (!step)
		return ENOMEM;

	step->engine = engine;
	str_ncpy(step->name, name, sizeof(step->name));
	step->prio = prio;
	step->synch = synch;

	LIST_FOREACH(&engine->syncl, le) {
		struct engine_sync_step *lstep = le->data;

		if (lstep->prio > step->prio)
			break;
	}

	if (le)
		list_insert_before(&engine->syncl, le, &step->le, step);
	else
		list_append(&engine->syncl, &step->le, step);

	return 0;
}


/*** engine_sync_unregister
 */

void engine_sync_unregister(struct engine_sync_step *step)
{
	if (!step)
		return;

	list_unlink(&step->le);
}


/*** engine_start_sync
 */

static void save_need_sync(struct engine *engine, bool need_sync)
{
	struct sobject *so;
	int err;

	if (!engine->store) {
		debug("save_need_sync: no store.\n");
		return;
	}

	err = store_user_open(&so, engine->store, "state", "need_sync", "wb");
	if (err) {
		debug("save_need_sync: store_user_open() failed (%m).\n",
			err);
		return;
	}
	err = sobject_write_u8(so, need_sync);
	if (err)
		debug("save_need_sync: write failed (%m).\n", err);
	sobject_close(so);
	mem_deref(so);
}


int engine_start_sync(struct engine *engine)
{
	struct le *le;
	struct engine_sync_step *step;

	if (!engine)
		return EINVAL;

	if (engine->state != ENGINE_STATE_ACTIVE) {
		engine->need_sync = true;
		return 0;
	}

	engine->ts_start = tmr_jiffies();

	info("sync: starting sync.\n");

	engine->need_sync = false;

	le = list_head(&engine->syncl);
	if (!le) {
		warning("Nothing to sync.\n");
		return 0;
	}

	save_need_sync(engine, true);

	step = le->data;

	debug("Sync: running handler '%s'.\n", step->name);

	engine->state = ENGINE_STATE_SYNC;
	step->synch(step);

	return 0;
}


static void send_syncdone_notifications(struct engine *engine)
{
	struct le *le;

	if (!engine)
		return;

	LIST_FOREACH(&engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->syncdoneh)
			lsnr->syncdoneh(lsnr->arg);
	}
}


/*** engine_sync_next
 */

void engine_sync_next(struct engine_sync_step *step)
{
	struct engine *engine;

	if (!step || !step->engine)
		return;

	assert(step->engine != NULL);
	engine = step->engine;

	if (engine->destroyed)
		return;

	if (step->le.next) {
		struct engine_sync_step *next = step->le.next->data;

		debug("Sync: running handler '%s'.\n", next->name);

		assert(next->engine != NULL);

		next->synch(next);
	}
	else {
		info("Sync: done. (%u ms)\n",
			  tmr_jiffies() - engine->ts_start);

		save_need_sync(step->engine, false);
		step->engine->state = ENGINE_STATE_ACTIVE;

		send_syncdone_notifications(engine);
	}
}
