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
#include "../catalog/catalog.h"
#include "../ecall/ecall.h"

struct cloudflare_retry {
	struct cloudflare_realtime *adapter;
	enum ccall_ecall_role role;
	struct tmr timer;
	char *method;
	char *path;
	char *body;
	unsigned attempt;
};

struct cloudflare_peer_channel {
	struct le le;
	char *participant_id;
	char *session_id;
	bool ready;
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
	struct tmr join_timer;
	bool subscriber_negotiated;
	bool subscriber_answer_accepted;
	bool subscriber_channel_registered;
	char *joined_keyholder;
	struct list peer_channels;
	struct cloudflare_peer_channel *peer_opening;
	struct json_object *media_current;
	struct json_object *media_pending;
	struct json_object *media_after_close;
	bool media_negotiating;
};

static void request_response(struct cloudflare_realtime *adapter,
			     enum ccall_ecall_role role,
			     int status, const char *body, void *arg);

static int media_progress(struct cloudflare_realtime *adapter);

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

static int join_progress(struct cloudflare_realtime *adapter);
static int peer_progress(struct cloudflare_realtime *adapter);
static int send_catalog(struct cloudflare_realtime *adapter, enum ccall_ecall_role role, bool hello)
{
	struct ccall *ccall = adapter->ccall;
	struct ecall *ecall = ccall_get_ecall(ccall, role);
	struct econn_message msg;
	int err;
	if (!ccall->catalog_complete || !ecall || !ecall_get_econn(ecall)) return EAGAIN;
	err = econn_message_init(&msg, ECONN_CONF_CATALOG, econn_sessid_local(ecall_get_econn(ecall)));
	if (err) return err;
	const struct userinfo *self = userlist_get_self(ccall->userl);
	if (self) {
		str_ncpy(msg.src_userid, self->userid_hash, ECONN_ID_LEN);
		str_ncpy(msg.src_clientid, self->clientid_hash, ECONN_ID_LEN);
	}
	err = hello ? catalog_encode_hello(ccall->catalog, ccall->participant_id, &msg.u.catalog.json)
	            : catalog_encode_snapshot(ccall->catalog, &msg.u.catalog.json);
	if (!err) err = ecall_dce_sendmsg(ecall, &msg);
	econn_message_reset(&msg);
	info("cloudflare: catalog-%s role=%d revision=%u err=%d\n", hello ? "hello" : "broadcast", role, ccall->catalog_revision, err);
	return err;
}

static int channel_request(struct cloudflare_realtime *adapter, bool update)
{
	char *body = NULL;
	char path[256];
	int err;
	const char *target = adapter->peer_opening ? adapter->peer_opening->session_id : adapter->joined_keyholder;
	if (!target) return EINVAL;
	struct json_object *root = jzon_alloc_object(), *channels = jzon_alloc_array(), *channel = jzon_alloc_object();
	if (!root || !channels || !channel) {
		mem_deref(root); mem_deref(channels); mem_deref(channel);
		return ENOMEM;
	}
	err = jzon_add_str(channel, "location", "remote");
	if (!err) err = jzon_add_str(channel, "sessionId", "%s", target);
	if (!err) err = jzon_add_str(channel, "dataChannelName", "calling-3.0");
	if (!err) err = jzon_add_bool(channel, "canReply", adapter->peer_opening == NULL);
	if (!err) {
		json_object_array_add(channels, channel); channel = NULL;
		json_object_object_add(root, "dataChannels", channels); channels = NULL;
		err = jzon_encode(&body, root);
	}
	mem_deref(root); mem_deref(channels); mem_deref(channel);
	if (err) return err;
	re_snprintf(path, sizeof(path), "/sessions/%s/datachannels/%s",
	            session_id(adapter, CCALL_ECALL_SUBSCRIBER), update ? "update" : "new");
	adapter->pending[1] = update ? CLOUDFLARE_REALTIME_OP_GRANT_REPLY : CLOUDFLARE_REALTIME_OP_SUBSCRIBE_DATACHANNEL;
	err = traced_request(adapter, CCALL_ECALL_SUBSCRIBER, update ? "PUT" : "POST", path,
	                     adapter->app_id, adapter->app_secret, body, request_response, adapter->arg);
	mem_deref(body);
	if (err) adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
	return err;
}

static void join_timeout(void *arg)
{
	struct cloudflare_realtime *adapter = arg;
	struct ccall *ccall = adapter->ccall;
	if (ccall->transport_ending || ccall->state == CCALL_STATE_IDLE || ccall->catalog_join_confirmed) return;
	if (adapter->pending[1] != CLOUDFLARE_REALTIME_OP_NONE) {
		tmr_start(&adapter->join_timer, 500, join_timeout, adapter);
		return;
	}
	info("cloudflare: catalog-join retry participant=%s revision=%u\n", ccall->participant_id, ccall->catalog_revision);
	if (channel_request(adapter, true))
		tmr_start(&adapter->join_timer, 1000 + rand_u16() % 1000, join_timeout, adapter);
}

static void hello_send(struct cloudflare_realtime *adapter)
{
	if (!adapter->joined_keyholder || adapter->ccall->catalog_join_confirmed || !adapter->subscriber_channel_registered) return;
	if (!adapter->ccall->transport[1].state.data_ready) return;
	send_catalog(adapter, CCALL_ECALL_SUBSCRIBER, true);
	tmr_start(&adapter->join_timer, 1000 + rand_u16() % 1000, join_timeout, adapter);
}

