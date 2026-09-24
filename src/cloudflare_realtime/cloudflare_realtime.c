/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#include <errno.h>
#include <string.h>

#include <re.h>
#include <avs.h>

#define AVS_CCALL_INCLUDED
#include "cloudflare_realtime.h"
#include "../realtime_bridge/realtime_bridge.h"

struct cloudflare_realtime {
	struct ccall *ccall;
	struct realtime_bridge *bridge;
	char *app_id;
	char *app_secret;
	char *api_base;
	char *tracks_json;
	cloudflare_realtime_request_h *requesth;
	void *arg;
	bool enabled;
	enum cloudflare_realtime_operation pending[2];
};

static void request_response(struct cloudflare_realtime *adapter,
			     enum ccall_ecall_role role,
			     int status, const char *body, void *arg);

static bool valid_role(enum ccall_ecall_role role)
{
	return role == CCALL_ECALL_PUBLISHER || role == CCALL_ECALL_SUBSCRIBER;
}

static const char *session_id(const struct cloudflare_realtime *adapter, enum ccall_ecall_role role)
{
	const struct realtime_bridge_session *session;

	if (!adapter || !adapter->bridge || !valid_role(role))
		return NULL;

	session = &adapter->bridge->session[role];
	return session->session_id;
}

static int make_request_body(char **bodyp, bool offer, const char *sdp, const char *tracks_json)
{
	struct json_object *root = NULL;
	struct json_object *description = NULL;
	struct json_object *tracks = NULL;
	int err;

	if (!bodyp || !sdp)
		return EINVAL;

	root = jzon_alloc_object();
	description = jzon_alloc_object();
	if (!root || !description) {
		err = ENOMEM;
		goto out;
	}

	json_object_object_add(description, "type", json_object_new_string(offer ? "offer" : "answer"));
	json_object_object_add(description, "sdp", json_object_new_string(sdp));
	json_object_object_add(root, "sessionDescription", description);
	description = NULL;

	if (tracks_json) {
		err = jzon_decode(&tracks, tracks_json, strlen(tracks_json));
		if (err)
			goto out;
		json_object_object_add(root, "tracks", tracks);
		tracks = NULL;
	}

	err = jzon_encode(bodyp, root);

out:
	mem_deref(tracks);
	mem_deref(description);
	mem_deref(root);
	return err;
}

static int request_role(struct cloudflare_realtime *adapter, enum ccall_ecall_role role, struct econn_message *msg)
{
	const char *sid;
	const char *method;
	char pathbuf[256];
	char *body = NULL;
	int err;
	enum cloudflare_realtime_operation operation;

	if (!adapter || !valid_role(role) || !msg || !adapter->requesth)
		return EINVAL;
	if (msg->msg_type != ECONN_SETUP && msg->msg_type != ECONN_UPDATE)
		return EPROTONOSUPPORT;
	if (!msg->u.setup.sdp_msg)
		return EINVAL;
	err = cloudflare_realtime_map_message(adapter, role, msg, &operation);
	if (err)
		return err;
	if (adapter->pending[role] != CLOUDFLARE_REALTIME_OP_NONE)
		return EBUSY;

	sid = session_id(adapter, role);
	if (!sid && (role == CCALL_ECALL_SUBSCRIBER || msg->msg_type == ECONN_UPDATE))
		return ENOTCONN;

	err = make_request_body(&body, !msg->resp, msg->u.setup.sdp_msg,
				role == CCALL_ECALL_SUBSCRIBER
				? adapter->tracks_json : NULL);
	if (err)
		return err;

	if (role == CCALL_ECALL_PUBLISHER && !sid) {
		method = "POST";
		str_ncpy(pathbuf, "/sessions/new", sizeof(pathbuf));
	}
	else if (msg->msg_type == ECONN_UPDATE) {
		method = "PUT";
		re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/renegotiate", sid);
	}
	else {
		method = "POST";
		re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/tracks/new", sid);
	}
	adapter->pending[role] = operation;

	err = adapter->requesth(adapter, role, method, pathbuf,
				adapter->app_id, adapter->app_secret, body,
				request_response, adapter->arg);
	mem_deref(body);
	if (err)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
	return err;
}

