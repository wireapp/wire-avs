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
 * Event handling.
 */

#include <re.h>
#include "avs_nevent.h"
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_store.h"
#include "avs_string.h"
#include "avs_trace.h"
#include "avs_engine.h"
#include "engine.h"
#include "module.h"
#include "sync.h"
#include "event.h"


static struct {
	struct list lsnrl;
} glob = {
	.lsnrl = LIST_INIT
};


struct engine_event_data {
	struct websock *websock;
	struct nevent *nevent;
	char *token;
	struct nevent_lsnr event_lsnr;
};


/*** engine_event_update_token
 */

int engine_event_update_token(struct engine *engine, const char *token)
{
	char *cpy;
	int err;

	if (!engine || !engine->event || !token)
		return EINVAL;

	err = str_dup(&cpy, token);
	if (err)
		return err;

	mem_deref(engine->event->token);
	engine->event->token = cpy;

	if (engine->event->nevent) {
		err = nevent_set_access_token(engine->event->nevent,
					      engine->event->token);
	}

	return err;
}


/*** engine_event_register
 */

void engine_event_register(struct engine_event_lsnr *lsnr)
{
	list_append(&glob.lsnrl, &lsnr->le, lsnr);
}


/*** dispatch an event
 */

static void dispatch_event(struct engine *engine, const char *type,
			   struct json_object *jobj, bool catchup)
{
	struct le *le;

	LIST_FOREACH(&glob.lsnrl, le) {
		struct engine_event_lsnr *lsnr = le->data;

		if (lsnr->eventh && (!lsnr->type || streq(type, lsnr->type)))
			lsnr->eventh(engine, type, jobj, catchup);
	}

}


/*** storing last id
 */

static int load_last_id(char **idp, struct engine *engine)
{
	struct sobject *so;
	int err;

	err = store_user_open(&so, engine->store, "state", "event", "rb");
	if (err)
		return err;

	err = sobject_read_lenstr(idp, so);

	mem_deref(so);
	return err;
}


static int save_last_id(struct engine *engine, const char *id)
{
	struct sobject *so;
	int err;

	if (!engine->store)
		return 0;

	err = store_user_open(&so, engine->store, "state", "event", "wb");
	if (err)
		return err;

	err = sobject_write_lenstr(so, id);

	mem_deref(so);
	return err;
}


/*** catchup notifications
 */

static int handle_notification(struct engine *engine,
			       struct json_object *jobj)
{
	const char *id;
	struct json_object *jpld;
	int i, datac;
	int err;

	id = jzon_str(jobj, "id");
	err = jzon_array(&jpld, jobj, "payload");
	if (err)
		return err;

	datac = json_object_array_length(jpld);
	if (datac == 0)
		return 0;

	for (i = 0; i < datac; ++i) {
		struct json_object *jitem;
		const char *type;

		jitem = json_object_array_get_idx(jpld, i);
		if (!jitem)
			continue;

		type = jzon_str(jitem, "type");
		if (!type) {
			info("Got an event without type.\n");
			continue;
		}

		debug("* one '%s' event.\n", type);
		dispatch_event(engine, type, jitem, true);
	}

	if (engine->state == ENGINE_STATE_ACTIVE && id)
		err = save_last_id(engine, id);

	return err;
}


/*** nevent handling
 */

static void websock_shutdown_handler(void *arg)
{
	struct engine_module_state *state = arg;

	state->state = ENGINE_STATE_DEAD;
	engine_shutdown_handler(state->engine);
}


static void send_estab_notifications(struct engine *engine, bool estab)
{
	struct le *le;

	LIST_FOREACH(&engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->estabh)
			lsnr->estabh(estab, lsnr->arg);
	}
}


static void estab_handler(void *arg)
{
	struct engine *engine = arg;

	send_estab_notifications(engine, true);
}


static void recv_handler(struct json_object *jobj, void *arg)
{
	struct engine *engine = arg;
	const char *id;
	int err;

	trace_write(engine_get_trace(engine),
		    "EVENT: %H\n", jzon_print, jobj);

	id = jzon_str(jobj, "id");
	if (!id) {
		info("Notification without 'id' field.\n");
		return;
	}

	err = save_last_id(engine, id);
	if (err)
		info("Saving notification ID failed: %m.\n", err);
}


static void nevent_close_handler(int err, void *arg)
{
	struct engine *engine = arg;

	send_estab_notifications(engine, false);
}


static void event_handler(const char *type, struct json_object *jobj,
			  void *arg)
{
	struct engine *engine = arg;

	dispatch_event(engine, type, jobj, false);
}


static void start_nevent(struct engine *engine,
			 struct engine_module_state *state)
{
	int err;

	err = websock_alloc(&engine->event->websock,
			    websock_shutdown_handler, state);
	if (err)
		goto out;

	err = nevent_alloc(&engine->event->nevent, engine->event->websock,
			   engine->http_ws, engine->notification_uri,
			   engine->event->token,
			   estab_handler, recv_handler, nevent_close_handler,
			   engine);
	if (err)
		goto out;

	engine->event->event_lsnr.type = NULL;
	engine->event->event_lsnr.eventh = event_handler;
	engine->event->event_lsnr.arg = engine;
	nevent_register(engine->event->nevent, &engine->event->event_lsnr);