static int join_progress(struct cloudflare_realtime *adapter)
{
	struct ccall *ccall = adapter->ccall;
	int err;
	if (!ccall->keyholder_session || !ccall->catalog_complete ||
	    !ccall->publisher_channel_registered || !ccall->transport[0].state.data_ready ||
	    adapter->joined_keyholder || ccall->transport_ending) return 0;
	err = str_dup(&adapter->joined_keyholder, ccall->keyholder_session);
	if (err) return err;
	adapter->pending[1] = CLOUDFLARE_REALTIME_OP_CREATE_SESSION;
	err = traced_request(adapter, CCALL_ECALL_SUBSCRIBER, "POST", "/sessions/new",
	                     adapter->app_id, adapter->app_secret, NULL, request_response, adapter->arg);
	if (err) adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
	return err;
}

static void peer_destroy(void *arg)
{
	struct cloudflare_peer_channel *peer = arg;
	list_unlink(&peer->le);
	mem_deref(peer->participant_id);
	mem_deref(peer->session_id);
}

static int peer_progress(struct cloudflare_realtime *adapter)
{
	struct le *le;
	int err;
	if (adapter->media_negotiating || adapter->peer_opening || adapter->pending[1] != CLOUDFLARE_REALTIME_OP_NONE ||
	    adapter->ccall->transport_ending) return 0;
	LIST_FOREACH(&adapter->peer_channels, le) {
		struct cloudflare_peer_channel *peer = le->data;
		if (peer->ready) continue;
		if (!session_id(adapter, CCALL_ECALL_SUBSCRIBER)) {
			adapter->pending[1] = CLOUDFLARE_REALTIME_OP_CREATE_SESSION;
			err = traced_request(adapter, CCALL_ECALL_SUBSCRIBER, "POST", "/sessions/new",
				adapter->app_id, adapter->app_secret, NULL, request_response, adapter->arg);
			if (err) adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
			return err;
		}
		if (!adapter->subscriber_answer_accepted || !adapter->ccall->transport[1].state.media_ready) return 0;
		adapter->peer_opening = peer;
		err = channel_request(adapter, false);
		if (err) adapter->peer_opening = NULL;
		return err;
	}
	return 0;
}

static int peer_enqueue(struct cloudflare_realtime *adapter, struct json_object *root)
{
	struct json_object *entry = NULL;
	struct cloudflare_peer_channel *peer;
	struct le *le;
	int err = jzon_object(&entry, root, "participant");
	if (err) return err;
	const char *pid = jzon_str(entry, "participantId"), *sid = jzon_str(entry, "publisherSessionId");
	if (!pid || !sid) return EINVAL;
	if (streq(pid, adapter->ccall->participant_id)) return 0;
	LIST_FOREACH(&adapter->peer_channels, le) {
		peer = le->data;
		if (streq(peer->participant_id, pid) && streq(peer->session_id, sid)) return 0;
	}
	peer = mem_zalloc(sizeof(*peer), peer_destroy);
	if (!peer) return ENOMEM;
	err = str_dup(&peer->participant_id, pid);
	if (!err) err = str_dup(&peer->session_id, sid);
	if (err) { mem_deref(peer); return err; }
	list_append(&adapter->peer_channels, &peer->le, peer);
	info("cloudflare: peer-channel queued participant=%s session=%s\n", pid, sid);
	return peer_progress(adapter);
}

static void peer_refresh_user(struct cloudflare_realtime *adapter, struct cloudflare_peer_channel *peer)
{
	char *json = NULL;
	struct json_object *entry = NULL, *tracks = NULL;
	struct userinfo *user;
	if (catalog_get_participant(adapter->ccall->catalog, peer->participant_id, &json)) return;
	if (jzon_decode(&entry, json, strlen(json))) { mem_deref(json); return; }
	user = userlist_find_by_real(adapter->ccall->userl, jzon_str(entry, "userId"), jzon_str(entry, "clientId"));
	if (user && user->se_approved && peer->ready) {
		enum icall_vstate previous = user->video_state;
		user->incall_now = true;
		user->video_state = ICALL_VIDEO_STATE_STOPPED;
		if (!jzon_array(&tracks, entry, "tracks")) {
			int i;
			for (i = 0; i < json_object_array_length(tracks); ++i) {
				struct json_object *track = json_object_array_get_idx(tracks, i);
				if (streq(jzon_str(track, "kind"), "audio")) jzon_bool(&user->muted, track, "muted");
				else if (streq(jzon_str(track, "kind"), "video"))
					user->video_state = streq(jzon_str(track, "source"), "screen")
						? ICALL_VIDEO_STATE_SCREENSHARE : ICALL_VIDEO_STATE_STARTED;
			}
		}
		if (previous != user->video_state)
			ICALL_CALL_CB(adapter->ccall->icall, vstate_changedh, &adapter->ccall->icall,
				user->userid_real, user->clientid_real, user->video_state, adapter->ccall->icall.arg);
	}
	mem_deref(entry); mem_deref(json);
}

static void catalog_refresh_participants(struct cloudflare_realtime *adapter)
{
	char *json = NULL;
	struct json_object *root = NULL, *entries = NULL;
	int i;
	if (catalog_encode_snapshot(adapter->ccall->catalog, &json)) return;
	if (!jzon_decode(&root, json, strlen(json)) && !jzon_array(&entries, root, "participants")) {
		for (i = 0; i < json_object_array_length(entries); ++i) {
			struct json_object *entry = json_object_array_get_idx(entries, i);
			const char *pid = jzon_str(entry, "participantId");
			if (!pid || streq(pid, adapter->ccall->participant_id)) continue;
			struct cloudflare_peer_channel peer = {0};
			peer.participant_id = (char *)pid;
			peer.ready = true;
			peer_refresh_user(adapter, &peer);
		}
	}
	mem_deref(root); mem_deref(json);
}

