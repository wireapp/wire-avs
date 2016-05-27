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
 * User management
 */

#include <re.h>
#include "avs_dict.h"
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_store.h"
#include "avs_string.h"
#include "avs_engine.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_flowmgr.h"
#include "module.h"
#include "engine.h"
#include "sync.h"
#include "event.h"
#include "user.h"
#include "call.h"

#define ENGINE_DEFAULT_DISPLAY_NAME ""


struct engine_user_data {
	struct dict *userd;
	struct engine_user *self;
};


/*** struct engine_user
 */

static void engine_user_destructor(void *arg)
{
	struct engine_user *user = arg;

	mem_deref(user->id);
	mem_deref(user->email);
	mem_deref(user->phone);
	mem_deref(user->name);
	mem_deref(user->display_name);
	mem_deref(user->conn_message);
	mem_deref(user->arg);
}


static int user_alloc(struct engine_user **userp, struct engine *engine,
		      const char *id)
{
	struct engine_user *user;
	int err;

	user = mem_zalloc(sizeof(*user), engine_user_destructor);
	if (!user)
		return ENOMEM;

	err = str_dup(&user->id, id);
	if (err)
		goto out;

	err = str_dup(&user->display_name, ENGINE_DEFAULT_DISPLAY_NAME);
	if (err)
		goto out;

	user->engine = engine;
	user->collected = false;

	err = dict_add(engine->user->userd, id, user);
	if (err)
		goto out;

	/* Ownership transferred to dictionary.  */
	mem_deref(user);

	*userp = user;

 out:
	if (err)
		mem_deref(user);
	return err;
}


/*** engine_save_user
 */

int engine_save_user(struct engine_user *user)
{
	struct sobject *so;
	int err;

	if (!user)
		return EINVAL;

	if (!user->engine->store)
		return 0;

	err = store_user_open(&so, user->engine->store, "users",
			      user->id, "wb");
	if (err)
		return err;

	err = sobject_write_u8(so, user->collected);
	if (err)
		goto out;

	if (user->collected) {
		err = sobject_write_lenstr(so, user->email);
		if (err)
			goto out;

		err = sobject_write_lenstr(so, user->phone);
		if (err)
			goto out;

		err = sobject_write_u32(so, user->accent_id);
		if (err)
			goto out;

		err = sobject_write_lenstr(so, user->name);
		if (err)
			goto out;

		err = sobject_write_lenstr(so, user->display_name);
		if (err)
			goto out;

		err = sobject_write_u8(so, user->conn_status);
		if (err)
			goto out;

		err = sobject_write_lenstr(so, user->conn_message);
		if (err)
			goto out;
	}

 out:
	if (err)
		warning("Writing user '%s' failed: %m.\n", user->id, err);
	mem_deref(so);
	return err;
}


/*** load user
 */

static int load_user(struct engine_user *user)
{
	struct sobject *so;
	char *dst;
	uint8_t v8;
	int err;

	err = store_user_open(&so, user->engine->store, "users", user->id,
			      "rb");
	if (err)
		return err;

	err = sobject_read_u8(&v8, so);
	if (err)
		goto out;
	user->collected = v8;

	if (user->collected) {
		err = sobject_read_lenstr(&dst, so);
		if (err)
			goto out;
		mem_deref(user->email);
		user->email = dst;

		err = sobject_read_lenstr(&dst, so);
		if (err)
			goto out;
		mem_deref(user->phone);
		user->phone = dst;

		err = sobject_read_u32(&user->accent_id, so);
		if (err)
			goto out;

		err = sobject_read_lenstr(&dst, so);
		if (err)
			goto out;
		mem_deref(user->name);
		user->name = dst;

		err = sobject_read_lenstr(&dst, so);
		if (err)
			goto out;
		mem_deref(user->display_name);
		user->display_name = dst;

		err = sobject_read_u8(&v8, so);
		if (err)
			goto out;
		user->conn_status = v8;

		err = sobject_read_lenstr(&dst, so);
		if (err)
			goto out;
		mem_deref(user->conn_message);
		user->conn_message = dst;
	}

 out:
	mem_deref(so);
	return err;
}


/*** update user
 */

static int update_str(struct json_object *juser, const char *field,
		      char **target, enum engine_user_changes *changes,
		      enum engine_user_changes flag)
{
	const char *str;
	char *dst;
	int err;

	str = jzon_str(juser, field);
	if (str && (!*target || !streq(str, *target))) {
		err = str_dup(&dst, str);
		if (err)
			return err;
		mem_deref(*target);
		*target = dst;
		*changes |= flag;
	}
	return 0;
}


