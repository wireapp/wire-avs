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
 * Engine house keeping.
 */

#include <re.h>
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_jzon.h"
#include "avs_store.h"
#include "avs_nevent.h"
#include "avs_engine.h"
#include "avs_trace.h"
#include "engine.h"
#include "module.h"
#include "event.h"
#include "sync.h"


enum {
	ENGINE_CONF_REST_MAXOPEN = 4,
};

struct {
	char msys[64];
} engine_global;


/*** engine_init
 */

int engine_init(const char *msys)
{
	struct le *le;
	int err;

	str_ncpy(engine_global.msys, msys, sizeof(engine_global.msys));

	engine_init_modules();

	LIST_FOREACH(engine_get_modules(), le) {
		struct engine_module *mod = le->data;

		if (mod->inith) {
			err = mod->inith();
			if (err) {
				warning("engine module '%s': %m.\n",
					mod->name, err);
				return err;
			}
		}
	}

	return 0;
}


int engine_set_trace(struct engine *engine, const char *path, bool use_stdout)
{
	if (!engine)
		return EINVAL;

	return trace_alloc(&engine->trace, path, use_stdout);
}


/*** engine_close
 */

void engine_close(void)
{
	struct le *le;

	LIST_FOREACH(engine_get_modules(), le) {
		struct engine_module *mod = le->data;

		if (mod->closeh)
			mod->closeh();

	}

	engine_close_modules();
}


/*** engine_alloc
 */

static void engine_destructor(void *arg)
{
	struct engine *engine = arg;

	debug("engine: engine %p destroyed\n", engine);

	engine->destroyed = true;

	engine->call  = mem_deref(engine->call);
	engine->conv  = mem_deref(engine->conv);
	engine->user  = mem_deref(engine->user);
	engine->event = mem_deref(engine->event);

	mem_deref(engine->request_uri);
	mem_deref(engine->notification_uri);
	mem_deref(engine->email);
	mem_deref(engine->user_agent);
	mem_deref(engine->password);
	mem_deref(engine->store);
	mem_deref(engine->dnsc);
	mem_deref(engine->http_ws);
	mem_deref(engine->http);
	mem_deref(engine->rest);
	mem_deref(engine->login);
	mem_deref(engine->trace);
	list_flush(&engine->modulel);
	list_flush(&engine->syncl);
}


struct http_cli *engine_get_httpc(struct engine *engine)
{
	return engine ? engine->http : NULL;
}


static int dns_init(struct dnsc **dnscp)
{
	struct sa nsv[16];
	uint32_t nsn, i;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		error("dns srv get: %m\n", err);
		goto out;
	}

	err = dnsc_alloc(dnscp, NULL, nsv, nsn);
	if (err) {
		error("dnsc alloc: %m\n", err);
		goto out;
	}

	info("engine: DNS Servers: (%u)\n", nsn);
	for (i=0; i<nsn; i++) {
		info("    %J\n", &nsv[i]);
	}

 out:
	return err;
}


static int dns_restart(struct dnsc *dnsc)
{
	struct sa nsv[16];
	uint32_t nsn;
	uint32_t i;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		error("dns srv get: %m\n", err);
		goto out;
	}

	info("dns_restart: %u DNS servers\n", nsn);
	for (i = 0; i < nsn; ++i) {
		info("dns_restart: DNS[%u]=%j\n", i, &nsv[i]);
	}

	err = dnsc_srv_set(dnsc, nsv, nsn);
	if (err) {
		error("dnsc alloc: %m\n", err);
		goto out;
	}

 out:
	return err;
}


static int calc_user_hash(const char *request_uri, const char *email,
			  char *user_hash)
{
	uint8_t hash[MD5_SIZE];
	int err;

	err = md5_printf(hash, "%s\r\n%s", request_uri, email);
	if (err)
		return err;

	err = re_snprintf(user_hash, MD5_STR_SIZE, "%w", hash, MD5_SIZE);
	return err == -1 ? EPROTO : 0;
}


static int prepare_store(struct store *store, const char *request_uri,
			 const char *email, bool flush_store)
{
	char user_hash[MD5_STR_SIZE];
	int err;

	err = calc_user_hash(request_uri, email, user_hash);
	if (err)
		return err;

	err = store_set_user(store, user_hash);
	if (err)
		return err;

	if (flush_store)
		store_flush_user(store);

	return 0;
}


static void engine_module_state_destructor(void *arg)
{
	struct engine_module_state *ms = arg;

	list_unlink(&ms->le);
}