static int peer_request_catalog(struct cloudflare_realtime *adapter, struct cloudflare_peer_channel *peer)
{
	struct ccall *ccall = adapter->ccall;
	struct ecall *publisher = ccall->ecall;
	struct json_object *root = jzon_alloc_object();
	struct econn_message msg;
	int err;
	if (!root) return ENOMEM;
	if (!publisher || !ecall_get_econn(publisher)) { mem_deref(root); return EAGAIN; }
	err = econn_message_init(&msg, ECONN_CONF_CATALOG, econn_sessid_local(ecall_get_econn(publisher)));
	if (err) { mem_deref(root); return err; }
	jzon_add_str(root, "type", "catalog_request");
	jzon_add_str(root, "catalogId", "%s", ccall->convid_real);
	jzon_add_str(root, "participantId", "%s", peer->participant_id);
	jzon_add_str(root, "publisherSessionId", "%s", peer->session_id);
	err = jzon_encode(&msg.u.catalog.json, root);
	if (!err) err = ecall_dce_sendmsg(publisher, &msg);
	info("cloudflare: peer-catalog requested participant=%s err=%d\n", peer->participant_id, err);
	econn_message_reset(&msg);
	mem_deref(root);
	return err;
}

static bool catalog_request_received(struct cloudflare_realtime *adapter, const struct econn_message *msg)
{
	struct json_object *root = NULL;
	struct ccall *ccall = adapter->ccall;
	if (msg->msg_type != ECONN_CONF_CATALOG || !msg->u.catalog.json ||
	    jzon_decode(&root, msg->u.catalog.json, strlen(msg->u.catalog.json))) return false;
	bool request = streq(jzon_str(root, "type"), "catalog_request");
	if (request && streq(jzon_str(root, "catalogId"), ccall->convid_real) &&
	    streq(jzon_str(root, "participantId"), ccall->participant_id) &&
	    streq(jzon_str(root, "publisherSessionId"), ccall->catalog_session))
		send_catalog(adapter, CCALL_ECALL_PUBLISHER, false);
	mem_deref(root);
	return request;
}

static void peer_opened(struct cloudflare_realtime *adapter)
{
	struct cloudflare_peer_channel *peer = adapter->peer_opening;
	if (!peer || adapter->pending[1] != CLOUDFLARE_REALTIME_OP_NONE) return;
	peer->ready = true;
	adapter->peer_opening = NULL;
	/* Presence does not imply a subscribed audio/video track. */
	peer_refresh_user(adapter, peer);
	peer_request_catalog(adapter, peer);
	info("cloudflare: peer-channel ready participant=%s session=%s\n", peer->participant_id, peer->session_id);
	struct ccall *ccall = mem_ref(adapter->ccall);
	peer_progress(adapter);
	media_progress(adapter);
	ICALL_CALL_CB(ccall->icall, group_changedh, &ccall->icall, ccall->icall.arg);
	mem_deref(ccall);
}

static struct cloudflare_peer_channel *peer_sender(struct cloudflare_realtime *adapter,
                                                   const struct econn_message *msg)
{
	struct userinfo *user = userlist_find_by_hash(adapter->ccall->userl, msg->src_userid, msg->src_clientid);
	struct le *le;
	char *pid = NULL;
	struct cloudflare_peer_channel *found = NULL;
	if (!user || re_sdprintf(&pid, "%s.%s", user->userid_real, user->clientid_real)) return NULL;
	LIST_FOREACH(&adapter->peer_channels, le) {
		struct cloudflare_peer_channel *peer = le->data;
		if (peer->ready && streq(peer->participant_id, pid)) found = peer;
	}
	mem_deref(pid);
	return found;
}

/* Desired publications are derived from the validated catalog, never SDP IDs. */
static struct json_object *json_clone(struct json_object *value)
{
 char *json = NULL;
 struct json_object *copy = NULL;
 if (!jzon_encode(&json, value)) jzon_decode(&copy, json, strlen(json));
 mem_deref(json);
 return copy;
}

static bool same_publication(struct json_object *a, struct json_object *b)
{
 return streq(jzon_str(a, "sessionId"), jzon_str(b, "sessionId")) &&
        streq(jzon_str(a, "trackName"), jzon_str(b, "trackName"));
}

static bool contains_publication(struct json_object *array, struct json_object *track)
{
 int i;
 for (i = 0; array && i < json_object_array_length(array); ++i)
  if (same_publication(json_object_array_get_idx(array, i), track)) return true;
 return false;
}

static struct json_object *media_desired(struct cloudflare_realtime *adapter)
{
 struct ccall *call = adapter->ccall;
 struct json_object *root = NULL, *participants = NULL, *desired = jzon_alloc_array();
 char *json = NULL;
 int i, j;
 if (!desired || catalog_encode_snapshot(call->catalog, &json) ||
     jzon_decode(&root, json, strlen(json)) || jzon_array(&participants, root, "participants")) goto error;
 for (i = 0; i < json_object_array_length(participants); ++i) {
  struct json_object *participant = json_object_array_get_idx(participants, i), *tracks = NULL;
  const char *uid = jzon_str(participant, "userId"), *cid = jzon_str(participant, "clientId");
  struct userinfo *user = userlist_find_by_real(call->userl, uid, cid);
  struct le *le;
  bool video = false;
  if (!user || !user->se_approved || streq(jzon_str(participant, "participantId"), call->participant_id)) continue;
  LIST_FOREACH(&call->videol, le) {
   struct icall_client *client = le->data;
   if (streq(client->userid, uid) && streq(client->clientid, cid)) video = true;
  }
  if (jzon_array(&tracks, participant, "tracks")) continue;
  for (j = 0; j < json_object_array_length(tracks); ++j) {
   struct json_object *track = json_object_array_get_idx(tracks, j), *entry;
   const char *kind = jzon_str(track, "kind"), *sid = jzon_str(track, "sessionId");
   if ((!streq(kind, "audio") && !(video && streq(kind, "video"))) ||
       !streq(sid, jzon_str(participant, "publisherSessionId"))) continue;
   entry = jzon_alloc_object();
   if (!entry) goto error;
   jzon_add_str(entry, "sessionId", "%s", sid);
   jzon_add_str(entry, "trackName", "%s", jzon_str(track, "trackName"));
   jzon_add_str(entry, "userId", "%s", uid);
   jzon_add_str(entry, "clientId", "%s", cid);
   jzon_add_str(entry, "kind", "%s", kind);
   if (json_object_array_add(desired, entry)) { mem_deref(entry); goto error; }
  }
 }
 mem_deref(root); mem_deref(json);
 return desired;
error:
 mem_deref(root); mem_deref(json); mem_deref(desired);
 return NULL;
}

