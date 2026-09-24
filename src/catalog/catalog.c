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
	jzon_add_int(entry, "publicationVersion", (int32_t)catalog->version);
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

int catalog_encode_snapshot(const struct catalog *catalog, char **jsonp)
{
	struct json_object *root;
	struct json_object *participants = NULL;
	char *encoded = NULL;
	int err;

	if (!catalog || !jsonp)
		return EINVAL;
	root = jzon_alloc_object();
	if (!root)
		return ENOMEM;
	jzon_add_str(root, "catalogId", "%s", catalog->catalog_id);
	err = jzon_encode(&encoded, catalog->participants);
	if (err)
		goto out;
	err = jzon_decode(&participants, encoded, strlen(encoded));
	if (err)
		goto out;
	json_object_object_add(root, "participants", participants);
	participants = NULL;
	err = jzon_encode(jsonp, root);

out:
	mem_deref(encoded);
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
	err = jzon_int_opt(&incoming_version, entry, "publicationVersion", -1);
	if (err)
		goto out;
	if (incoming_version < 0)
		goto out;
	{
		struct json_object *old = NULL;
		current_version = -1;
		if (!jzon_object(&old, catalog->participants, pid)) {
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