static int add_module(struct engine *engine, struct engine_module *module)
{
	struct engine_module_state *ms;

	if (!module->alloch)
		return 0;

	ms = mem_zalloc(sizeof(*ms), engine_module_state_destructor);
	if (!ms)
		return ENOMEM;

	ms->engine = engine;
	ms->module = module;
	ms->state = ENGINE_STATE_LOGIN;

	return module->alloch(engine, ms);
}


static void trigger_startup(struct engine *engine)
{
	struct le *le;

	engine->state = ENGINE_STATE_STARTUP;

	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->module->startuph) {
			ms->state = ENGINE_STATE_STARTUP;
			ms->module->startuph(engine, ms);
		}
		else
			ms->state = ENGINE_STATE_ACTIVE;
	}

	engine_active_handler(engine);
}

static int clear_login(struct engine *engine);

static void login_handler(int err, const struct login_token *token, void *arg)
{
	struct engine *engine = arg;

	if (err) {
		/* Failed (re-) login is fatal.
		 */
		if (engine->errorh)
			engine->errorh(err, engine->arg);
		engine_shutdown(engine);
	}
	else {
		rest_client_set_token(engine->rest, token);
		err = engine_event_update_token(engine, token->access_token);
		if (err) {
			debug("engine: event_update error (%m)\n", err);
			engine_error(engine, err);
			return;
		}

		if (engine->state == ENGINE_STATE_LOGIN) {
			trigger_startup(engine);

			if (engine->clear_cookies) {
				clear_login(engine);
			}
		}
	}
}


static int start_login(struct engine *engine)
{
	int err;

	err = login_request(&engine->login, engine->rest,
			    engine->request_uri, engine->email,
			    engine->password, login_handler, engine);
	if (err) {
		warning("Login request failed: %m.\n", err);
	}

#if 0
	/* keep the password around, needed for registration of client */
	engine->password = mem_deref(engine->password);
#endif

	return err;
}


static void clear_login_handler(int err, const struct http_msg *msg,
				struct mbuf *mb, struct json_object *jobj,
				void *arg)
{
	struct engine *engine = arg;

	err = rest_err(err, msg);
	if (err)
		warning("Failed to clear cookies: %m (%u %r).\n",
			err,
			msg ? msg->scode : 0,
			msg ? &msg->reason : NULL);

	if (err) {
		if (engine->errorh)
			engine->errorh(err, engine->arg);
		engine_shutdown(engine);
	}
}


static int clear_login(struct engine *engine)
{
	struct json_object *jobj, *inner;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	inner = json_object_new_string(engine->email);
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "email", inner);

	inner = json_object_new_string(engine->password);
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "password", inner);

	inner = json_object_new_array();
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "labels", inner);

	err = rest_request(NULL, engine->rest, 0, "POST",
			   clear_login_handler, engine, "/cookies/remove",
			   "%H", jzon_print, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);
	return err;
}


int engine_alloc(struct engine **enginep, const char *request_uri,
		 const char *notification_uri, const char *email,
		 const char *password, struct store *store, bool flush_store,
		 bool clear_cookies, const char *user_agent,
		 engine_ping_h *readyh, engine_status_h *errorh,
		 engine_ping_h *shuth, void *arg)
{
	struct engine *engine;
	struct le *le;
	int err;

	if (!enginep || !request_uri || !notification_uri || !email
	    || !password)
	{
		return EINVAL;
	}

	engine = mem_zalloc(sizeof(*engine), engine_destructor);
	if (!engine)
		return ENOMEM;

	engine->state = ENGINE_STATE_LOGIN;
	engine->need_sync = flush_store || !store;

	err = str_dup(&engine->request_uri, request_uri);
	if (err)
		goto out;

	err = str_dup(&engine->notification_uri, notification_uri);
	if (err)
		goto out;

	err = str_dup(&engine->email, email);
	if (err)
		goto out;

	err = str_dup(&engine->password, password);
	if (err)
		goto out;

	if (user_agent) {
		err = str_dup(&engine->user_agent, user_agent);
		if (err)
			goto out;
	}

	if (store) {
		engine->store = mem_ref(store);
		err = prepare_store(store, request_uri, email, flush_store);
		if (err) {
			warning("Preparing store failed: %m.\n", err);
			goto out;
		}
	}

	err = dns_init(&engine->dnsc);
	if (err) {
		warning("DNS init failed: %m.\n", err);
		goto out;
	}

	err = http_client_alloc(&engine->http, engine->dnsc);
	if (err) {
		warning("HTTP client init failed: %m.\n", err);
		goto out;
	}

	/* Websocket needs its own HTTP client */
	err = http_client_alloc(&engine->http_ws, engine->dnsc);
	if (err) {
		warning("HTTP client init failed: %m.\n", err);
		goto out;
	}

	err = rest_client_alloc(&engine->rest, engine->http,
				engine->request_uri, engine->store,
				ENGINE_CONF_REST_MAXOPEN, user_agent);
	if (err) {
		warning("REST client init failed: %m.\n", err);
		goto out;
	}

	LIST_FOREACH(engine_get_modules(), le) {
		struct engine_module *mod = le->data;

		err = add_module(engine, mod);
		if (err) {
			warning("Starting engine module '%s' failed: %m.\n",
			      mod->name, err);
			goto out;
		}
	}

	engine->clear_cookies = clear_cookies;
	err = start_login(engine);
	if (err)
		goto out;

	engine->readyh = readyh;
	engine->errorh = errorh;
	engine->shuth = shuth;
	engine->arg = arg;

	*enginep = engine;

 out:
	if (err)
		mem_deref(engine);
	return err;
}