int cloudflare_realtime_map_message(
				struct cloudflare_realtime *adapter,
				enum ccall_ecall_role role,
				const struct econn_message *msg,
				enum cloudflare_realtime_operation *operation)
{
	const char *sid;

	if (!adapter || !valid_role(role) || !msg || !operation)
		return EINVAL;
	if (msg->msg_type != ECONN_SETUP && msg->msg_type != ECONN_UPDATE)
		return EPROTONOSUPPORT;

	sid = session_id(adapter, role);
	if (msg->msg_type == ECONN_UPDATE) {
		if (!sid)
			return ENOTCONN;
		*operation = CLOUDFLARE_REALTIME_OP_RENEGOTIATE;
	}
	else if (role == CCALL_ECALL_PUBLISHER && !sid) {
		*operation = CLOUDFLARE_REALTIME_OP_CREATE_SESSION;
	}
	else if (role == CCALL_ECALL_PUBLISHER) {
		*operation = CLOUDFLARE_REALTIME_OP_PUBLISH_TRACKS;
	}
	else {
		if (!sid)
			return ENOTCONN;
		*operation = CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS;
	}

	return 0;
}

static void request_response(struct cloudflare_realtime *adapter,
			     enum ccall_ecall_role role,
			     int status, const char *body, void *arg)
{
	int err;
	(void)arg;

	err = cloudflare_realtime_handle_response(adapter, role, status, body);
	if (err)
		warning("cloudflare realtime: response handling failed: %m\n", err);
}

static int bridge_sendh(struct realtime_bridge *bridge,
			 enum ccall_ecall_role role,
			 struct econn_message *msg, void *arg)
{
	(void)bridge;
	return request_role(arg, role, msg);
}

static int ccall_sendh(struct ccall *ccall, enum ccall_ecall_role role,
			struct econn_message *msg, void *arg)
{
	struct cloudflare_realtime *adapter = arg;
	(void)ccall;
	return realtime_bridge_send(adapter->bridge, role, msg);
}

static void ccall_eventh(struct ccall *ccall, enum ccall_ecall_role role,
				enum ccall_transport_event event,
				const struct ccall_transport_state *state,
				void *arg)
{
	struct cloudflare_realtime *adapter = arg;
	(void)ccall;
	(void)state;

	if (event == CCALL_TRANSPORT_CLOSED)
		realtime_bridge_stop_role(adapter->bridge, role);
}

int cloudflare_realtime_alloc(struct cloudflare_realtime **adapterp,
			      struct ccall *ccall,
			      const struct cloudflare_realtime_conf *conf,
			      void *arg)
{
	struct cloudflare_realtime *adapter;
	struct realtime_bridge_handlers handlers = {
		.sendh = bridge_sendh,
	};
	struct ccall_transport_handlers transport = {
		.sendh = ccall_sendh,
		.eventh = ccall_eventh,
	};
	int err;

	if (!adapterp || !ccall || !conf || !conf->requesth)
		return EINVAL;

	adapter = mem_zalloc(sizeof(*adapter), NULL);
	if (!adapter)
		return ENOMEM;

	adapter->ccall = ccall;
	adapter->requesth = conf->requesth;
	adapter->arg = arg;
	err = str_dup(&adapter->app_id, conf->app_id);
	if (!err)
		err = str_dup(&adapter->app_secret, conf->app_secret);
	if (!err)
		err = str_dup(&adapter->api_base, conf->api_base);
	if (err)
		goto out;

	err = realtime_bridge_alloc(&adapter->bridge, ccall, &handlers, adapter);
	if (err)
		goto out;

	err = ccall_set_transport_handlers(ccall, &transport, adapter);
	if (err)
		goto out;

	*adapterp = adapter;
	return 0;

out:
	cloudflare_realtime_close(adapter);
	return err;
}

