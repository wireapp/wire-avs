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

#include "catalog.h"

struct catalog {
	char *catalog_id;
	char *participant_id;
	uint32_t version;
	char *state;
	struct json_object *publication;
	struct json_object *participants;
};

int catalog_tracks_validate(struct json_object *tracks)
{
	int i;
	if (!jzon_is_array(tracks))
		return EINVAL;
	for (i = 0; i < json_object_array_length(tracks); ++i) {
		struct json_object *track = json_object_array_get_idx(tracks, i);
		const char *kind = jzon_str(track, "kind");
		bool muted;
		if (!str_isset(jzon_str(track, "sessionId")) ||
		    !str_isset(jzon_str(track, "trackName")) || !kind)
			return EINVAL;
		if (!strcmp(kind, "audio")) {
			if (jzon_bool(&muted, track, "muted"))
				return EINVAL;
		}
		else if (!strcmp(kind, "video")) {
			const char *source = jzon_str(track, "source");
			if (!source || (strcmp(source, "camera") && strcmp(source, "screen")))
				return EINVAL;
		}
		else
			return EINVAL;
	}
	return 0;
}

int catalog_tracks_from_publisher(char **jsonp, const char *session_id,
		const char *publisher_tracks, bool muted, const char *video_source)
{
	struct json_object *input = NULL, *tracks = NULL;
	int err, i;
	if (!jsonp || !str_isset(session_id) || !publisher_tracks || !video_source)
		return EINVAL;
	err = jzon_decode(&input, publisher_tracks, strlen(publisher_tracks));
	if (err)
		return err;
	if (!jzon_is_array(input)) {
		err = EINVAL;
		goto out;
	}
	tracks = jzon_alloc_array();
	if (!tracks) {
		err = ENOMEM;
		goto out;
	}
	for (i = 0; i < json_object_array_length(input); ++i) {
		struct json_object *item = json_object_array_get_idx(input, i);
		struct json_object *track;
		const char *kind = jzon_str(item, "kind");
		const char *name = jzon_str(item, "trackName");
		if (!kind || !str_isset(name)) {
			err = EINVAL;
			goto out;
		}
		track = jzon_alloc_object();
		if (!track) {
			err = ENOMEM;
			goto out;
		}
		err = jzon_add_str(track, "sessionId", "%s", session_id);
		err |= jzon_add_str(track, "trackName", "%s", name);
		err |= jzon_add_str(track, "kind", "%s", kind);
		if (!strcmp(kind, "audio"))
			err |= jzon_add_bool(track, "muted", muted);
		else if (!strcmp(kind, "video"))
			err |= jzon_add_str(track, "source", "%s", video_source);
		if (!err)
			err = json_object_array_add(tracks, track);
		if (err) {
			mem_deref(track);
			goto out;
		}
	}
	err = catalog_tracks_validate(tracks);
	if (!err)
		err = jzon_encode(jsonp, tracks);
out:
	mem_deref(input);
	mem_deref(tracks);
	return err;
}

static int make_entry(const struct catalog *catalog, struct json_object **entryp)
{
	struct json_object *entry;
	char *encoded = NULL;
	int err;

	if (!catalog || !entryp || !catalog->publication)
		return EINVAL;

	err = jzon_encode(&encoded, catalog->publication);
	if (err)
		return err;
	err = jzon_decode(&entry, encoded, strlen(encoded));
	mem_deref(encoded);
	if (err)
		return err;

	jzon_add_str(entry, "participantId", "%s", catalog->participant_id);
	jzon_add_int(entry, "revision", (int32_t)catalog->version);
	jzon_add_str(entry, "state", "%s", catalog->state);
	*entryp = entry;
	return 0;
}

static int encode_message(struct catalog *catalog, const char *type, const char *request_id, char **jsonp)
{
	struct json_object *message = NULL;
	struct json_object *entry = NULL;
	int err;

	if (!catalog || !type || !jsonp)
		return EINVAL;

	message = jzon_alloc_object();
	if (!message)
		return ENOMEM;
	jzon_add_str(message, "type", "%s", type);
	jzon_add_str(message, "catalogId", "%s", catalog->catalog_id);
	if (request_id)
		jzon_add_str(message, "requestId", "%s", request_id);

	if (strcmp(type, "catalog_update") == 0 ||
	    strcmp(type, "catalog_welcome") == 0) {
		err = make_entry(catalog, &entry);
		if (err)
			goto out;
		json_object_object_add(message, "participant", entry);
		entry = NULL;
	}

	err = jzon_encode(jsonp, message);

out:
	mem_deref(entry);
	mem_deref(message);
	return err;
}

