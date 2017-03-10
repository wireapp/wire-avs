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


/*** sending of notifications
 */

static void send_call_notifications(struct engine_conv *conv)
{
	struct le *le;

	LIST_FOREACH(&conv->engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->callh)
			lsnr->callh(conv, lsnr->arg);
	}
}


/*** call.state events
 */

static bool update_call_state(struct engine_conv *conv,
			      struct json_object *jobj)
{
	const char *state;

	state = jzon_str(jobj, "state");
	if (state == NULL)
		return false;

	if (streq(state, "idle") && conv->device_in_call) {
		conv->device_in_call = false;
		flowmgr_release_flows(conv->engine->call->flowmgr, conv->id);
		return true;
	}
	if (streq(state, "joined") && !conv->device_in_call) {
		conv->device_in_call = true;
		return true;
	}

	return false;
}


static void send_call_participant(struct engine_conv *conv,
				  struct engine_user *user, bool joined)
{
	struct le *le;

	LIST_FOREACH(&conv->engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->callparth)
			lsnr->callparth(conv, user, joined, lsnr->arg);
	}
}


static int update_participant(struct engine_conv *conv, const char *user_id,
			      const char *state, double quality)
{
	struct le *le;
	bool joined;

	if (streq(state, "joined"))
		joined = true;
	else if (streq(state, "idle"))
		joined = false;
	else {
		/* We skip unknown states sort of quietly.
		 */
		warning("engine: Unrecognised call state '%s'"
			" for userid %s -- Skipping.\n", state, user_id);
		return 0;
	}

	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *member = le->data;

		if (!streq(member->user->id, user_id))
			continue;

		if (joined != member->in_call) {
			member->in_call = joined;
			send_call_participant(conv, member->user, joined);
		}

		return 0;
	}

	/* XXX New member? The hell?
	 */
	return 0;
}


static bool part_handler(const char *key, struct json_object *jobj,
			 void *arg)
{
	const char *user_id = key;
	const char *state;
	void **argv = arg;
	struct engine_conv *conv = argv[0];
	bool *user_in_call = argv[1];
	int err = 0;

	state = jzon_str(jobj, "state");
	if (!state)
		state = "idle";

	if (engine_is_self(conv->engine, user_id))
		*user_in_call = streq(state, "joined");
	else {
		err = update_participant(conv, user_id, state, 0.5);
		if (err)
			goto out;
	}

 out:
	return err != 0;
}


static bool update_participants(struct engine_conv *conv,
				struct json_object *jobj)
{
	struct le *le;
	bool user_in_call = false;
	int others_in_call;
	void *argv[2];

	argv[0] = conv;
	argv[1] = &user_in_call;
	(void)jzon_apply(jobj, part_handler, argv);

	others_in_call = 0;
	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *member = le->data;

		if (member->in_call) {
			others_in_call += 1;
		}
	}

	if (user_in_call && others_in_call) {
		flowmgr_set_active(engine_get_flowmgr(conv->engine),
				   conv->id, true);
	}

	if (user_in_call != conv->user_in_call
	    || others_in_call != conv->others_in_call)
	{
		conv->others_in_call = others_in_call;
		conv->user_in_call = user_in_call;
		return true;
	}

	return false;
}


static void process_call_state(struct engine_conv *conv,
			       struct json_object *jobj, bool notify)
{
	struct json_object *jself = NULL;
	struct json_object *jpart = NULL;
	bool changes;

	/* Be robust and ignore attributes of wrong type.
	 */
	jzon_object(&jself, jobj, "self");
	jzon_object(&jpart, jobj, "participants");

	changes = false;
	if (jself) {
		debug("process_call_state: got self.\n");
		changes |= update_call_state(conv, jself);
	}
	if (jpart) {
		debug("process_call_state: got participants.\n");
		changes |= update_participants(conv, jpart);
	}

	if (changes) {
		engine_save_conv(conv);
		if (notify)
			send_call_notifications(conv);
	}

}


static void call_state_handler(struct engine *engine, const char *type,
			       struct json_object *jobj, bool catchup)
{
	const char *conv_id;
	const char *sess_id;
	struct engine_conv *conv;
	struct flowmgr *fm = engine_get_flowmgr(engine);
	int err;

	conv_id = jzon_str(jobj, "conversation");
	if (!conv_id)
		return;

	sess_id = jzon_str(jobj, "session");
	if (sess_id && fm)
		flowmgr_set_sessid(fm, conv_id, sess_id);

	err = engine_lookup_conv(&conv, engine, conv_id);
	if (err) {
		info("call.state event for unknown conversation '%s'.\n",
		     conv_id);
		return;
	}

	process_call_state(conv, jobj, !catchup);
}


struct engine_event_lsnr call_state_lsnr = {
	.type = "call.state",
	.eventh = call_state_handler
};


/*** set device state
 */

static int set_device_state(struct engine_conv *conv,
			    bool in_call,
			    bool video_enabled,
			    const char *state, double quality,
			    rest_resp_h *resph, void *arg)
{
	struct json_object *jobj, *self, *inner;
	char path[512];
	int err = 0;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	self = json_object_new_object();
	if (!self) {
		err = ENOMEM;
		goto out;
	}

	inner = json_object_new_string(state);
	if (inner == NULL) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(self, "state", inner);
	inner = json_object_new_double(quality);
	if (inner == NULL) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(self, "quality", inner);
	if (streq(state, "joined")) {
		inner = json_object_new_boolean(video_enabled);
		json_object_object_add(self, "videod", inner);
	}

	json_object_object_add(jobj, "self", self);

	err = re_snprintf(path, sizeof(path),
			  "/conversations/%s/call/state",
			  conv->id);
	if (err == -1) {
		err = EINVAL;
		goto out;
	}
	err = rest_request(NULL, conv->engine->rest, 0, "PUT", resph, arg,
			   path, "%H", jzon_print, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);

	return err;
}


