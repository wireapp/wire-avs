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
};


struct engine_ctx {
	struct engine *engine;
	struct rr_resp *ctx;
};


/*** engine_get_flowmgr
 */

struct flowmgr *engine_get_flowmgr(struct engine *engine)
{
	if (!engine || !engine->call)
		return NULL;

	return engine->call->flowmgr;
}


/*** engine_call_post_conv_load
 */

void engine_call_post_conv_load(struct engine_conv *conv)
{
	//conv->device_in_call = false;
}


void engine_call_post_conv_sync(struct engine_conv *conv)
{
	warning("no-op: %s\n", __FUNCTION__);
}


/*** flowmgr interface
 */

static void flow_response_handler(int err, const struct http_msg *msg,
				  struct mbuf *mb, struct json_object *jobj,
				  void *arg)
{
	struct engine_ctx *etx = arg;
	struct rr_resp *rr = etx->ctx;
	struct flowmgr *fm = flowmgr_rr_flowmgr(rr);
	int status;
	const char *jstr;
	char reason[256]="";
	char *x = NULL;

	if (!rr)
		goto out;

	if (err) {

		if (err == ECONNABORTED)
			return;

		status = -1;
	}
	else {
		if (msg == NULL)
			status = 200;
		else
			status = msg->scode;

		if (msg)
			pl_strcpy(&msg->reason, reason, sizeof(reason));
	}

	if (jobj) {
		err = jzon_encode(&x, jobj);
		if (err)
			goto out;
	}
	jstr = jobj ? x : "[]";

	trace_write(engine_get_trace(etx->engine), "RESP(%p) %d %s -- %s\n",
		    rr, status, reason, jstr);

	if (jobj == NULL) {
		err = flowmgr_resp(fm, status, "", NULL, NULL, 0, rr);
	}
	else {
		err = flowmgr_resp(fm, status, reason, "application/json",
				   jstr, strlen(jstr), rr);
	}
	if (err) {
		warning("conv: flowmgr_resp failed (%m)\n", err);
	}

 out:
	mem_deref(etx);
	mem_deref(x);
}


static int flow_request_handler(struct rr_resp *ctx,
			        const char *path, const char *method,
			        const char *ctype, const char *content,
			        size_t clen, void *arg)
{
	struct engine *engine = arg;
	struct engine_ctx *etx = NULL;

	trace_write(engine_get_trace(engine), "REQ(%p) %s %s %b\n",
		    ctx, method, path, content, clen);

	(void) ctype; /* XXX This should probably be checked.  */

	if (ctx) {
		etx = mem_zalloc(sizeof(*etx), NULL);
		etx->engine = engine;
		etx->ctx = ctx;
	}

#if 0
	re_printf("   @@@ REQ @@@ %s %s [%s %zu bytes]\n",
		  method, path, ctype, clen);
#endif

	return rest_request(NULL, engine->rest, 0, method,
			    ctx ? flow_response_handler : NULL, etx,
			    path,
			    (content && clen) ? "%b" : NULL, content, clen);
}


static void flow_err_handler(int err, const char *convid, void *arg)
{
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


static const char *username_handler(const char *userid, void *arg)
{
	struct engine *engine = arg;
	struct engine_user *user;
	int err;

	err = engine_lookup_user(&user, engine, userid, true);

	return err ? NULL : user->display_name;
}


static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	struct engine_call_data *data;
	int err;

	data = mem_zalloc(sizeof(*data), engine_call_data_destructor);
	if (!data)
		return ENOMEM;

	err = flowmgr_alloc(&data->flowmgr, flow_request_handler,
			    flow_err_handler, engine);
	if (err)
		goto out;

	flowmgr_enable_metrics(data->flowmgr, true);

	engine->call = data;
	list_append(&engine->modulel, &state->le, state);

	flowmgr_set_username_handler(data->flowmgr, username_handler, engine);

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


/*** engine_call_module
 */

struct engine_module engine_call_module = {
	.name = "call",
	.inith = init_handler,
	.alloch = alloc_handler,
	.closeh = close_handler,
};