int catalog_alloc(struct catalog **catalogp,
			 const char *catalog_id,
			 const char *participant_id)
{
	struct catalog *catalog;
	int err;

	if (!catalogp || !catalog_id || !participant_id)
		return EINVAL;
	catalog = mem_zalloc(sizeof(*catalog), NULL);
	if (!catalog)
		return ENOMEM;
	err = str_dup(&catalog->catalog_id, catalog_id);
	if (!err)
		err = str_dup(&catalog->participant_id, participant_id);
	if (!err)
		err = str_dup(&catalog->state, "reachable");
	if (!err)
		catalog->participants = jzon_alloc_object();
	if (!catalog->participants)
		err = ENOMEM;
	if (err)
		goto out;
	*catalogp = catalog;
	return 0;
out:
	catalog_close(catalog);
	return err;
}

int catalog_set_publication(struct catalog *catalog,
				const char *publication_json,
				uint32_t version,
				const char *state)
{
	struct json_object *publication = NULL;
	int err;

	if (!catalog || !publication_json || !state)
		return EINVAL;
	err = jzon_decode(&publication, publication_json,
				 strlen(publication_json));
	if (err)
		return err;
	mem_deref(catalog->publication);
	catalog->publication = publication;
	catalog->version = version;
	{
		char *new_state = NULL;
		err = str_dup(&new_state, state);
		if (!err) {
			mem_deref(catalog->state);
			catalog->state = new_state;
		}
	}
	return err;
}

int catalog_encode_hello(const struct catalog *catalog, const char *request_id, char **jsonp)
{
	if (!catalog || !request_id)
		return EINVAL;
	return encode_message((struct catalog *)catalog, "catalog_hello",
				      request_id, jsonp);
}

int catalog_encode_update(const struct catalog *catalog, char **jsonp)
{
	return encode_message((struct catalog *)catalog, "catalog_update",
				      NULL, jsonp);
}

int catalog_encode_welcome(const struct catalog *catalog, const char *request_id, char **jsonp)
{
	if (!catalog || !request_id)
		return EINVAL;
	return encode_message((struct catalog *)catalog, "catalog_welcome",
				      request_id, jsonp);
}

struct snapshot_context {
	struct json_object *participants;
	int err;
};

static bool snapshot_participant(const char *key, struct json_object *entry,
				void *arg)
{
	struct snapshot_context *ctx = arg;
	(void)key;
	mem_ref(entry);
	ctx->err = json_object_array_add(ctx->participants, entry);
	if (ctx->err)
		mem_deref(entry);
	return ctx->err != 0;
}

int catalog_encode_snapshot(const struct catalog *catalog, char **jsonp)
{
	struct json_object *root;
	struct json_object *participants = NULL;
	struct snapshot_context ctx;
	int err;

	if (!catalog || !jsonp)
		return EINVAL;
	root = jzon_alloc_object();
	if (!root)
		return ENOMEM;
	jzon_add_str(root, "catalogId", "%s", catalog->catalog_id);
	jzon_add_str(root, "type", "%s", "conf_catalog");
	if (catalog->publication) {
		struct json_object *epoch = NULL;
		if (json_object_object_get_ex(catalog->publication, "mlsEpoch", &epoch))
			json_object_object_add(root, "mlsEpoch", mem_ref(epoch));
	}
	if (catalog->publication) {
		struct json_object *self = NULL;
		err = make_entry(catalog, &self);
		if (err)
			goto out;
		json_object_object_add(catalog->participants,
				       catalog->participant_id, self);
	}
	participants = jzon_alloc_array();
	if (!participants) {
		err = ENOMEM;
		goto out;
	}
	ctx.participants = participants;
	ctx.err = 0;
	jzon_apply(catalog->participants, snapshot_participant, &ctx);
	err = ctx.err;
	if (err)
		goto out;
	json_object_object_add(root, "participants", participants);
	participants = NULL;
	err = jzon_encode(jsonp, root);

out:
	mem_deref(participants);
	mem_deref(root);
	return err;
}