/*** engine_join_call
 */

static void join_call_state_handler(int err, const struct http_msg *msg,
		struct mbuf *mb, struct json_object *jobj, void *arg)
{
	struct engine_conv *conv = arg;
	struct le *le;

	if (err) {
		error("Joining call failed: %m.\n", err);
		return;
	}
	if (msg && msg->scode != 200) {
		error("Joining call failed: %i %r.\n", (int) msg->scode,
		      &msg->reason);
		return;
	}

	{
		struct engine_user *self = engine_get_self(conv->engine);
		if (self) {
			flowmgr_set_self_userid(conv->engine->call->flowmgr,
						self->id);
		}
	}

	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		if (!mbr->in_call)
			continue;

		flowmgr_user_add(conv->engine->call->flowmgr,
				 conv->id,
				 mbr->user->id,
				 mbr->user->display_name);
	}

	flowmgr_acquire_flows(conv->engine->call->flowmgr, conv->id,
			      NULL, NULL, NULL);

	process_call_state(conv, jobj, true);
}


int engine_join_call(struct engine_conv *conv, bool video_enabled)
{
	int err;

	if (!conv)
		return EINVAL;

	if (conv->device_in_call) {
		info("Already in call ...\n");
		return 0;
	}

	err = set_device_state(conv, true, video_enabled, "joined", .5,
			       join_call_state_handler, conv);
	if (err)
		goto out;

out:
	return err;
}


/*** engine_leave_call
 */

static void leave_call_state_handler(int err, const struct http_msg *msg,
		struct mbuf *mb, struct json_object *jobj, void *arg)
{
	struct engine_conv *conv = arg;

	if (err) {
		error("Leaving call failed: %m.\n", err);
		return;
	}
	if (msg && msg->scode != 200) {
		error("Leaving call failed: %i %r.\n", (int) msg->scode,
		      &msg->reason);
		return;
	}

	process_call_state(conv, jobj, true);
}


int engine_leave_call(struct engine_conv *conv)
{
	int err;

	if (!conv)
		return EINVAL;

	if (!conv->device_in_call)
		return ENOENT;

	err = set_device_state(conv, false, false, "idle", .5,
			       leave_call_state_handler, conv);

	flowmgr_release_flows(conv->engine->call->flowmgr, conv->id);

	return err;
}


/*** engine_call_post_conv_load
 */

void engine_call_post_conv_load(struct engine_conv *conv)
{
	conv->device_in_call = false;
}


/*** engine_call_post_conv_sync
 */

static void get_call_state_handler(int err, const struct http_msg *msg,
				   struct mbuf *mb, struct json_object *jobj,
				   void *arg)
{
	struct engine_conv *conv = arg;

	if (err == ECONNABORTED)
		return;

	err = rest_err(err, msg);
	if (err && err != ENOENT) {
		error("sync error: failed to fetch call state for "
		      "conversation %s (%m).\n",
		      conv->id, err);
		return;
	}

	process_call_state(conv, jobj, true);
}


void engine_call_post_conv_sync(struct engine_conv *conv)
{
	int err;

	if ((conv->type != ENGINE_CONV_REGULAR
	     && conv->type != ENGINE_CONV_ONE)
	    || !conv->active)
	{
		return;
	}

	err = rest_get(NULL, conv->engine->rest, 1, get_call_state_handler,
		       conv, "/conversations/%s/call/state", conv->id);
	if (err) {
		error("sync error: failed to fetch call state for "
		      "conversation %s (%m).\n",
		      conv->id, err);
	}
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
	struct engine *engine = arg;
	struct engine_conv *conv;

	warning("engine: flow failed on convid='%s' (%m)\n",
		convid, err);

	err = engine_lookup_conv(&conv, engine, convid);
	if (err) {
		debug("flow_err_handler: "
		      "engine_lookup_conv for '%s' failed (%m).\n",
		      convid, err);
		return;
	}

	engine_leave_call(conv);
}


static void flowmgr_event_handler(struct engine *engine, const char *type,
			          struct json_object *jobj, bool catchup)
{
	char *jstr = NULL;
	int err;

	err = jzon_encode(&jstr, jobj);
	if (err) {
		warning("engine: event_handler: jzon_encode failed (%m)\n",
			err);
		return;
	}

	flowmgr_process_event(NULL, engine->call->flowmgr,
			      "application/json", jstr, str_len(jstr));

	mem_deref(jstr);
}

struct engine_event_lsnr flowmgr_event_lsnr = {
	.type = NULL,
	.eventh = flowmgr_event_handler
};


/*** init handler
 */

static int init_handler(void)
{
	int err;

	if (str_isset(engine_get_msys())) {

		err = flowmgr_init(engine_get_msys(), NULL,
				   TLS_KEYTYPE_EC);
		if (err)
			return err;
	}

	engine_event_register(&call_state_lsnr);
	engine_event_register(&flowmgr_event_lsnr);

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


/*** engine_get_audio_mute
 */

int engine_get_audio_mute(struct engine *engine, bool *muted)
{
	if (!engine || !engine->call || !muted)
		return EINVAL;

	return flowmgr_get_mute(engine->call->flowmgr, muted);
}


/*** engine_set_audio_mute
 */

int engine_set_audio_mute(struct engine *engine, bool mute)
{
	if (!engine || !engine->call)
		return EINVAL;

	return flowmgr_set_mute(engine->call->flowmgr, mute);
}

