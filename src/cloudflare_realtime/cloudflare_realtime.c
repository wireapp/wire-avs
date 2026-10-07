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

struct cloudflare_retry {
	struct cloudflare_realtime *adapter;
	enum ccall_ecall_role role;
	struct tmr timer;
	char *method;
	char *path;
	char *body;
	unsigned attempt;
};

struct cloudflare_realtime {
	struct ccall *ccall;
	struct realtime_bridge *bridge;
	char *app_id;
	char *app_secret;
	char *api_base;
	char *tracks_json;
	char *publisher_tracks;
	char *publisher_offer_tracks;
	bool awaiting_update_apply;
	bool publisher_closing_tracks;
	bool publisher_force_pending;
	char *publisher_force_tracks;
	char *publisher_publish_body;
	enum econn_msg pending_message[2];
	char *datachannel_id;
	cloudflare_realtime_request_h *requesth;
	void *arg;
	bool enabled;
	enum cloudflare_realtime_operation pending[2];
	bool pending_cancelled[2];
	uint32_t request_sequence;
	uint32_t request_id[2];
	uint64_t request_started[2];
	struct cloudflare_retry retry[2];
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

static void retry_clear(struct cloudflare_retry *retry)
{
	tmr_cancel(&retry->timer);
	retry->method = mem_deref(retry->method);
	retry->path = mem_deref(retry->path);
	retry->body = mem_deref(retry->body);
	retry->attempt = 0;
}

/* Diagnostics only: do not log credentials or full SDP/candidates. */
static int dispatch_request(struct cloudflare_retry *retry)
{
	struct cloudflare_realtime *adapter = retry->adapter;
	enum ccall_ecall_role role = retry->role;
	struct json_object *root = NULL, *tracks = NULL;
	uint32_t id = ++adapter->request_sequence;
	int err;

	++retry->attempt;
	adapter->request_id[role] = id;
	adapter->request_started[role] = tmr_jiffies();
	info("cloudflare(%p): request-start id=%u attempt=%u/3 role=%d session=%s method=%s path=%s pending=%d awaiting-sdp=%d time=%llu\n",
	     adapter, id, retry->attempt, role,
	     session_id(adapter, role) ? session_id(adapter, role) : "(new)",
	     retry->method, retry->path, adapter->pending[role], adapter->awaiting_update_apply,
	     (unsigned long long)adapter->request_started[role]);
	if (retry->body && !jzon_decode(&root, retry->body, strlen(retry->body)) &&
	    !jzon_array(&tracks, root, "tracks")) {
		int i;
		for (i = 0; i < json_object_array_length(tracks); ++i) {
			struct json_object *track = json_object_array_get_idx(tracks, i);
			const char *mid = jzon_str(track, "mid");
			const char *name = jzon_str(track, "trackName");
			info("cloudflare(%p): request-track id=%u mid=%s name=%s\n",
			     adapter, id, mid ? mid : "(none)", name ? name : "(none)");
		}
	}
	mem_deref(root);
	err = adapter->requesth(adapter, role, retry->method, retry->path,
	                        adapter->app_id, adapter->app_secret, retry->body,
	                        request_response, adapter->arg);
	if (err)
		info("cloudflare(%p): request-dispatch-failed id=%u err=%d\n", adapter, id, err);
	return err;
}

static void retry_timeout(void *arg)
{
	struct cloudflare_retry *retry = arg;
	struct cloudflare_realtime *adapter = retry->adapter;
	int err;

	if (adapter->pending_cancelled[retry->role] ||
	    adapter->ccall->state == CCALL_STATE_IDLE ||
	    adapter->ccall->state == CCALL_STATE_TERMINATING ||
	    adapter->ccall->transport_ending) {
		retry_clear(retry);
		return;
	}
	err = dispatch_request(retry);
	if (err)
		request_response(adapter, retry->role, 599, NULL, adapter->arg);
}

static int traced_request(struct cloudflare_realtime *adapter,
                          enum ccall_ecall_role role,
                          const char *method, const char *path,
                          const char *app_id, const char *app_secret,
                          const char *body,
                          cloudflare_realtime_response_h *responseh,
                          void *arg)
{
	struct cloudflare_retry *retry = &adapter->retry[role];
	int err;
	(void)app_id; (void)app_secret; (void)responseh; (void)arg;

	retry_clear(retry);
	retry->adapter = adapter;
	retry->role = role;
	err = str_dup(&retry->method, method);
	if (!err) err = str_dup(&retry->path, path);
	if (!err && body) err = str_dup(&retry->body, body);
	if (err) {
		retry_clear(retry);
		return err;
	}
	return dispatch_request(retry);
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
			kind = (len >= 9 && !strncmp(line, "m=video 0", 9) &&
				(len == 9 || line[9] == ' ' || line[9] == '\r')) ? NULL : "video";
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

static int new_publisher_tracks(char **jsonp, const char *offered, const char *published)
{
	struct json_object *all = NULL, *old = NULL, *added = NULL;
	int err, i, j;
	if (!jsonp || !offered)
		return EINVAL;
	err = jzon_decode(&all, offered, strlen(offered));
	if (!err && published)
		err = jzon_decode(&old, published, strlen(published));
	if (err)
		goto out;
	added = jzon_alloc_array();
	if (!added) {
		err = ENOMEM;
		goto out;
	}
	for (i = 0; i < json_object_array_length(all); ++i) {
		struct json_object *track = json_object_array_get_idx(all, i);
		bool exists = false;
		for (j = 0; old && j < json_object_array_length(old); ++j) {
			struct json_object *previous = json_object_array_get_idx(old, j);
			if (streq(jzon_str(track, "trackName"), jzon_str(previous, "trackName"))) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			mem_ref(track);
			err = json_object_array_add(added, track);
			if (err) {
				mem_deref(track);
				goto out;
			}
		}
	}
	err = json_object_array_length(added) ? jzon_encode(jsonp, added) : EALREADY;
out:
	mem_deref(all);
	mem_deref(old);
	mem_deref(added);
	return err;
}

static int make_force_close_body(char **bodyp, const char *removed)
{
	struct json_object *tracks = NULL, *root = jzon_alloc_object();
	struct json_object *mids = jzon_alloc_array();
	int err, i;
	err = root && mids ? jzon_decode(&tracks, removed, strlen(removed)) : ENOMEM;
	for (i = 0; !err && i < json_object_array_length(tracks); ++i) {
		struct json_object *track = json_object_array_get_idx(tracks, i);
		struct json_object *item;
		if (!streq(jzon_str(track, "kind"), "video")) { err = ENOTSUP; break; }
		item = jzon_alloc_object();
		if (!item) { err = ENOMEM; break; }
		err = jzon_add_str(item, "mid", "%s", jzon_str(json_object_array_get_idx(tracks, i), "mid"));
		if (!err) err = json_object_array_add(mids, item);
		if (err) mem_deref(item);
	}
	if (!err) {
		json_object_object_add(root, "tracks", mids); mids = NULL;
		err = jzon_add_bool(root, "force", true);
	}
	if (!err) err = jzon_encode(bodyp, root);
	mem_deref(root); mem_deref(tracks); mem_deref(mids);
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
	else if (!msg->resp) {
		char *all = NULL;
		err = make_publisher_tracks(&publisher_tracks,
					    msg->u.setup.sdp_msg);
		if (err)
			return err;
		if (msg->msg_type == ECONN_UPDATE) {
			all = publisher_tracks;
			publisher_tracks = NULL;
			adapter->publisher_closing_tracks = false;
			adapter->publisher_force_tracks = mem_deref(adapter->publisher_force_tracks);
			err = new_publisher_tracks(&publisher_tracks, all, adapter->publisher_tracks);
			if (!err) {
				/* A replacement removes a publication as well as adding one. */
				int removed_err = new_publisher_tracks(&adapter->publisher_force_tracks,
					adapter->publisher_tracks, all);
				if (removed_err != EALREADY) err = removed_err;
			}
			else if (err == EALREADY) {
				err = new_publisher_tracks(&publisher_tracks, adapter->publisher_tracks, all);
				adapter->publisher_closing_tracks = !err;
			}
			if (err) {
				mem_deref(all);
				mem_deref(publisher_tracks);
				adapter->publisher_force_tracks = mem_deref(adapter->publisher_force_tracks);
				return err;
			}
			adapter->publisher_offer_tracks = mem_deref(adapter->publisher_offer_tracks);
			adapter->publisher_offer_tracks = all;
		}
		tracks_json = publisher_tracks;
		if (msg->msg_type == ECONN_SETUP) {
			adapter->publisher_tracks = mem_deref(adapter->publisher_tracks);
			err = str_dup(&adapter->publisher_tracks, publisher_tracks);
		}
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

	if (role == CCALL_ECALL_PUBLISHER && msg->msg_type == ECONN_UPDATE &&
	    adapter->publisher_closing_tracks) {
		struct json_object *root = jzon_alloc_object(), *tracks = NULL;
		struct json_object *mids = jzon_alloc_array(), *desc = jzon_alloc_object();
		char *removed = NULL;
		int i;
		err = root && mids && desc ? new_publisher_tracks(&removed,
			adapter->publisher_tracks, adapter->publisher_offer_tracks) : ENOMEM;
		if (!err) err = jzon_decode(&tracks, removed, strlen(removed));
		for (i = 0; !err && i < json_object_array_length(tracks); ++i) {
			struct json_object *item = jzon_alloc_object();
			if (!item) { err = ENOMEM; break; }
			err = jzon_add_str(item, "mid", "%s", jzon_str(json_object_array_get_idx(tracks, i), "mid"));
			if (!err) err = json_object_array_add(mids, item);
			if (err) mem_deref(item);
		}
		if (!err) err = jzon_add_str(desc, "type", "offer");
		if (!err) err = jzon_add_str(desc, "sdp", "%s", msg->u.setup.sdp_msg);
		if (!err) {
			json_object_object_add(root, "tracks", mids); mids = NULL;
			json_object_object_add(root, "sessionDescription", desc); desc = NULL;
			err = jzon_add_bool(root, "force", false);
		}
		if (!err) {
			body = mem_deref(body);
			err = jzon_encode(&body, root);
		}
		mem_deref(root); mem_deref(tracks); mem_deref(mids);
		mem_deref(desc); mem_deref(removed);
		if (err) { mem_deref(body); return err; }
	}

	if (adapter->publisher_force_tracks && role == CCALL_ECALL_PUBLISHER &&
	    msg->msg_type == ECONN_UPDATE) {
		adapter->publisher_publish_body = mem_deref(adapter->publisher_publish_body);
		adapter->publisher_publish_body = body;
		body = NULL;
		err = make_force_close_body(&body, adapter->publisher_force_tracks);
		if (err) return err;
		adapter->publisher_force_pending = true;
	}

	if (!sid) {
		/* The session is created by ECONN_CONF_CONN before SETUP. */
		return ENOTCONN;
	}
	else if (role == CCALL_ECALL_PUBLISHER && msg->msg_type == ECONN_UPDATE &&
		 (adapter->publisher_closing_tracks || adapter->publisher_force_pending)) {
		method = "PUT";
		re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/tracks/close", sid);
	}
	else if (operation == CLOUDFLARE_REALTIME_OP_RENEGOTIATE) {
		method = "PUT";
		re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/renegotiate", sid);
	}
	else {
		method = "POST";
		re_snprintf(pathbuf, sizeof(pathbuf), "/sessions/%s/tracks/new", sid);
	}
	adapter->pending[role] = operation;
	adapter->pending_message[role] = msg->msg_type;

	err = traced_request(adapter, role, method, pathbuf,
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
	err = traced_request(adapter, CCALL_ECALL_PUBLISHER, "POST",
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
	err = traced_request(adapter, role,
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
		*operation = role == CCALL_ECALL_PUBLISHER && !msg->resp
			? CLOUDFLARE_REALTIME_OP_PUBLISH_TRACKS
			: CLOUDFLARE_REALTIME_OP_RENEGOTIATE;
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
		retry_clear(&adapter->retry[role]);
		adapter->pending_cancelled[role] =
			adapter->pending[role] != CLOUDFLARE_REALTIME_OP_NONE;
		realtime_bridge_stop_role(adapter->bridge, role);
		if (role == CCALL_ECALL_PUBLISHER) {
			adapter->awaiting_update_apply = false;
			adapter->publisher_offer_tracks = mem_deref(adapter->publisher_offer_tracks);
			adapter->publisher_force_pending = false;
			adapter->publisher_force_tracks = mem_deref(adapter->publisher_force_tracks);
			adapter->publisher_publish_body = mem_deref(adapter->publisher_publish_body);
			adapter->datachannel_id = mem_deref(adapter->datachannel_id);
			adapter->publisher_tracks = mem_deref(adapter->publisher_tracks);
		}
	}
	else if (event == CCALL_TRANSPORT_UPDATE_APPLIED && role == CCALL_ECALL_PUBLISHER) {
		int err = state->error;
		info("cloudflare(%p): sdp-applied id=%u role=%d session=%s err=%d elapsed=%llu awaiting-sdp=%d\n",
		     adapter, adapter->request_id[role], role,
		     session_id(adapter, role) ? session_id(adapter, role) : "(none)",
		     err, (unsigned long long)(tmr_jiffies() - adapter->request_started[role]),
		     adapter->awaiting_update_apply);
		if (!adapter->awaiting_update_apply && !err)
			return;
		adapter->awaiting_update_apply = false;
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		if (!err && adapter->publisher_offer_tracks) {
			adapter->publisher_tracks = mem_deref(adapter->publisher_tracks);
			adapter->publisher_tracks = adapter->publisher_offer_tracks;
			adapter->publisher_offer_tracks = NULL;
			err = ccall_catalog_publish_media_update(ccall,
				session_id(adapter, role), adapter->publisher_tracks);
		}
		adapter->publisher_offer_tracks = mem_deref(adapter->publisher_offer_tracks);
		adapter->publisher_force_pending = false;
		adapter->publisher_force_tracks = mem_deref(adapter->publisher_force_tracks);
		adapter->publisher_publish_body = mem_deref(adapter->publisher_publish_body);
		if (err)
			warning("cloudflare: publisher media update failed: %m\n", err);
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

static int can_update(struct ccall *ccall, enum ccall_ecall_role role, void *arg)
{
	struct cloudflare_realtime *adapter = arg;
	return ecall_media_update_pending(ccall_get_ecall(ccall, role)) ||
		adapter->pending[role] != CLOUDFLARE_REALTIME_OP_NONE ||
		adapter->awaiting_update_apply ? EBUSY : 0;
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
		.can_updateh = can_update,
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

	if (adapter && valid_role(role))
		info("cloudflare(%p): http-response id=%u role=%d session=%s status=%d elapsed=%llu pending=%d awaiting-sdp=%d cancelled=%d\n",
		     adapter, adapter->request_id[role], role,
		     session_id(adapter, role) ? session_id(adapter, role) : "(new)",
		     status, (unsigned long long)(tmr_jiffies() - adapter->request_started[role]),
		     adapter->pending[role], adapter->awaiting_update_apply, adapter->pending_cancelled[role]);

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


	/* POC policy: repeat only HTTP 500, at most twice, preserving the body.
	 * Keep pending set across backoff and the subsequent SDP exchange. */
	if (adapter && valid_role(role) && status == 500 &&
	    adapter->retry[role].path && adapter->retry[role].attempt < 3) {
		unsigned delay = 500 * adapter->retry[role].attempt;
		info("cloudflare(%p): retry-scheduled id=%u role=%d next-attempt=%u/3 delay=%u status=500\n",
		     adapter, adapter->request_id[role], role,
		     adapter->retry[role].attempt + 1, delay);
		tmr_start(&adapter->retry[role].timer, delay, retry_timeout, &adapter->retry[role]);
		return 0;
	}
	if (adapter && valid_role(role))
		retry_clear(&adapter->retry[role]);

	if (status < 200 || status >= 300) {
		warning("cloudflare: response status=%d body=%s\n",
			status, body ? body : "(empty)");
		if (adapter && adapter->ccall->recovery_in_progress)
			ccall_pubsub_recovery_failed(adapter->ccall, EPROTO);
		else if (adapter && valid_role(role) && adapter->pending_message[role] == ECONN_UPDATE) {
			adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
			ecall_abort_media_update(ccall_get_ecall(adapter->ccall, role), EPROTO);
		}
		return status >= 400 ? EPROTO : EINVAL;
	}
	if (!adapter || !valid_role(role) || !body)
		return status >= 400 ? EPROTO : EINVAL;
	if (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_NONE)
		return EPROTO;
	if (adapter->awaiting_update_apply && role == CCALL_ECALL_PUBLISHER)
		return EBUSY;

	err = jzon_decode(&root, body, strlen(body));
	if (err)
		goto out;
	if (jzon_str(root, "errorCode")) {
		err = EPROTO;
		goto out;
	}

	if (role == CCALL_ECALL_PUBLISHER && adapter->publisher_force_pending) {
		struct json_object *expected = NULL, *results = NULL;
		char path[256], *publish_body;
		int i, j;
		err = jzon_array(&results, root, "tracks");
		if (!err) err = jzon_decode(&expected, adapter->publisher_force_tracks,
			strlen(adapter->publisher_force_tracks));
		for (i = 0; !err && i < json_object_array_length(results); ++i)
			if (jzon_str(json_object_array_get_idx(results, i), "errorCode")) err = EPROTO;
		for (i = 0; !err && i < json_object_array_length(expected); ++i) {
			const char *mid = jzon_str(json_object_array_get_idx(expected, i), "mid");
			bool found = false;
			for (j = 0; j < json_object_array_length(results); ++j)
				if (streq(mid, jzon_str(json_object_array_get_idx(results, j), "mid"))) found = true;
			if (!found) err = EPROTO;
		}
		mem_deref(expected);
		if (err) goto out;
		adapter->publisher_force_pending = false;
		adapter->publisher_force_tracks = mem_deref(adapter->publisher_force_tracks);
		publish_body = adapter->publisher_publish_body;
		adapter->publisher_publish_body = NULL;
		re_snprintf(path, sizeof(path), "/sessions/%s/tracks/new", session_id(adapter, role));
		err = traced_request(adapter, role, "POST", path,
			adapter->app_id, adapter->app_secret, publish_body,
			request_response, adapter->arg);
		mem_deref(publish_body);
		goto out;
	}

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

	if (role == CCALL_ECALL_PUBLISHER && adapter->publisher_tracks &&
	    adapter->pending_message[role] != ECONN_UPDATE) {
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
				 adapter->pending_message[role] == ECONN_UPDATE ||
				 adapter->pending[role] == CLOUDFLARE_REALTIME_OP_RENEGOTIATE
				 ? ECONN_UPDATE : ECONN_SETUP,
				 sid ? sid : "cloudflare");
	if (err)
		goto out;
	msg.resp = 0 == strcmp(type, "answer");
	if (role == CCALL_ECALL_PUBLISHER && adapter->pending_message[role] == ECONN_UPDATE) {
		struct json_object *results = NULL;
		int i;
		if (!msg.resp || jzon_array(&results, root, "tracks") ||
		    !json_object_array_length(results)) {
			err = EPROTO;
			econn_message_reset(&msg);
			goto out;
		}
		for (i = 0; i < json_object_array_length(results); ++i)
			if (jzon_str(json_object_array_get_idx(results, i), "errorCode")) {
				err = EPROTO;
				econn_message_reset(&msg);
				goto out;
			}
		{
			char *added_json = NULL;
			struct json_object *added = NULL;
			int j;
			err = adapter->publisher_closing_tracks
				? new_publisher_tracks(&added_json, adapter->publisher_tracks, adapter->publisher_offer_tracks)
				: new_publisher_tracks(&added_json, adapter->publisher_offer_tracks, adapter->publisher_tracks);
			if (!err)
				err = jzon_decode(&added, added_json, strlen(added_json));
			for (i = 0; !err && i < json_object_array_length(added); ++i) {
				const char *key = adapter->publisher_closing_tracks ? "mid" : "trackName";
				const char *name = jzon_str(json_object_array_get_idx(added, i), key);
				bool found = false;
				for (j = 0; j < json_object_array_length(results); ++j)
					if (streq(name, jzon_str(json_object_array_get_idx(results, j), key)))
						found = true;
				if (!found)
					err = EPROTO;
			}
			mem_deref(added_json);
			mem_deref(added);
			if (err) {
				econn_message_reset(&msg);
				goto out;
			}
		}
		adapter->awaiting_update_apply = true;
	}
	err = str_dup(&msg.u.setup.sdp_msg, sdp);
	if (!err)
		err = ecall_msg_recv(ecall, 0, 0, "cloudflare", "realtime", &msg);
	econn_message_reset(&msg);
	if (!adapter->awaiting_update_apply)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;

out:
	if (err)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
	mem_deref(description);
	mem_deref(root);
	if (err && adapter->ccall->recovery_in_progress)
		ccall_pubsub_recovery_failed(adapter->ccall, err);
	else if (err && adapter->pending_message[role] == ECONN_UPDATE)
		ecall_abort_media_update(ccall_get_ecall(adapter->ccall, role), err);
	return err ? err : 0;
}

void cloudflare_realtime_close(struct cloudflare_realtime *adapter)
{
	if (!adapter)
		return;

	retry_clear(&adapter->retry[0]);
	retry_clear(&adapter->retry[1]);
	if (adapter->ccall)
		ccall_set_transport_handlers(adapter->ccall, NULL, NULL);
	realtime_bridge_close(adapter->bridge);
	mem_deref(adapter->app_id);
	mem_deref(adapter->app_secret);
	mem_deref(adapter->api_base);
	mem_deref(adapter->publisher_tracks);
	mem_deref(adapter->publisher_offer_tracks);
	mem_deref(adapter->publisher_force_tracks);
	mem_deref(adapter->publisher_publish_body);
	mem_deref(adapter->tracks_json);
	mem_deref(adapter->datachannel_id);
	mem_deref(adapter);
}