int catalog_apply_message(struct catalog *catalog,
				 const char *json, size_t len)
{
	struct json_object *message = NULL;
	struct json_object *entry = NULL;
	const char *type;
	const char *cid;
	const char *pid;
	int incoming_version;
	int current_version;
	char *encoded = NULL;
	int err;

	if (!catalog || !json)
		return EINVAL;
	err = jzon_decode(&message, json, len);
	if (err)
		goto out;
	cid = jzon_str(message, "catalogId");
	if (!cid || strcmp(cid, catalog->catalog_id) != 0)
		goto out;
	type = jzon_str(message, "type");
	if (!type || (strcmp(type, "catalog_update") != 0 &&
		      strcmp(type, "catalog_welcome") != 0))
		goto out;
	err = jzon_object(&entry, message, "participant");
	if (err)
		goto out;
	pid = jzon_str(entry, "participantId");
	if (!pid)
		goto out;
	if (strcmp(pid, catalog->participant_id) == 0)
		goto out;
	err = jzon_int_opt(&incoming_version, entry, "revision", -1);
	if (!err && incoming_version < 0)
		err = jzon_int_opt(&incoming_version, entry,
				   "publicationVersion", -1);
	if (err)
		goto out;
	if (incoming_version < 0)
		goto out;
	{
		struct json_object *old = NULL;
		current_version = -1;
		if (!jzon_object(&old, catalog->participants, pid)) {
			err = jzon_int_opt(&current_version, old,
					   "revision", -1);
			if (!err && current_version < 0)
				err = jzon_int_opt(&current_version, old,
						   "publicationVersion", -1);
			if (err)
				goto out;
		}
		if (incoming_version <= current_version)
			goto out;
	}
	err = jzon_encode(&encoded, entry);
	if (err)
		goto out;
	{
		struct json_object *copy = NULL;
		err = jzon_decode(&copy, encoded, strlen(encoded));
		if (!err)
			json_object_object_add(catalog->participants, pid, copy);
	}
out:
	mem_deref(encoded);
	mem_deref(message);
	return err;
}

struct participant_match_context {
	const struct catalog *catalog;
	catalog_participant_match_h *matchh;
	void *arg;
};

static bool participant_matches(struct json_object *entry,
                                struct participant_match_context *ctx)
{
	const char *userid = jzon_str(entry, "userId");
	const char *clientid = jzon_str(entry, "clientId");
	return userid && clientid && ctx->matchh(userid, clientid, ctx->arg);
}

static bool participant_mismatch(const char *key, struct json_object *entry,
                                 void *arg)
{
	struct participant_match_context *ctx = arg;
	/* The local publication is authoritative; a snapshot may contain an older
		* copy of it in participants. */
	if (!strcmp(key, ctx->catalog->participant_id))
		return false;
	return !participant_matches(entry, ctx);
}

bool catalog_participants_match(const struct catalog *catalog,
                               catalog_participant_match_h *matchh, void *arg)
{
	struct participant_match_context ctx = {catalog, matchh, arg};
	if (!catalog || !catalog->publication || !matchh)
		return false;
	if (!participant_matches(catalog->publication, &ctx))
		return false;
	return jzon_apply(catalog->participants, participant_mismatch, &ctx) == NULL;
}

const char *catalog_id(const struct catalog *catalog)
{
	return catalog ? catalog->catalog_id : NULL;
}

const char *catalog_participant_id(const struct catalog *catalog)
{
	return catalog ? catalog->participant_id : NULL;
}

uint32_t catalog_version(const struct catalog *catalog)
{
	return catalog ? catalog->version : 0;
}

void catalog_close(struct catalog *catalog)
{
	if (!catalog)
		return;
	mem_deref(catalog->catalog_id);
	mem_deref(catalog->participant_id);
	mem_deref(catalog->state);
	mem_deref(catalog->publication);
	mem_deref(catalog->participants);
	mem_deref(catalog);
}
