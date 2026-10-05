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
#include <stdlib.h>
#include <string.h>

#include <re.h>
#include <avs.h>

#define AVS_CCALL_INCLUDED
#include "cloudflare_realtime.h"
#include "../realtime_bridge/realtime_bridge.h"
#include "../ccall/ccall.h"

struct cloudflare_realtime {
	struct ccall *ccall;
	struct realtime_bridge *bridge;
	char *app_id;
	char *app_secret;
	char *api_base;
	char *tracks_json;
	char *publisher_tracks;
	char *datachannel_id;
	cloudflare_realtime_request_h *requesth;
	void *arg;
	bool enabled;
	enum cloudflare_realtime_operation pending[2];
	bool pending_cancelled[2];
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
		struct json_object *wire_tracks;
		int i;
		err = jzon_decode(&tracks, tracks_json, strlen(tracks_json));
		if (err)
			goto out;
		if (!jzon_is_array(tracks)) {
			err = EINVAL;
			goto out;
		}
		wire_tracks = jzon_alloc_array();
		if (!wire_tracks) {
			err = ENOMEM;
			goto out;
		}
		for (i = 0; i < json_object_array_length(tracks); ++i) {
			struct json_object *item = json_object_array_get_idx(tracks, i);
			struct json_object *wire;
			const char *location = jzon_str(item, "location");
			if (location && !strcmp(location, "local")) {
				/* kind is retained internally for the Catalog, not sent to CF. */
				wire = jzon_alloc_object();
				if (!wire) {
					err = ENOMEM;
					break;
				}
				err = jzon_add_str(wire, "location", "%s", location);
				err |= jzon_add_str(wire, "mid", "%s", jzon_str(item, "mid"));
				err |= jzon_add_str(wire, "trackName", "%s", jzon_str(item, "trackName"));
			}
			else
				wire = mem_ref(item);
			if (!err)
				err = json_object_array_add(wire_tracks, wire);
			if (err) {
				mem_deref(wire);
				break;
			}
		}
		mem_deref(tracks);
		tracks = wire_tracks;
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

static int make_publisher_tracks(char **tracksp, const char *sdp)
{
	struct json_object *tracks = NULL;
	char *encoded = NULL;
	const char *line;
	const char *kind = NULL;
	struct json_object *current_track = NULL;
	bool media_send = true;
	char track_name[128] = {0};
	char current_mid[64] = {0};
	int err = 0;

	if (!tracksp || !sdp)
		return EINVAL;

	tracks = jzon_alloc_array();
	if (!tracks)
		return ENOMEM;

	line = sdp;
	while (*line) {
		struct json_object *track;
		const char *line_end = strchr(line, '\n');
		size_t len;

		if (!line_end)
			line_end = line + strlen(line);
		len = (size_t)(line_end - line);

		if (len >= 7 && 0 == strncmp(line, "m=audio", 7)) {
			kind = "audio";
			current_track = NULL;
			current_mid[0] = '\0';
			media_send = true;
			track_name[0] = '\0';
		}
		else if (len >= 7 && 0 == strncmp(line, "m=video", 7)) {
			kind = "video";
			current_track = NULL;
			current_mid[0] = '\0';
			media_send = true;
			track_name[0] = '\0';
		}
		else if (len >= 13 && 0 == strncmp(line, "m=application", 13)) {
			kind = NULL;
			current_track = NULL;
			current_mid[0] = '\0';
			media_send = false;
			track_name[0] = '\0';
		}
		else if (len >= 10 && 0 == strncmp(line, "a=recvonly", 10))
			media_send = false;
		else if (len >= 10 && 0 == strncmp(line, "a=inactive", 10))
			media_send = false;
		else if (len >= 10 && 0 == strncmp(line, "a=sendonly", 10))
			media_send = true;
		else if (len >= 10 && 0 == strncmp(line, "a=sendrecv", 10))
			media_send = true;
		else if (len > 7 && 0 == strncmp(line, "a=msid:", 7)) {
			const char *track_start = line + 7;
			const char *track_end;
			size_t track_len;

			info("cloudflare: found msid line=%.*s\n", (int)len, line);

			while (track_start < line_end && *track_start != ' ' &&
			       *track_start != '\t')
				++track_start;
			if (track_start < line_end) {
				while (track_start < line_end &&
				       (*track_start == ' ' || *track_start == '\t'))
					++track_start;
				track_end = track_start;
				while (track_end < line_end && *track_end != ' ' &&
				       *track_end != '\t' && *track_end != '\r')
					++track_end;
				track_len = (size_t)(track_end - track_start);
				if (track_len > 0 && track_len < sizeof(track_name)) {
					memcpy(track_name, track_start, track_len);
					track_name[track_len] = '\0';
				}
			}
			info("cloudflare: parsed track name=%s\n",
			     track_name[0] ? track_name : "(empty)");
			if (!current_track && media_send && current_mid[0]) {
				track = jzon_alloc_object();
				if (!track) {
					err = ENOMEM;
					goto out;
				}
				jzon_add_str(track, "location", "%s", "local");
				jzon_add_str(track, "mid", "%s", current_mid);
				jzon_add_str(track, "trackName", "%s", track_name);
				jzon_add_str(track, "kind", "%s", kind);
				json_object_array_add(tracks, track);
				current_track = track;
				info("cloudflare: publisher track name=%s\n", track_name);
			}
		}

		if (kind && media_send && len > 6 &&
		    0 == strncmp(line, "a=mid:", 6)) {
			const char *mid_start = line + 6;
			const char *mid_end = line_end;

			while (mid_end > mid_start &&
			       (mid_end[-1] == '\r' || mid_end[-1] == '\n'))
				--mid_end;
			if (!kind || mid_end <= mid_start ||
			    (size_t)(mid_end - mid_start) >= sizeof(current_mid))
				goto next_line;

			memcpy(current_mid, mid_start,
			       (size_t)(mid_end - mid_start));
			current_mid[mid_end - mid_start] = '\0';
			info("cloudflare: publisher track kind=%s mid=%s track_name=%s\n",
			     kind, current_mid, track_name[0] ? track_name : "(empty)");

			if (track_name[0]) {
				track = jzon_alloc_object();
				if (!track) {
					err = ENOMEM;
					goto out;
				}
				jzon_add_str(track, "location", "%s", "local");
				jzon_add_str(track, "mid", "%s", current_mid);
				jzon_add_str(track, "trackName", "%s", track_name);
				jzon_add_str(track, "kind", "%s", kind);
				json_object_array_add(tracks, track);
				current_track = track;
			}
		}

	next_line:
		line = *line_end ? line_end + 1 : line_end;
	}

	if (json_object_array_length(tracks) == 0) {
		err = EPROTO;
		goto out;
	}

	err = jzon_encode(&encoded, tracks);
	if (!err) {
		info("cloudflare: publisher tracks JSON=%s\n", encoded);
		*tracksp = encoded;
	}

out:
	if (err)
		mem_deref(encoded);
	mem_deref(tracks);
	return err;
}

static int request_role(struct cloudflare_realtime *adapter, enum ccall_ecall_role role, struct econn_message *msg)
{
	const char *sid;
	const char *method;
	char pathbuf[256];
	char *body = NULL;
	char *publisher_tracks = NULL;
	const char *tracks_json = NULL;
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

	if (role == CCALL_ECALL_SUBSCRIBER) {
		tracks_json = adapter->tracks_json;
	}
	else if (msg->msg_type == ECONN_SETUP && !msg->resp) {
		err = make_publisher_tracks(&publisher_tracks,
					    msg->u.setup.sdp_msg);
		if (err)
			return err;
		tracks_json = publisher_tracks;
		adapter->publisher_tracks = mem_deref(adapter->publisher_tracks);
		err = str_dup(&adapter->publisher_tracks, publisher_tracks);
		if (err) {
			mem_deref(publisher_tracks);
			return err;
		}
	}

	err = make_request_body(&body, !msg->resp, msg->u.setup.sdp_msg,
				tracks_json);
	mem_deref(publisher_tracks);
	if (err)
		return err;

	if (!sid) {
		/* The session is created by ECONN_CONF_CONN before SETUP. */
		return ENOTCONN;
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

int cloudflare_realtime_send_datachannel(
				struct cloudflare_realtime *adapter)
{
	struct json_object *root = NULL;
	struct json_object *channels = NULL;
	struct json_object *channel = NULL;
	char *body = NULL;
	char pathbuf[256];
	const char *sid;
	int err;

	if (!adapter || !adapter->requesth)
		return EINVAL;
	sid = session_id(adapter, CCALL_ECALL_PUBLISHER);
	if (!sid)
		return ENOTCONN;
	if (adapter->pending[CCALL_ECALL_PUBLISHER] !=
	    CLOUDFLARE_REALTIME_OP_NONE)
		return EBUSY;
	if (adapter->datachannel_id)
		return EALREADY;

	root = jzon_alloc_object();
	channels = jzon_alloc_array();
	channel = jzon_alloc_object();
	if (!root || !channels || !channel) {
		err = ENOMEM;
		goto out;
	}
	jzon_add_str(channel, "location", "%s", "local");
	jzon_add_str(channel, "dataChannelName", "%s", "calling-3.0");
	json_object_array_add(channels, channel);
	channel = NULL;
	json_object_object_add(root, "dataChannels", channels);
	channels = NULL;
	err = jzon_encode(&body, root);
	if (err)
		goto out;

	re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/datachannels/new", sid);
	adapter->pending[CCALL_ECALL_PUBLISHER] =
		CLOUDFLARE_REALTIME_OP_CREATE_DATACHANNEL;
	err = adapter->requesth(adapter, CCALL_ECALL_PUBLISHER, "POST",
				pathbuf, adapter->app_id, adapter->app_secret, body,
				request_response, adapter->arg);
	if (err)
		adapter->pending[CCALL_ECALL_PUBLISHER] =
			CLOUDFLARE_REALTIME_OP_NONE;

out:
	mem_deref(body);
	mem_deref(channel);
	mem_deref(channels);
	mem_deref(root);
	return err;
}

int cloudflare_realtime_send_conf_conn(
				struct cloudflare_realtime *adapter)
{
	enum ccall_ecall_role role = CCALL_ECALL_PUBLISHER;
	int err;

	if (!adapter || !adapter->requesth)
		return EINVAL;
	if (session_id(adapter, role))
		return EALREADY;
	if (adapter->pending[role] != CLOUDFLARE_REALTIME_OP_NONE)
		return EBUSY;

	adapter->pending[role] = CLOUDFLARE_REALTIME_OP_CREATE_SESSION;
	err = adapter->requesth(adapter, role,
				"POST", "/sessions/new",
				adapter->app_id, adapter->app_secret, NULL,
				request_response, adapter->arg);
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
		return ENOTCONN;
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
	if (msg && msg->msg_type == ECONN_CONF_CONN &&
	    role == CCALL_ECALL_PUBLISHER)
		return cloudflare_realtime_send_conf_conn(arg);
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

	if (event == CCALL_TRANSPORT_CLOSED) {
		adapter->pending_cancelled[role] =
			adapter->pending[role] != CLOUDFLARE_REALTIME_OP_NONE;
		realtime_bridge_stop_role(adapter->bridge, role);
		if (role == CCALL_ECALL_PUBLISHER) {
			adapter->datachannel_id = mem_deref(adapter->datachannel_id);
			adapter->publisher_tracks = mem_deref(adapter->publisher_tracks);
		}
	}
	else if (event == CCALL_TRANSPORT_DATA_READY &&
		 role == CCALL_ECALL_PUBLISHER) {
		int err = cloudflare_realtime_send_datachannel(adapter);
		if (err && err != EALREADY) {
			warning("cloudflare realtime: data channel request failed: %m\n", err);
			if (ccall->recovery_in_progress)
				ccall_pubsub_recovery_failed(ccall, err);
		}
	}
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

	if (adapter && valid_role(role) && adapter->pending_cancelled[role]) {
		adapter->pending_cancelled[role] = false;
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		return ECANCELED;
	}
	/* A response may arrive after the application has stopped the call. */
	if (adapter && valid_role(role) &&
	    (adapter->ccall->state == CCALL_STATE_IDLE ||
	     adapter->ccall->state == CCALL_STATE_TERMINATING ||
	     adapter->ccall->transport_ending)) {
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		return ECANCELED;
	}

	if (status < 200 || status >= 300) {
		warning("cloudflare: response status=%d body=%s\n",
			status, body ? body : "(empty)");
		if (adapter && adapter->ccall->recovery_in_progress)
			ccall_pubsub_recovery_failed(adapter->ccall, EPROTO);
		return status >= 400 ? EPROTO : EINVAL;
	}
	if (!adapter || !valid_role(role) || !body)
		return status >= 400 ? EPROTO : EINVAL;
	if (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_NONE)
		return EPROTO;

	err = jzon_decode(&root, body, strlen(body));
	if (err)
		goto out;

	if (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_CREATE_SESSION) {
		sid = jzon_str(root, "sessionId");
		if (!sid) {
			err = EPROTO;
			goto out;
		}
		err = cloudflare_realtime_set_session(adapter, role, sid);
		if (!err && role == CCALL_ECALL_PUBLISHER) {
			err = ccall_start_ecall(adapter->ccall, role);
			if (err == EALREADY)
				err = 0;
		}
		if (!err)
			adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		goto out;
	}

	if (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_CREATE_DATACHANNEL) {
		int channel_id = -1;
		const char *channel_id_str = NULL;
		struct json_object *channels = NULL;
		struct json_object *channel = NULL;
		int id_err = jzon_int(&channel_id, root, "id");
		if (id_err)
			channel_id_str = jzon_str(root, "id");
		if (channel_id < 0 && !jzon_array(&channels, root, "dataChannels") &&
		    json_object_array_length(channels) > 0) {
			channel = json_object_array_get_idx(channels, 0);
			id_err = jzon_int(&channel_id, channel, "id");
			if (id_err)
				channel_id_str = jzon_str(channel, "id");
		}
		if (channel_id < 0 && !channel_id_str) {
			err = EPROTO;
			mem_deref(channels);
			goto out;
		}
	if (channel_id < 0)
		channel_id = atoi(channel_id_str);
	err = ecall_dce_create(ccall_get_ecall(adapter->ccall,
						CCALL_ECALL_PUBLISHER),
					"calling-3.0", channel_id);
		if (!err) {
			char idbuf[32];
			re_snprintf(idbuf, sizeof(idbuf), "%d", channel_id);
			err = str_dup(&adapter->datachannel_id, idbuf);
		}
		if (!err)
			adapter->ccall->publisher_channel_registered = true;
		if (!err)
			info("cloudflare: registered data channel calling-3.0 id=%d\n",
			     channel_id);
		mem_deref(channels);
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		goto out;
	}

	if (role == CCALL_ECALL_PUBLISHER && adapter->publisher_tracks) {
		err = ccall_catalog_set_tracks(adapter->ccall,
					      session_id(adapter, role),
					      adapter->publisher_tracks);
		if (err)
			goto out;
	}

	err = jzon_object(&description, root, "sessionDescription");
	if (err)
		goto out;
	sdp = jzon_str(description, "sdp");
	type = jzon_str(description, "type");
	if (!sdp || !type) {
		err = EPROTO;
		goto out;
	}
	info("cloudflare: response type=%s direction=%s%s%s\n",
	     type,
	     strstr(sdp, "a=sendonly") ? "sendonly " : "",
	     strstr(sdp, "a=recvonly") ? "recvonly " : "",
	     strstr(sdp, "a=sendrecv") ? "sendrecv" : "");

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
	if (err && adapter->ccall->recovery_in_progress)
		ccall_pubsub_recovery_failed(adapter->ccall, err);
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
	mem_deref(adapter->publisher_tracks);
	mem_deref(adapter->tracks_json);
	mem_deref(adapter->datachannel_id);
	mem_deref(adapter);
}