int cloudflare_realtime_enable(struct cloudflare_realtime *adapter,
			       bool enabled)
{
	int err;

	if (!adapter)
		return EINVAL;
	err = realtime_bridge_enable(adapter->bridge, enabled);
	if (err)
		return err;
	adapter->enabled = enabled;
	if (enabled) {
		err = realtime_bridge_start_role(adapter->bridge,
					 CCALL_ECALL_PUBLISHER);
		if (!err)
			err = realtime_bridge_start_role(adapter->bridge,
						 CCALL_ECALL_SUBSCRIBER);
	}
	return err;
}

int cloudflare_realtime_set_session(struct cloudflare_realtime *adapter,
				    enum ccall_ecall_role role,
				    const char *session_id)
{
	if (!adapter || !valid_role(role) || !session_id)
		return EINVAL;
	return realtime_bridge_set_session(adapter->bridge, role, session_id,
					   adapter->api_base);
}

int cloudflare_realtime_set_subscribe_tracks(
				struct cloudflare_realtime *adapter,
				const char *tracks_json)
{
	struct json_object *tracks = NULL;
	int err;

	if (!adapter || !tracks_json)
		return EINVAL;
	err = jzon_decode(&tracks, tracks_json, strlen(tracks_json));
	if (err)
		return err;
	mem_deref(tracks);
	return str_dup(&adapter->tracks_json, tracks_json);
}

int cloudflare_realtime_handle_response(
				struct cloudflare_realtime *adapter,
				enum ccall_ecall_role role,
				int status,
				const char *body)
{
	struct json_object *root = NULL;
	struct json_object *description = NULL;
	struct ecall *ecall;
	const char *sdp;
	const char *sid;
	const char *type;
	struct econn_message msg;
	int err;

	if (!adapter || !valid_role(role) || status < 200 || status >= 300 || !body)
		return status >= 400 ? EPROTO : EINVAL;
	if (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_NONE)
		return EPROTO;

	err = jzon_decode(&root, body, strlen(body));
	if (err)
		goto out;
	err = jzon_object(&description, root, "sessionDescription");
	if (err)
		goto out;
	sdp = jzon_str(description, "sdp");
	type = jzon_str(description, "type");
	if (!sdp || !type) {
		err = EPROTO;
		goto out;
	}

	sid = jzon_str(root, "sessionId");
	if (sid) {
		err = cloudflare_realtime_set_session(adapter, role, sid);
		if (err)
			goto out;
	}

	ecall = ccall_get_ecall(adapter->ccall, role);
	if (!ecall) {
		err = ENOENT;
		goto out;
	}

	err = econn_message_init(&msg,
				 adapter->pending[role] == CLOUDFLARE_REALTIME_OP_RENEGOTIATE
				 ? ECONN_UPDATE : ECONN_SETUP,
				 sid ? sid : "cloudflare");
	if (err)
		goto out;
	msg.resp = 0 == strcmp(type, "answer");
	err = str_dup(&msg.u.setup.sdp_msg, sdp);
	if (!err)
		err = ecall_msg_recv(ecall, 0, 0, "cloudflare", "realtime", &msg);
	econn_message_reset(&msg);
	adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;

out:
	if (err)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
	mem_deref(description);
	mem_deref(root);
	return err ? err : 0;
}

void cloudflare_realtime_close(struct cloudflare_realtime *adapter)
{
	if (!adapter)
		return;

	if (adapter->ccall)
		ccall_set_transport_handlers(adapter->ccall, NULL, NULL);
	realtime_bridge_close(adapter->bridge);
	mem_deref(adapter->app_id);
	mem_deref(adapter->app_secret);
	mem_deref(adapter->api_base);
	mem_deref(adapter->tracks_json);
	mem_deref(adapter);
}