 out:
	if (err)
		engine_error(engine, err);
}


/*** sync
 */

static void sync_clear_handler(struct engine_sync_step *step)
{
	if (step->engine->store)
		store_user_unlink(step->engine->store, "state", "event");

	engine_sync_next(step);
}


static void get_last_event_handler(int err, const struct http_msg *msg,
				   struct mbuf *mb, struct json_object *jobj,
				   void *arg)
{
	struct engine_sync_step *step = arg;
	const char *id;

	if (err == ECONNABORTED)
		return;

	if (msg && msg->scode == 404) {
		info("engine: no notifications for this user\n");
		goto out;
	}

	err = rest_err(err, msg);
	if (err) {
		error("sync error: failed to get last id (%m) (%u %r).\n",
		      err,
		      msg ? msg->scode : 0,
		      msg ? &msg->reason : 0);
		goto out;
	}
	id = jzon_str(jobj, "id");
	if (id)
		save_last_id(step->engine, id);

 out:
	engine_sync_next(step);
}


static void sync_last_event_handler(struct engine_sync_step *step)
{
	int err;

	err = rest_get(NULL, step->engine->rest, 1, get_last_event_handler,
		       step, "/notifications/last");

	if (err) {
		error("sync error: failed to get last event (%m).\n", err);
		engine_sync_next(step);
	}
}


/*** init_handler
 */

static int init_handler(void)
{
	return 0;
}


/*** alloc_handler
 */

static void engine_event_data_destructor(void *arg)
{
	struct engine_event_data *mod = arg;

	mem_deref(mod->nevent);
	mem_deref(mod->websock);
	mem_deref(mod->token);
}


static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	struct engine_event_data *mod;
	int err = 0;

	mod = mem_zalloc(sizeof(*mod), engine_event_data_destructor);
	if (!mod) {
		mem_deref(state);
		return ENOMEM;
	}

	engine->event = mod;
	list_append(&engine->modulel, &state->le, state);

	err |= engine_sync_register(engine, "deleting last event",
				    0., sync_clear_handler);
	err |= engine_sync_register(engine, "fetching last event",
				    1000000., sync_last_event_handler);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(mod);

	return err;
}


/*** active_handler
 *
 * First playback events queue, then start an event channel. If the
 * event queue thing failed, start a full sync.
 */

static void get_notifications_handler(int err, const struct http_msg *msg,
				      struct mbuf *mb,
				      struct json_object *jobj, void *arg)
{
	struct engine_module_state *state = arg;
	struct engine *engine = state->engine;
	struct json_object *jnot;
	int i, datac;
	struct le *le;

	if (err) {
		info("engine: get notifications failed: %m.\n", err);
		goto out;
	}

	if (msg && msg->scode >= 300) {
		info("engine: get notifications failed: %u %r.\n",
		     msg->scode, &msg->reason);
		err = EPIPE;
		goto out;
	}

	err = jzon_array(&jnot, jobj, "notifications");
	if (err)
		goto out;
	datac = json_object_array_length(jnot);
	if (datac == 0)
		goto out;

	for (i = 0; i< datac; ++i) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jnot, i);
		if (!jitem)
			continue;

		err = handle_notification(engine, jitem);
		if (err)
			break;
	}

	/* We had at least one event, so we need to signal that we are
	 * done.
	 */
	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->module->caughtuph)
			ms->module->caughtuph(engine);
	}

 out:
	if (err)
		engine->need_sync = true;
	start_nevent(engine, state);
}


static void active_handler(struct engine *engine,
			   struct engine_module_state *state)
{
	char *id = NULL;
	int err;

	if (!engine->store) {
		err = ENOENT;
		goto out;
	}

	err = load_last_id(&id, engine);
	if (err) {
		info("engine: load_last_id failed: %m.\n", err);
		goto out;
	}

	info("engine: getting notifications for id='%s'\n", id);

	err = rest_get(NULL, engine->rest, 0, get_notifications_handler,
		       state, "/notifications?since=%s", id);
	if (err) {
		info("event/active_handler rest failed: %m.\n", err);
		goto out;
	}

 out:
	mem_deref(id);
	if (err) {
		engine->need_sync = true;
		start_nevent(engine, state);
	}
}


/*** shutdown_handler
 */

static void shutdown_handler(struct engine *engine,
			     struct engine_module_state *state)
{
	if (engine && engine->event && engine->event->websock) {
		engine->event->nevent = mem_deref(engine->event->nevent);

		/* Shutdown all websockets now and wait for a clean
		 * protocol shutdown from the server. We only delete the
		 * websock object then.
		 */
		websock_shutdown(engine->event->websock);
	}
	else {
		state->state = ENGINE_STATE_DEAD;
		engine_shutdown_handler(engine);
	}
}


/*** close handler
 */

static void close_handler(void)
{
	list_clear(&glob.lsnrl);
}


/*** engine_event_module
 */

struct engine_module engine_event_module = {
	.name = "event",
	.inith = init_handler,
	.alloch = alloc_handler,
	.activeh = active_handler,
	.shuth = shutdown_handler,
	.closeh = close_handler
};


int engine_event_restart(struct engine *engine)
{
	int err;

	if (!engine)
		return EINVAL;

	if (engine->event) {
		err = nevent_restart(engine->event->nevent);
		if (err)
			return err;
	}

	return 0;
}
