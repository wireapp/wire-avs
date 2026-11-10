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

void catalog_close(struct catalog *catalog);

#endif
