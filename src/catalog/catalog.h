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
#ifndef AVS_CATALOG_H
#define AVS_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct catalog;
struct json_object;

/* Shared Track-Locator and media-attribute schema for ConfCatalog/CatalogSync. */
int catalog_tracks_validate(struct json_object *tracks);
int catalog_tracks_from_publisher(char **jsonp, const char *session_id,
		const char *publisher_tracks, bool muted, const char *video_source);

enum catalog_message_type {
	CATALOG_MESSAGE_HELLO,
	CATALOG_MESSAGE_WELCOME,
	CATALOG_MESSAGE_UPDATE,
	CATALOG_MESSAGE_REQUEST,
};

int catalog_alloc(struct catalog **catalogp, const char *catalog_id, const char *participant_id);

int catalog_set_publication(struct catalog *catalog,
				const char *publication_json,
				uint32_t version,
				const char *state);

int catalog_encode_hello(const struct catalog *catalog, const char *request_id, char **jsonp);
int catalog_encode_welcome(const struct catalog *catalog, const char *request_id, char **jsonp);
int catalog_encode_update(const struct catalog *catalog, char **jsonp);
int catalog_encode_snapshot(const struct catalog *catalog, char **jsonp);

int catalog_apply_message(struct catalog *catalog, const char *json, size_t len);

const char *catalog_id(const struct catalog *catalog);
const char *catalog_participant_id(const struct catalog *catalog);
uint32_t catalog_version(const struct catalog *catalog);

/* All stored participants, including the local publication, must match. */
typedef bool (catalog_participant_match_h)(const char *userid,
					 const char *clientid, void *arg);
bool catalog_participants_match(const struct catalog *catalog,
				catalog_participant_match_h *matchh, void *arg);

int catalog_get_participant(const struct catalog *catalog, const char *participant_id, char **jsonp);
uint32_t catalog_participant_revision(const struct catalog *catalog, const char *participant_id);
int catalog_apply_peer_snapshot(struct catalog *catalog, const char *json,
	const char *participant_id, const char *session_id,
	catalog_participant_match_h *approved, void *arg);
int catalog_apply_peer_sync(struct catalog *catalog, const char *participant_id,
	const char *session_id, uint32_t revision, struct json_object *tracks,
	catalog_participant_match_h *approved, void *arg);
int catalog_find_session(const char *json, const char *userid, const char *clientid, char **sessionp);
int catalog_apply_snapshot(struct catalog *catalog, const char *json,
	catalog_participant_match_h *approved, void *arg, bool *self_confirmed);
int catalog_apply_hello(struct catalog *catalog, const char *json,
	catalog_participant_match_h *approved, void *arg);

void catalog_close(struct catalog *catalog);

#endif