static int update_display_name(struct engine_user *user)
{
	char *dst;
#if 0
	struct pl word;
#endif
	int err;

	/* XXX Implement the proper rules
	 *     Until we do, we keep the full name.
	 */

#if 0
	err = re_regex(user->name, strlen(user->name),
		       "[^ \t\r\n]+", &word);
	if (err)
		err = str_dup(&dst, user->name);
	else
		err = pl_strdup(&dst, &word);
	if (err)
		return err;
	mem_deref(user->display_name);
	user->display_name = dst;
#else
	if (user->name) {
		err = str_dup(&dst, user->name);
		if (err)
			return err;
		mem_deref(user->display_name);
		user->display_name = dst;
	}
#endif
	return 0;
}


static int update_user(struct engine_user *user, struct json_object *juser)
{
	enum engine_user_changes changes = 0;
	struct le *le;
	int err;

	err = update_str(juser, "email", &user->email, &changes,
			 ENGINE_USER_EMAIL);
	if (err)
		goto out;

	err = update_str(juser, "phone", &user->phone, &changes,
			 ENGINE_USER_PHONE);
	if (err)
		goto out;

	err = jzon_u32(&user->accent_id, juser, "accent_id");
	if (err == 0)
		changes |= ENGINE_USER_ACCENT_ID;
	else if (err != ENOENT)
		goto out;

	err = update_str(juser, "name", &user->name, &changes,
			 ENGINE_USER_NAME);
	if (err)
		goto out;

	if (changes & ENGINE_USER_NAME) {
		err = update_display_name(user);
		if (err)
			goto out;
	}

	user->collected = true;

	if (changes > 0) {
		engine_save_user(user);

		LIST_FOREACH(&user->engine->lsnrl, le) {
			struct engine_lsnr *lsnr = le->data;

			if (lsnr->userh)
				lsnr->userh(user, changes, lsnr->arg);
		}
	}

 out:
	return err;
}


/*** collect user information
 */

static void collect_user_handler(int err, const struct http_msg *msg,
			         struct mbuf *mb, struct json_object *jobj,
			         void *arg)
{
	struct engine_user *user = arg;

	if (err) {
		info("collecting user failed: %m\n", err);
		return;
	}
	if (msg && msg->scode >= 300) {
		info("collecting user '%s' failed: %u %r.\n",
		     user->id, msg->scode, &msg->reason);
		return;
	}

	err = update_user(user, jobj);
	if (err)
		info("updating user '%s' failed: %m.\n", user->id, err);
}


static int collect_user(struct engine_user *user)
{
	if (!user || !user->id)
		return EINVAL;

	return rest_get(NULL, user->engine->rest, 1, collect_user_handler,
		        user, "/users/%s", user->id);
}

/*** engine_lookup_user
 */

int engine_lookup_user(struct engine_user **userp, struct engine *engine,
		       const char *id, bool collect)
{
	struct engine_user *user;
	int err;

	if (!userp || !engine || !id)
		return EINVAL;

	user = dict_lookup(engine->user->userd, id);
	if (user) {
		*userp = user;
		return 0;
	}

	err = user_alloc(userp, engine, id);
	if (err)
		return err;

	if (collect)
		return collect_user(*userp);

	return 0;
}


/*** engine_get_self
 */

struct engine_user *engine_get_self(struct engine *engine)
{
	return engine->user->self;
}


/*** engine_is_self
 */

bool engine_is_self(struct engine *engine, const char *id)
{
	if (!engine || !engine->user || !engine->user->self)
		return false;

	return streq(engine->user->self->id, id);
}


/*** engine_apply_users
 */

struct apply_users_data {
	engine_user_apply_h *applyh;
	void *arg;
};


static bool apply_users_handler(char *key, void *val, void *arg)
{
	struct apply_users_data *data = arg;
	struct engine_user *user = val;

	return data->applyh(user, data->arg);
}


struct engine_user *engine_apply_users(struct engine *engine,
				       engine_user_apply_h *applyh,
				       void *arg)
{
	struct apply_users_data data;

	if (!engine || !engine->user || !applyh)
		return NULL;

	data.applyh = applyh;
	data.arg = arg;

	return dict_apply(engine->user->userd, apply_users_handler, &data);
}


/*** user.update Event Handling
 */

static void user_update_handler(struct engine *engine, const char *type,
				struct json_object *jobj, bool catchup)
{
	struct json_object *juser;
	const char *user_id;
	struct engine_user *user;
	int err;

	(void) type;
	(void) catchup;

	if (!json_object_object_get_ex(jobj, "user", &juser)) {
		debug("user_update_handler: event without user.\n");
		return;
	}

	user_id = jzon_str(juser, "id");
	if (user_id == NULL) {
		debug("user_update_handler: user without id.\n");
		return;
	}

	err = engine_lookup_user(&user, engine, user_id, false);
	if (err) {
		debug("engine_lookup_user %s failed: %m\n", user_id, err);
		return;
	}

	err = update_user(user, jobj);
	if (err) {
		debug("user.update: update user failed (%m)\n", err);
		return;
	}
}

struct engine_event_lsnr user_update_lsnr = {
	.type = "user.update",
	.eventh = user_update_handler
};