static int media_commit_bindings(struct cloudflare_realtime *adapter)
{
 struct ccall *call = adapter->ccall;
 struct ecall *ecall = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER);
 struct le *le;
 char *json = NULL;
 int i, err = 0;
 LIST_FOREACH(&call->userl->users, le) {
  struct userinfo *user = le->data;
  user->audio_subscribed = user->video_subscribed = false;
 }
 for (i = 0; adapter->media_current && i < json_object_array_length(adapter->media_current); ++i) {
  struct json_object *track = json_object_array_get_idx(adapter->media_current, i);
  struct userinfo *user = userlist_find_by_real(call->userl, jzon_str(track, "userId"), jzon_str(track, "clientId"));
  if (!user || !user->se_approved) continue;
  if (streq(jzon_str(track, "kind"), "audio")) user->audio_subscribed = true;
  else user->video_subscribed = true;
 }
 if (ecall && ecall->flow && ecall->flow->set_receive_tracks) {
  if (!adapter->media_current) adapter->media_current = jzon_alloc_array();
  err = adapter->media_current ? jzon_encode(&json, adapter->media_current) : ENOMEM;
  if (!err) err = ecall->flow->set_receive_tracks(ecall->flow, json);
  mem_deref(json);
 }
 ICALL_CALL_CB(call->icall, group_changedh, &call->icall, call->icall.arg);
 return err;
}

static int media_progress(struct cloudflare_realtime *adapter)
{
 struct json_object *desired = NULL, *adds = NULL, *keep = NULL, *wire = NULL, *root = NULL;
 char *body = NULL, path[256];
 int i, err = 0;
 if (!adapter->enabled || !adapter->ccall->catalog_complete || adapter->ccall->transport_ending ||
     !adapter->subscriber_channel_registered || !adapter->subscriber_answer_accepted ||
     adapter->peer_opening || adapter->media_negotiating || adapter->pending[1] != CLOUDFLARE_REALTIME_OP_NONE ||
     (adapter->joined_keyholder && !adapter->ccall->catalog_join_confirmed)) return 0;
 desired = media_desired(adapter);
 adds = jzon_alloc_array(); keep = jzon_alloc_array(); wire = jzon_alloc_array(); root = jzon_alloc_object();
 if (!desired || !adds || !keep || !wire || !root) { err = ENOMEM; goto out; }
 for (i = 0; adapter->media_current && i < json_object_array_length(adapter->media_current); ++i) {
  struct json_object *track = json_object_array_get_idx(adapter->media_current, i);
  if (contains_publication(desired, track)) json_object_array_add(keep, json_clone(track));
  else {
   struct json_object *item = jzon_alloc_object();
   jzon_add_str(item, "mid", "%s", jzon_str(track, "mid"));
   json_object_array_add(wire, item);
  }
 }
 if (json_object_array_length(wire)) {
  adapter->media_after_close = mem_deref(adapter->media_after_close);
  adapter->media_after_close = keep; keep = NULL;
  jzon_add_bool(root, "force", true);
  adapter->pending[1] = CLOUDFLARE_REALTIME_OP_CLOSE_SUBSCRIBER_TRACKS;
  re_snprintf(path, sizeof(path), "/sessions/%s/tracks/close", session_id(adapter, CCALL_ECALL_SUBSCRIBER));
 }
 else {
  for (i = 0; i < json_object_array_length(desired); ++i) {
   struct json_object *track = json_object_array_get_idx(desired, i), *item;
   if (contains_publication(adapter->media_current, track)) continue;
   json_object_array_add(adds, json_clone(track));
   item = jzon_alloc_object();
   jzon_add_str(item, "location", "remote");
   jzon_add_str(item, "sessionId", "%s", jzon_str(track, "sessionId"));
   jzon_add_str(item, "trackName", "%s", jzon_str(track, "trackName"));
   json_object_array_add(wire, item);
  }
  if (!json_object_array_length(wire)) goto out;
  adapter->media_pending = mem_deref(adapter->media_pending);
  adapter->media_pending = adds; adds = NULL;
  adapter->pending[1] = CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS;
  re_snprintf(path, sizeof(path), "/sessions/%s/tracks/new", session_id(adapter, CCALL_ECALL_SUBSCRIBER));
 }
 json_object_object_add(root, "tracks", wire); wire = NULL;
 err = jzon_encode(&body, root);
 if (!err) err = traced_request(adapter, CCALL_ECALL_SUBSCRIBER,
  adapter->pending[1] == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS ? "POST" : "PUT", path,
  adapter->app_id, adapter->app_secret, body, request_response, adapter->arg);
 if (err) adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
 info("cloudflare: media-subscribe operation=%d err=%d\n", adapter->pending[1], err);
out:
 mem_deref(desired); mem_deref(adds); mem_deref(keep); mem_deref(wire); mem_deref(root); mem_deref(body);
 return err;
}

