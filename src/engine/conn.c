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
 * Connection handling
 */

#include <re.h>
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_string.h"
#include "avs_engine.h"
#include "engine.h"
#include "event.h"
#include "module.h"
#include "user.h"
#include "utils.h"
#include "sync.h"
#include "conv.h"
#include "conn.h"


/*** enum engine_conn_status
 */

static int import_conn_status(enum engine_conn_status *dst,
			      struct json_object *cobj, const char *key)
{
	const char *status;

	status = jzon_str(cobj, key);
	if (!status)
		return ENOENT;

	if (streq(status, "accepted"))
		*dst = ENGINE_CONN_ACCEPTED;
	else if (streq(status, "blocked"))
		*dst = ENGINE_CONN_BLOCKED;
	else if (streq(status, "pending"))
		*dst = ENGINE_CONN_PENDING;
	else if (streq(status, "ignored"))
		*dst = ENGINE_CONN_IGNORED;
	else if (streq(status, "sent"))
		*dst = ENGINE_CONN_SENT;
	else if (streq(status, "cancelled"))
		*dst = ENGINE_CONN_CANCELLED;
	else {
		info("engine: unknown connection status `%s'\n", status);
		return EPROTO;
	}

	return 0;
}


const char *engine_conn_status_string(enum engine_conn_status status)
{
	switch (status) {

	case ENGINE_CONN_ACCEPTED:
		return "accepted";
	case ENGINE_CONN_BLOCKED:
		return "blocked";
	case ENGINE_CONN_PENDING:
		return "pending";
	case ENGINE_CONN_IGNORED:
		return "ignored";
	case ENGINE_CONN_SENT:
		return "sent";
	default:
		return NULL;
	}
}


/*** update connection from JSON object
 */

static int import_conn(struct engine_user *user, struct json_object *jconn)
{
	const char *conv_id;
	enum engine_conn_status old_status;
	struct le *le;
	int err;

	old_status = user->conn_status;
	err = import_conn_status(&user->conn_status, jconn, "status");
	if (err) {
		warning("import connection status failed: %m\n", err);
		return err;
	}

	jzon_strrepl(&user->conn_message, jconn, "message");

	if (!user->conv) {
		conv_id = jzon_str(jconn, "conversation");
		if (conv_id) {
			engine_fetch_conv(user->engine, conv_id);
		}
	}

	engine_save_user(user);

	if (user->conn_status != old_status) {
		LIST_FOREACH(&user->engine->lsnrl, le) {
			struct engine_lsnr *lsnr = le->data;

			if (lsnr->connh)
				lsnr->connh(user, old_status, lsnr->arg);
		}

		if (user->conn_status == ENGINE_CONN_ACCEPTED
		    && user->conv && user->conv->type != ENGINE_CONV_ONE)
		{
			user->conv->type = ENGINE_CONV_ONE;
			engine_send_conv_update(user->conv, ENGINE_CONV_TYPE);
		}
	}

	return 0;
}


/*** engine_update_conn
 */

struct engine_update_conn_data {
	struct engine_user *user;
	engine_status_h *statush;
	void *arg;
};


static void update_conn_handler(int err, const struct http_msg *msg,
			        struct mbuf *mb, struct json_object *jobj,
			        void *arg)
{
	struct engine_update_conn_data *data = arg;
	const char *id;

	err = engine_rest_err(err, msg);
	if (err)
		goto out;

	/* Make sure we get the right user back.
	 */
	id = jzon_str(jobj, "to");
	if (!id || !streq(id, data->user->id)) {
		err = EPROTO;
		goto out;
	}

	err = import_conn(data->user, jobj);

 out:
	if (data->statush)
		data->statush(err, data->arg);
	mem_deref(data);
}


