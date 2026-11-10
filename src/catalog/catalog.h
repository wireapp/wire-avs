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

/*
 * catalog -- participant catalog of a publish/subscribe conference
 *
 * Every participant publishes one entry describing its publisher session
 * (session id, track names, catalog data channel) and its call state
 * (keysync epoch, video state, mute). Entries are exchanged as JSON:
 *
 *   hello    -- joining, sent over Wire messaging to the conversation
 *   welcome  -- answer to hello, sent over Wire to the joiner
 *   update   -- entry changed, sent over our catalog data channel
 *   bye      -- leaving, sent over our catalog data channel
 *
 * User and client ids are the per-call hashed ids, so the content seen
 * by the media backend does not reveal real ids.
 */

#ifndef AVS_CATALOG_H
#define AVS_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct catalog;

enum catalog_msg_type {
	CATALOG_MSG_HELLO = 0,
	CATALOG_MSG_WELCOME,
	CATALOG_MSG_UPDATE,
	CATALOG_MSG_BYE,
};

struct catalog_entry {
	struct le le;

	char *userid;        /* hashed */
	char *clientid;      /* hashed */

	char *session_id;    /* publisher session */
	char *audio_track;   /* NULL if not published */
	char *video_track;   /* NULL if not published */
	char *dc_name;       /* catalog data channel */

	uint64_t joined;     /* join time (ms), orders rejoins */
	uint32_t version;    /* increases with every change of this entry */
	uint32_t keysync;    /* latest MLS epoch */
	int vstate;          /* enum icall_vstate */
	bool muted;

	void *arg;           /* owner data */
};

int  catalog_entry_alloc(struct catalog_entry **ep,
			 const char *userid,
			 const char *clientid);
int  catalog_entry_set_session(struct catalog_entry *e,
			       const char *session_id,
			       const char *audio_track,
			       const char *video_track,
			       const char *dc_name);

int  catalog_encode(char **jsonp,
		    enum catalog_msg_type type,
		    const char *catalog_id,
		    const struct catalog_entry *e);

/* Returns EPROTO for malformed messages, EBADMSG for another catalog */
int  catalog_decode(enum catalog_msg_type *typep,
		    struct catalog_entry **ep,
		    const char *catalog_id,
		    const char *json,
		    size_t len);

const char *catalog_msg_name(enum catalog_msg_type type);


int  catalog_alloc(struct catalog **catp, const char *catalog_id,
		   const char *userid, const char *clientid);

const char *catalog_id(const struct catalog *cat);
struct catalog_entry *catalog_self(struct catalog *cat);

/* Bump our version after changing the self entry */
void catalog_self_changed(struct catalog *cat);

/* Apply a received entry. Newer joins replace older ones; within a join
 * only higher versions are taken. A BYE removes the entry.
 * *changedp is set if the catalog changed; *entryp (optional) points to
 * the stored entry (NULL after removal).
 */
int  catalog_apply(struct catalog *cat,
		   enum catalog_msg_type type,
		   const struct catalog_entry *e,
		   bool *changedp,
		   struct catalog_entry **entryp);

struct catalog_entry *catalog_find(const struct catalog *cat,
				   const char *userid,
				   const char *clientid);
void catalog_remove(struct catalog *cat, struct catalog_entry *e);
void catalog_flush(struct catalog *cat);

/* Remote entries (struct catalog_entry) */
struct list *catalog_peers(struct catalog *cat);

int  catalog_debug(struct re_printf *pf, const struct catalog *cat);

#ifdef __cplusplus
}
#endif

#endif