static int request_streams(struct ccall *call, struct list *clients, enum icall_stream_mode mode, void *arg)
{
 struct le *le;
 struct list selected = LIST_INIT;
 (void)mode;
 if (!clients) return EINVAL;
 LIST_FOREACH(clients, le) {
  struct icall_client *client = le->data, *copy = icall_client_alloc(client->userid, client->clientid);
  if (!copy) { list_flush(&selected); return ENOMEM; }
  copy->quality = client->quality;
  list_append(&selected, &copy->le, copy);
 }
 list_flush(&call->videol);
 while (selected.head) {
  struct le *entry = selected.head;
  void *data = entry->data;
  list_unlink(entry);
  list_append(&call->videol, entry, data);
 }
 return media_progress(arg);
}

static int media_offer(struct cloudflare_realtime *adapter, struct json_object *root)
{
 struct json_object *results = NULL, *description = NULL, *bindings = jzon_alloc_array();
 struct ecall *ecall = ccall_get_ecall(adapter->ccall, CCALL_ECALL_SUBSCRIBER);
 struct econn_message msg;
 char *json = NULL;
 int i, j, err = bindings && ecall ? 0 : ENOMEM;
 if (!err) err = jzon_array(&results, root, "tracks");
 if (!err) err = jzon_object(&description, root, "sessionDescription");
 if (!err && (!streq(jzon_str(description, "type"), "offer") || !str_isset(jzon_str(description, "sdp")))) err = EPROTO;
 for (i = 0; !err && i < json_object_array_length(adapter->media_pending); ++i) {
  struct json_object *track = json_object_array_get_idx(adapter->media_pending, i), *result = NULL;
  struct userinfo *user;
  for (j = 0; j < json_object_array_length(results); ++j) {
   struct json_object *item = json_object_array_get_idx(results, j);
   if (same_publication(item, track)) { result = item; break; }
  }
  if (!result || jzon_str(result, "errorCode") || !str_isset(jzon_str(result, "mid"))) { err = EPROTO; break; }
  jzon_add_str(track, "mid", "%s", jzon_str(result, "mid"));
  user = userlist_find_by_real(adapter->ccall->userl, jzon_str(track, "userId"), jzon_str(track, "clientId"));
  if (!user || !user->se_approved) { err = EACCES; break; }
  err = ecall_add_decoders_for_user(ecall, user->userid_real, user->clientid_real, user->userid_hash, 0, 0);
 }
 for (i = 0; !err && adapter->media_current && i < json_object_array_length(adapter->media_current); ++i)
  err = json_object_array_add(bindings, json_clone(json_object_array_get_idx(adapter->media_current, i)));
 for (i = 0; !err && i < json_object_array_length(adapter->media_pending); ++i)
  err = json_object_array_add(bindings, json_clone(json_object_array_get_idx(adapter->media_pending, i)));
 if (!err) err = jzon_encode(&json, bindings);
 if (!err && ecall->flow->set_receive_tracks) err = ecall->flow->set_receive_tracks(ecall->flow, json);
 if (!err) err = econn_message_init(&msg, ECONN_UPDATE, "cloudflare");
 if (!err) {
  err = str_dup(&msg.u.setup.sdp_msg, jzon_str(description, "sdp"));
  adapter->media_negotiating = true;
  info("cloudflare: media-offer tracks=%u existing=%u\n", (unsigned)json_object_array_length(adapter->media_pending), adapter->media_current ? (unsigned)json_object_array_length(adapter->media_current) : 0);
  adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
  if (!err) err = ecall_msg_recv(ecall, 0, 0, "cloudflare", "realtime", &msg);
  econn_message_reset(&msg);
 }
 mem_deref(json); mem_deref(bindings);
 return err;
}