int engine_update_conn(struct engine_user *user,
		       enum engine_conn_status status,
		       engine_status_h *statush, void *arg)
{
	struct engine_update_conn_data *data;
	struct rest_req *rr;
	int err;

	if (!user)
		return EINVAL;
	if (status != ENGINE_CONN_ACCEPTED && status != ENGINE_CONN_BLOCKED)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->user = user;
	data->statush = statush;
	data->arg = arg;

	err = rest_req_alloc(&rr, update_conn_handler, data,
			     "PUT", "/connections/%s", user->id);
	if (err)
		goto out;

	err = rest_req_add_json(rr, "s", "status",
				engine_conn_status_string(status));
	if (err)
		goto out;

	err = rest_req_start(NULL, rr, user->engine->rest, 0);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(data);
	return err;
}


/*** user.connection Event Handling
 */

static void conn_update_handler(struct engine *engine, const char *type,
				struct json_object *jobj, bool catchup)
{
	struct json_object *jconn;
	const char *user_id;
	struct engine_user *user;
	int err;

	(void) type;
	(void) catchup;

	err = jzon_object(&jconn, jobj, "connection");
	if (err) {
		info("conn_update_handler: missing connection.\n");
		return;
	}

	user_id = jzon_str(jconn, "to");
	if (user_id == NULL) {
		info("conn_update_handler: connection without to.\n");
		return;
	}

	err = engine_lookup_user(&user, engine, user_id, true);
	if (err) {
		debug("engine_lookup_user '%s' failed: %m\n", user_id, err);
		return;
	}

	err = import_conn(user, jconn);
	if (err) {
		warning("import connection status failed: %m\n", err);
		return;
	}
}

struct engine_event_lsnr conn_update_lsnr = {
	.type = "user.connection",
	.eventh = conn_update_handler
};


/*** sync
 *
 * Read all connections and add them the users behind them.
 */

static void process_conn(struct engine *engine, struct json_object *jobj)
{
	struct engine_user *user;
	const char *id;
	const char *message;
	char *dst;
	int err;

	id = jzon_str(jobj, "to");
	if (!id) {
		warning("process_conn: missing 'to' entry in json\n");
		return;
	}

	err = engine_lookup_user(&user, engine, id, true);
	if (err)
		return;

	err = import_conn_status(&user->conn_status, jobj, "status");
	if (err) {
		warning("engine: import_conn_status failed (%m)\n", err);
		return;
	}

	message = jzon_str(jobj, "message");
	if (!message) {
		err = str_dup(&dst, message);
		if (err)
			return;
		mem_deref(user->conn_message);
		user->conn_message = dst;
	}
}


static void get_conn_handler(int err, const struct http_msg *msg,
			     struct mbuf *mb, struct json_object *jobj,
			     void *arg)
{
	struct engine_sync_step *step = arg;
	struct json_object *jarr;
	int i, datac;

	err = rest_err(err, msg);
	if (err) {
		error("Getting connections failed: %m.\n", err);
		goto out;
	}

	err = jzon_array(&jarr, jobj, "connections");
	if (err) {
		/* fallback to old api */
		jarr = jobj;
	}

	datac = json_object_array_length(jarr);
	if (datac == 0)
		goto out;

	for (i = 0; i < datac; ++i) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jarr, i);
		if (!jitem)
			continue;

		process_conn(step->engine, jitem);
	}

 out:
	engine_sync_next(step);
}


static void sync_handler(struct engine_sync_step *step)
{
	int err;

	err = rest_get(NULL, step->engine->rest, 0, get_conn_handler, step,
		       "/connections");
	if (err) {
		error("Getting connections failed: %m.\n", err);
		engine_sync_next(step);
	}
}


/*** init handler
 */

static int init_handler(void)
{
	engine_event_register(&conn_update_lsnr);
	return 0;
}


/*** alloc handler
 */

static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	int err;

	err = engine_sync_register(engine, "fetching connections",
				   10., sync_handler);
	if (err)
		return err;

	list_append(&engine->modulel, &state->le, state);
	return 0;
}


/*** close handler
 */

static void close_handler(void)
{
}


/*** engine_conn_module
 */

struct engine_module engine_conn_module = {
	.name = "conn",
	.inith = init_handler,
	.alloch = alloc_handler,
	.closeh = close_handler
};