/*** sync
 *
 *   Get self user and get information for all other users we know of.
 */

static void save_state(struct engine *engine)
{
	struct sobject *so;
	int err;

	if (!engine->store || !engine->user->self)
		return;

	err = store_user_open(&so, engine->store, "state", "user", "wb");
	if (err)
		return;

	err = sobject_write_lenstr(so, engine->user->self->id);
	if (err)
		goto out;

 out:
	mem_deref(so);
}


static void get_self_handler(int err, const struct http_msg *msg,
			     struct mbuf *mb, struct json_object *jobj,
			     void *arg)
{
	struct engine_sync_step *step = arg;
	const char *id;
	struct engine_user *self;

	if (err) {
		error("sync error: couldn't get self (%m).\n", err);
		goto out;
	}
	if (msg && msg->scode >= 300) {
		error("sync error: couldn't get self (%u %r).\n",
		      msg->scode, &msg->reason);
		goto out;
	}

	id = jzon_str(jobj, "id");
	if (!id) {
		error("sync error: couldn't get self (no 'id' field).\n");
		goto out;
	}
	err = engine_lookup_user(&self, step->engine, id, false);
	if (err) {
		error("sync error: couldn't create self (%m).\n", err);
		goto out;
	}
	step->engine->user->self = self;
	err = update_user(self, jobj);
	if (err) {
		error("sync error: couldn't update self (%m).\n", err);
		goto out;
	}
	save_state(step->engine);

 out:
	engine_sync_next(step);
}


static bool collect_apply_handler(char *key, void *val, void *arg)
{
	struct engine_user *user = val;

	(void) arg;

	collect_user(user);
	return false;
}


static void sync_self_handler(struct engine_sync_step *step)
{
	int err;

	err = rest_get(NULL, step->engine->rest, 1, get_self_handler, step,
		       "/self");

	if (err) {
		error("sync error: couldn't get self (%m).\n", err);
		engine_sync_next(step);
	}
}


static void sync_others_handler(struct engine_sync_step *step)
{
	if (!step || !step->engine || !step->engine->user)
		return;

	dict_apply(step->engine->user->userd, collect_apply_handler, NULL);
	engine_sync_next(step);
}


/*** init handler
 */

static int init_handler(void)
{
	engine_event_register(&user_update_lsnr);
	return 0;
}


/*** alloc handler
 */

static void engine_user_data_destructor(void *arg)
{
	struct engine_user_data *mod = arg;

	mem_deref(mod->userd);
}


static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	struct engine_user_data *mod;
	int err;

	mod = mem_zalloc(sizeof(*mod), engine_user_data_destructor);
	if (!mod) {
		err = ENOMEM;
		goto out;
	}

	err = dict_alloc(&mod->userd);
	if (err)
		goto out;

	engine->user = mod;

	err |= engine_sync_register(engine, "fetching self",
				    1., sync_self_handler);
	err |= engine_sync_register(engine, "fetching users",
				    20., sync_others_handler);
	if (err)
		goto out;

 out:
	if (err) {
		mem_deref(mod);
		mem_deref(state);
	}
	else
		list_append(&engine->modulel, &state->le, state);
	return err;
}


/*** startup handler
 */

static int user_dir_handler(const char *id, void *arg)
{
	struct engine *engine = arg;
	struct engine_user *user;
	int err;

	err = engine_lookup_user(&user, engine, id, false);
	if (err) {
		info("Loading user '%s' failed in creation: %m.\n", id, err);
		return 0;
	}
	err = load_user(user);
	if (err)
		info("Loading user '%s' failed: %m.\n", id, err);
	return 0;
}


static int load_state(struct engine *engine)
{
	struct sobject *so;
	char *id = NULL;
	int err;

	err = store_user_open(&so, engine->store, "state", "user", "rb");
	if (err)
		return err;

	err = sobject_read_lenstr(&id, so);
	if (err)
		goto out;

	err = engine_lookup_user(&engine->user->self, engine, id, false);
	if (err)
		goto out;

 out:
	mem_deref(so);
	mem_deref(id);
	return err;
}


static void startup_handler(struct engine *engine,
			    struct engine_module_state *state)
{
	int err;

	if (!engine->store) {
		err = ENOENT;
		goto out;
	}

	err = store_user_dir(engine->store, "users", user_dir_handler,
			     engine);
	if (err)
		goto out;

	err = load_state(engine);
	if (err)
		goto out;

 out:
	if (err)
		engine->need_sync = true;
	state->state = ENGINE_STATE_ACTIVE;
	engine_active_handler(engine);
}


/*** close handler
 */

static void close_handler(void)
{
}


/*** engine_user_module
 */

struct engine_module engine_user_module = {
	.name = "user",
	.inith = init_handler,
	.alloch = alloc_handler,
	.startuph = startup_handler,
	.closeh = close_handler
};