static void catalog_message(struct ccall *ccall, enum ccall_ecall_role role,
                            const struct econn_message *msg, void *arg)
{
	struct cloudflare_realtime *adapter = arg;
	struct json_object *root = NULL;
	const char *type;
	int err;
	if (!ccall->catalog_complete) return;
	if (catalog_request_received(adapter, msg)) return;
	if (role == CCALL_ECALL_SUBSCRIBER && !adapter->joined_keyholder) {
		struct cloudflare_peer_channel *peer = peer_sender(adapter, msg);
		if (!peer) return;
		uint32_t before = catalog_participant_revision(ccall->catalog, peer->participant_id);
		if (msg->msg_type == ECONN_CONF_CATALOG && msg->u.catalog.json)
			err = catalog_apply_peer_snapshot(ccall->catalog, msg->u.catalog.json,
				peer->participant_id, peer->session_id, ccall_catalog_participant_approved, ccall);
		else if (msg->msg_type == ECONN_CATALOG_SYNC)
			err = catalog_apply_peer_sync(ccall->catalog, peer->participant_id, peer->session_id,
				msg->u.catalogsync.revision, msg->u.catalogsync.tracks, ccall_catalog_participant_approved, ccall);
		else return;
		if (!err && catalog_participant_revision(ccall->catalog, peer->participant_id) == before) return;
		if (!err) peer_refresh_user(adapter, peer);
		if (!err) err = ccall_catalog_refresh(ccall);
		if (!err) err = send_catalog(adapter, CCALL_ECALL_PUBLISHER, false);
		if (!err) err = media_progress(adapter);
		info("cloudflare: peer-catalog update participant=%s err=%d\n", peer->participant_id, err);
		ICALL_CALL_CB(ccall->icall, group_changedh, &ccall->icall, ccall->icall.arg);
		return;
	}
	if (role == CCALL_ECALL_SUBSCRIBER && adapter->joined_keyholder &&
	    ccall->catalog_join_confirmed && msg->msg_type == ECONN_CATALOG_SYNC) {
		struct userinfo *user = userlist_find_by_hash(ccall->userl, msg->src_userid, msg->src_clientid);
		char *pid = NULL, *entry_json = NULL;
		struct json_object *entry = NULL;
		if (!user || !user->se_approved) return;
		err = re_sdprintf(&pid, "%s.%s", user->userid_real, user->clientid_real);
		if (!err) err = catalog_get_participant(ccall->catalog, pid, &entry_json);
		if (!err) err = jzon_decode(&entry, entry_json, strlen(entry_json));
		/* A subscriber receives direct sync only from its known keyholder.
		 * Updates from other publishers arrive in the keyholder's snapshot. */
		if (!err && !streq(jzon_str(entry, "publisherSessionId"), adapter->joined_keyholder)) err = EACCES;
		uint32_t before = catalog_participant_revision(ccall->catalog, pid);
		if (!err) err = catalog_apply_peer_sync(ccall->catalog, pid, adapter->joined_keyholder,
			msg->u.catalogsync.revision, msg->u.catalogsync.tracks, ccall_catalog_participant_approved, ccall);
		bool changed = !err && catalog_participant_revision(ccall->catalog, pid) != before;
		mem_deref(pid); mem_deref(entry_json); mem_deref(entry);
		if (changed) {
			err = ccall_catalog_refresh(ccall);
			if (!err) catalog_refresh_participants(adapter);
			if (!err) err = media_progress(adapter);
			if (!ccall->transport_ending)
				ICALL_CALL_CB(ccall->icall, group_changedh, &ccall->icall, ccall->icall.arg);
		}
		info("cloudflare: keyholder-media-sync revision=%u changed=%d err=%d\n",
		     msg->u.catalogsync.revision, changed, err);
		return;
	}
	if (msg->msg_type != ECONN_CONF_CATALOG || !msg->u.catalog.json) return;
	err = jzon_decode(&root, msg->u.catalog.json, strlen(msg->u.catalog.json));
	if (err) return;
	type = jzon_str(root, "type");
	if (role == CCALL_ECALL_PUBLISHER && streq(type, "catalog_hello") && !ccall->keyholder_session) {
		err = catalog_apply_hello(ccall->catalog, msg->u.catalog.json, ccall_catalog_participant_approved, ccall);
		if (!err) err = ccall_catalog_refresh(ccall);
		if (!err) err = send_catalog(adapter, CCALL_ECALL_PUBLISHER, false);
		if (!err) err = peer_enqueue(adapter, root);
	}
	else if (role == CCALL_ECALL_SUBSCRIBER && streq(type, "conf_catalog")) {
		bool confirmed = false;
		err = catalog_apply_snapshot(ccall->catalog, msg->u.catalog.json,
		                             ccall_catalog_participant_approved, ccall, &confirmed);
		if (!err) err = ccall_catalog_refresh(ccall);
		if (!err && confirmed) {
			ccall->catalog_join_confirmed = true;
			media_progress(adapter);
			tmr_cancel(&adapter->join_timer);
			info("cloudflare: catalog-join confirmed participant=%s revision=%u\n", ccall->participant_id, ccall->catalog_revision);
			ccall_set_transport_active(ccall, CCALL_ECALL_PUBLISHER);
		}
		if (!err && ccall->catalog_join_confirmed) {
			catalog_refresh_participants(adapter);
			if (!ccall->transport_ending && ccall->state == CCALL_STATE_ACTIVE)
				ICALL_CALL_CB(ccall->icall, group_changedh, &ccall->icall, ccall->icall.arg);
		}
	}
	if (err) warning("cloudflare: catalog receive failed: %m\n", err);
	mem_deref(root);
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

	if (role == CCALL_ECALL_SUBSCRIBER && msg->resp)
		operation = CLOUDFLARE_REALTIME_OP_RENEGOTIATE;
	if (role == CCALL_ECALL_SUBSCRIBER) {
		tracks_json = msg->resp ? NULL : adapter->tracks_json;
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

	if (event == CCALL_TRANSPORT_UPDATE_APPLIED && role == CCALL_ECALL_SUBSCRIBER && state->error) {
		ccall_pubsub_recovery_failed(ccall, state->error);
		return;
	}
	if (event == CCALL_TRANSPORT_CATALOG_READY)
		media_progress(adapter);

	if (event == CCALL_TRANSPORT_CLOSED) {
		tmr_cancel(&adapter->join_timer);
		adapter->peer_opening = NULL;
		list_flush(&adapter->peer_channels);
		adapter->media_current = mem_deref(adapter->media_current);
		adapter->media_pending = mem_deref(adapter->media_pending);
		adapter->media_after_close = mem_deref(adapter->media_after_close);
		adapter->media_negotiating = false;
		{ struct le *le; LIST_FOREACH(&adapter->ccall->userl->users, le) {
			struct userinfo *u = le->data; u->audio_subscribed = u->video_subscribed = false;
		} }
		adapter->joined_keyholder = mem_deref(adapter->joined_keyholder);
		adapter->subscriber_channel_registered = false;
		adapter->subscriber_negotiated = false;
		adapter->subscriber_answer_accepted = false;
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
	else if (event == CCALL_TRANSPORT_OFFER_RECEIVED && role == CCALL_ECALL_SUBSCRIBER) {
		ecall_answer(ccall_get_ecall(ccall, role), ccall->call_type, false);
	}
	else if (event == CCALL_TRANSPORT_MEDIA_READY && role == CCALL_ECALL_SUBSCRIBER &&
	         adapter->subscriber_answer_accepted && !adapter->subscriber_channel_registered &&
	         adapter->pending[role] == CLOUDFLARE_REALTIME_OP_NONE) {
		if (adapter->joined_keyholder) channel_request(adapter, false);
		else peer_progress(adapter);
	}
	else if (event == CCALL_TRANSPORT_DATA_READY && role == CCALL_ECALL_SUBSCRIBER) {
		if (adapter->peer_opening) peer_opened(adapter);
		else hello_send(adapter);
	}
	else if (event == CCALL_TRANSPORT_CATALOG_READY ||
	         (event == CCALL_TRANSPORT_MEDIA_READY && role == CCALL_ECALL_PUBLISHER)) {
		join_progress(adapter);
	}
	else if (event == CCALL_TRANSPORT_DATA_READY &&
		 role == CCALL_ECALL_PUBLISHER) {
		int err = cloudflare_realtime_send_datachannel(adapter);
		if (err && err != EALREADY) {
			warning("cloudflare realtime: data channel request failed: %m\n", err);
			if (ccall->recovery_in_progress)
				ccall_pubsub_recovery_failed(ccall, err);
		}
		else join_progress(adapter);
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
		.streamsh = request_streams,
		.confmsgh = catalog_message,
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
	int err = realtime_bridge_set_session(adapter->bridge, role, session_id, adapter->api_base);
	/* Keep the legacy signaling envelope's required URL field populated;
	 * the actual rendezvous locator lives in the embedded catalog. */
	if (!err && role == CCALL_ECALL_PUBLISHER && !adapter->ccall->primary_sft_url)
		err = str_dup(&adapter->ccall->primary_sft_url, adapter->api_base);
	return err;
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
	enum cloudflare_realtime_operation response_op = adapter && valid_role(role)
		? adapter->pending[role] : CLOUDFLARE_REALTIME_OP_NONE;

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
	/* This single-channel operation reports allocation failure explicitly.
	 * Retry the failed item, not a successful or ambiguous allocation. */
	if (adapter && role == CCALL_ECALL_SUBSCRIBER && status >= 200 && status < 300 &&
	    adapter->pending[role] == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_DATACHANNEL && body) {
		struct json_object *probe = NULL, *channels = NULL;
		if (!jzon_decode(&probe, body, strlen(body)) &&
		    !jzon_array(&channels, probe, "dataChannels") && json_object_array_length(channels) == 1) {
			struct json_object *channel = json_object_array_get_idx(channels, 0), *id = NULL;
			const char *code = jzon_str(channel, "errorCode");
			if (code) warning("cloudflare: channel-subscribe item error=%s description=%s\n",
			                  code, jzon_str(channel, "errorDescription") ? jzon_str(channel, "errorDescription") : "(none)");
			if (streq(code, "transport_unavailable_error") &&
			    !json_object_object_get_ex(channel, "id", &id) &&
			    adapter->ccall->transport[role].state.media_ready &&
			    adapter->retry[role].path && adapter->retry[role].attempt < 3) {
				unsigned delay = 500 * adapter->retry[role].attempt;
				info("cloudflare(%p): retry-scheduled id=%u role=%d next-attempt=%u/3 delay=%u reason=transport_unavailable_error\n",
				     adapter, adapter->request_id[role], role, adapter->retry[role].attempt + 1, delay);
				tmr_start(&adapter->retry[role].timer, delay, retry_timeout, &adapter->retry[role]);
				mem_deref(probe);
				return 0;
			}
		}
		mem_deref(probe);
	}
	if (adapter && valid_role(role))
		retry_clear(&adapter->retry[role]);

	if (status < 200 || status >= 300) {
		if (adapter && role == CCALL_ECALL_SUBSCRIBER &&
		    adapter->pending[role] == CLOUDFLARE_REALTIME_OP_GRANT_REPLY) {
			adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
			tmr_start(&adapter->join_timer, 1000 + rand_u16() % 1000, join_timeout, adapter);
		}
		warning("cloudflare: response status=%d body=%s\n",
			status, body ? body : "(empty)");
		if (adapter && role == CCALL_ECALL_SUBSCRIBER &&
		    (response_op == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS ||
		     response_op == CLOUDFLARE_REALTIME_OP_CLOSE_SUBSCRIBER_TRACKS || adapter->media_negotiating))
			ccall_pubsub_recovery_failed(adapter->ccall, EPROTO);
		else if (adapter && adapter->ccall->recovery_in_progress)
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

		if (!err) adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		if (!err && role == CCALL_ECALL_SUBSCRIBER) {
			char path[256];
			re_snprintf(path, sizeof(path), "/sessions/%s/datachannels/establish", session_id(adapter, role));
			adapter->pending[role] = CLOUDFLARE_REALTIME_OP_ESTABLISH_DATACHANNEL;
			err = traced_request(adapter, role, "POST", path, adapter->app_id, adapter->app_secret,
				"{\"dataChannel\":{\"location\":\"remote\",\"dataChannelName\":\"server-events\"}}", request_response, adapter->arg);
		}
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
	/* The application channel must report its own Open, independently of
	 * the provisional channel used to negotiate SCTP. */
	adapter->ccall->transport[CCALL_ECALL_PUBLISHER].state.data_ready = false;
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
		if (!err) err = join_progress(adapter);
		goto out;
	}



 if (role == CCALL_ECALL_SUBSCRIBER && response_op == CLOUDFLARE_REALTIME_OP_CLOSE_SUBSCRIBER_TRACKS) {
  struct json_object *results = NULL;
  int i;
  err = jzon_array(&results, root, "tracks");
  if (!err && !json_object_array_length(results)) err = EPROTO;
  for (i = 0; !err && i < json_object_array_length(results); ++i)
   if (jzon_str(json_object_array_get_idx(results, i), "errorCode")) err = EPROTO;
  for (i = 0; !err && i < json_object_array_length(adapter->media_current); ++i) {
   struct json_object *track = json_object_array_get_idx(adapter->media_current, i);
   int j; bool found = false;
   if (contains_publication(adapter->media_after_close, track)) continue;
   for (j = 0; j < json_object_array_length(results); ++j)
    if (streq(jzon_str(track, "mid"), jzon_str(json_object_array_get_idx(results, j), "mid"))) found = true;
   if (!found) err = EPROTO;
  }
  if (!err) {
   adapter->media_current = mem_deref(adapter->media_current);
   adapter->media_current = adapter->media_after_close; adapter->media_after_close = NULL;
   err = media_commit_bindings(adapter);
   info("cloudflare: media-close completed remaining=%u err=%d\n", (unsigned)json_object_array_length(adapter->media_current), err);
   adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
   if (!err) err = media_progress(adapter);
  }
  goto out;
 }
 if (role == CCALL_ECALL_SUBSCRIBER && response_op == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS && adapter->media_pending) {
  err = media_offer(adapter, root);
  goto out;
 }
 if (role == CCALL_ECALL_SUBSCRIBER && response_op == CLOUDFLARE_REALTIME_OP_RENEGOTIATE && adapter->media_negotiating) {
  int i;
  if (!adapter->media_current) adapter->media_current = jzon_alloc_array();
  for (i = 0; !err && i < json_object_array_length(adapter->media_pending); ++i)
   err = json_object_array_add(adapter->media_current, json_clone(json_object_array_get_idx(adapter->media_pending, i)));
  adapter->media_pending = mem_deref(adapter->media_pending);
  adapter->media_negotiating = false;
  if (!err) err = media_commit_bindings(adapter);
  info("cloudflare: media-answer accepted subscribed=%u err=%d\n", (unsigned)json_object_array_length(adapter->media_current), err);
  adapter->pending[1] = CLOUDFLARE_REALTIME_OP_NONE;
  if (!err) err = peer_progress(adapter);
  if (!err) err = media_progress(adapter);
  goto out;
 }
	if (role == CCALL_ECALL_SUBSCRIBER && adapter->subscriber_negotiated &&
	    adapter->pending[role] == CLOUDFLARE_REALTIME_OP_RENEGOTIATE) {
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		adapter->subscriber_answer_accepted = true;
		if (adapter->ccall->transport[role].state.media_ready)
			err = adapter->joined_keyholder ? channel_request(adapter, false) : peer_progress(adapter);
		goto out;
	}
	if (role == CCALL_ECALL_SUBSCRIBER &&
	    (adapter->pending[role] == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_DATACHANNEL ||
	     adapter->pending[role] == CLOUDFLARE_REALTIME_OP_GRANT_REPLY)) {
		struct json_object *channels = NULL, *channel;
		bool grant = adapter->pending[role] == CLOUDFLARE_REALTIME_OP_GRANT_REPLY;
		int id;
		err = jzon_array(&channels, root, "dataChannels");
		if (!err && json_object_array_length(channels) != 1) err = EPROTO;
		channel = !err ? json_object_array_get_idx(channels, 0) : NULL;
		if (!err && jzon_str(channel, "errorCode")) err = EPROTO;
		if (!err && !grant) err = jzon_int(&id, channel, "id");
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
		if (!err && !grant) {
			adapter->subscriber_channel_registered = true;
			/* Readiness of the reserved transport channel does not mean
			 * the new application channel is open. */
			adapter->ccall->transport[role].state.data_ready = false;
			char label[128];
			re_snprintf(label, sizeof(label), adapter->peer_opening ? "calling-3.0/%s" : "calling-3.0",
			            adapter->peer_opening ? adapter->peer_opening->session_id : "");
			err = ecall_dce_create(ccall_get_ecall(adapter->ccall, role), label, id);
		}
		if (!err && !adapter->peer_opening) hello_send(adapter);
		else if (grant) tmr_start(&adapter->join_timer, 1000 + rand_u16() % 1000, join_timeout, adapter);
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
	if (role == CCALL_ECALL_SUBSCRIBER && adapter->pending[role] == CLOUDFLARE_REALTIME_OP_ESTABLISH_DATACHANNEL) {
		adapter->subscriber_negotiated = true;
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
	}
	err = str_dup(&msg.u.setup.sdp_msg, sdp);
	if (!err)
		err = ecall_msg_recv(ecall, 0, 0, "cloudflare", "realtime", &msg);
	econn_message_reset(&msg);
	if (!adapter->awaiting_update_apply && role == CCALL_ECALL_PUBLISHER)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;

out:
	if (err && role == CCALL_ECALL_SUBSCRIBER &&
	    adapter->pending[role] == CLOUDFLARE_REALTIME_OP_GRANT_REPLY)
		tmr_start(&adapter->join_timer, 1000 + rand_u16() % 1000, join_timeout, adapter);
	if (err)
		adapter->pending[role] = CLOUDFLARE_REALTIME_OP_NONE;
	mem_deref(description);
	mem_deref(root);
	if (err && role == CCALL_ECALL_SUBSCRIBER &&
	    (response_op == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS ||
	     response_op == CLOUDFLARE_REALTIME_OP_CLOSE_SUBSCRIBER_TRACKS ||
	     response_op == CLOUDFLARE_REALTIME_OP_SUBSCRIBE_DATACHANNEL ||
	     response_op == CLOUDFLARE_REALTIME_OP_ESTABLISH_DATACHANNEL ||
	     response_op == CLOUDFLARE_REALTIME_OP_RENEGOTIATE)) {
		ccall_pubsub_recovery_failed(adapter->ccall, err);
		return err;
	}
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

	tmr_cancel(&adapter->join_timer);
	adapter->peer_opening = NULL;
	list_flush(&adapter->peer_channels);
	adapter->media_current = mem_deref(adapter->media_current);
	adapter->media_pending = mem_deref(adapter->media_pending);
	adapter->media_after_close = mem_deref(adapter->media_after_close);
	adapter->media_negotiating = false;
	mem_deref(adapter->joined_keyholder);
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