/*** engine_active_handler
 */

static bool need_sync(struct engine *engine)
{
	struct sobject *so;
	uint8_t ret;
	int err;

	if (engine->need_sync || !engine->store) {
		debug("Sync explicitely requested or no store.\n");
		return true;
	}

	err = store_user_open(&so, engine->store, "state", "need_sync", "rb");
	if (err) {
		debug("store_user_open() failed: %m.\n", err);
		return true;
	}
	err = sobject_read_u8(&ret, so);
	if (err) {
		debug("Reading 'need_sync' from store failed: %m.\n", err);
		ret = true;
	}
	sobject_close(so);
	mem_deref(so);
	if (ret)
		debug("Store says we need sync.\n");
	return ret;
}


void engine_active_handler(struct engine *engine)
{
	struct le *le;

	if (!engine)
		return;

	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->state != ENGINE_STATE_ACTIVE)
			return;
	}

	engine->state = ENGINE_STATE_ACTIVE;

	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->module->activeh)
			ms->module->activeh(engine, ms);
	}

	if (engine->readyh)
		engine->readyh(engine->arg);

	if (need_sync(engine))
		engine_start_sync(engine);
}


/*** engine_error
 */

void engine_error(struct engine *engine, int err)
{
	info("engine_error: %m\n", err);

	if (engine->errorh)
		engine->errorh(err, engine->arg);
	engine_shutdown(engine);
}


/*** engine_shutdown
 */

void engine_shutdown(struct engine *engine)
{
	struct le *le;

	if (!engine || engine->state == ENGINE_STATE_SHUTDOWN)
		return;

	engine->state = ENGINE_STATE_SHUTDOWN;

	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->module->shuth) {
			ms->state = ENGINE_STATE_SHUTDOWN;
			ms->module->shuth(engine, ms);
		}
		else
			ms->state = ENGINE_STATE_DEAD;
	}

	engine_shutdown_handler(engine);
}


/*** engine_shutdown_handler
 */

void engine_shutdown_handler(struct engine *engine)
{
	struct le *le;

	if (!engine)
		return;

	LIST_FOREACH(&engine->modulel, le) {
		struct engine_module_state *ms = le->data;

		if (ms->state != ENGINE_STATE_DEAD)
			return;
	}

	engine->state = ENGINE_STATE_DEAD;
	if (engine->shuth)
		engine->shuth(engine->arg);
}

/*** engine_get_login_token
 */

const char *engine_get_login_token(struct engine *engine)
{
	if (!engine || !engine->login)
		return NULL;

	return login_get_token(engine->login)->access_token;
}


/*** engine_lsnr_(de)register
 */

int engine_lsnr_register(struct engine *engine,
			 struct engine_lsnr *lsnr)
{
	if (!engine || !lsnr)
		return EINVAL;

	list_append(&engine->lsnrl, &lsnr->le, lsnr);
	return 0;
}


void engine_lsnr_unregister(struct engine_lsnr *lsnr)
{
	if (!lsnr)
		return;

	list_unlink(&lsnr->le);
}


struct rest_cli *engine_get_restcli(struct engine *engine)
{
	return engine ? engine->rest : NULL;
}


const char *engine_get_msys(void)
{
	return engine_global.msys;
}


struct trace *engine_get_trace(struct engine *engine)
{
	return engine ? engine->trace : NULL;
}


int engine_restart(struct engine *eng)
{
	int err;

	if (!eng)
		return EINVAL;

	err = dns_restart(eng->dnsc);
	if (err)
		return err;

	err = engine_event_restart(eng);
	if (err)
		return err;

	return err;
}
