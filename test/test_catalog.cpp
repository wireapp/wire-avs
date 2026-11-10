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
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include "../src/catalog/catalog.h"

#define CATALOG_ID "conv_hash"


static struct catalog_entry *make_entry(const char *userid,
					const char *session,
					uint64_t joined, uint32_t version)
{
	struct catalog_entry *e = NULL;

	EXPECT_EQ(catalog_entry_alloc(&e, userid, "_"), 0);
	EXPECT_EQ(catalog_entry_set_session(e, session, "audio", "video",
					    "catalog"), 0);
	e->joined = joined;
	e->version = version;

	return e;
}


TEST(catalog, encode_decode)
{
	struct catalog_entry *e, *d = NULL;
	enum catalog_msg_type type;
	char *json = NULL;

	e = make_entry("alice", "sess-a", 1700000000123ULL, 3);
	e->keysync = 42;
	e->vstate = ICALL_VIDEO_STATE_STARTED;
	e->muted = true;

	ASSERT_EQ(catalog_encode(&json, CATALOG_MSG_HELLO, CATALOG_ID, e), 0);
	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, json, strlen(json)), 0);

	ASSERT_EQ(type, CATALOG_MSG_HELLO);
	ASSERT_STREQ(d->userid, "alice");
	ASSERT_STREQ(d->clientid, "_");
	ASSERT_STREQ(d->session_id, "sess-a");
	ASSERT_STREQ(d->audio_track, "audio");
	ASSERT_STREQ(d->video_track, "video");
	ASSERT_STREQ(d->dc_name, "catalog");
	ASSERT_EQ(d->joined, 1700000000123ULL);
	ASSERT_EQ(d->version, 3u);
	ASSERT_EQ(d->keysync, 42u);
	ASSERT_EQ(d->vstate, ICALL_VIDEO_STATE_STARTED);
	ASSERT_TRUE(d->muted);

	mem_deref(json);
	mem_deref(d);
	mem_deref(e);
}


TEST(catalog, decode_errors)
{
	struct catalog_entry *e, *d = NULL;
	enum catalog_msg_type type;
	char *json = NULL;
	const char *bad_type =
		"{\"type\":\"nope\",\"catalogId\":\"conv_hash\","
		"\"userid\":\"u\",\"clientid\":\"_\",\"joined\":\"1\","
		"\"version\":0,\"sessionId\":\"s\"}";
	const char *no_session =
		"{\"type\":\"update\",\"catalogId\":\"conv_hash\","
		"\"userid\":\"u\",\"clientid\":\"_\",\"joined\":\"1\","
		"\"version\":0}";

	e = make_entry("alice", "sess-a", 1, 0);
	ASSERT_EQ(catalog_encode(&json, CATALOG_MSG_UPDATE, "other", e), 0);
	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, json, strlen(json)),
		  EBADMSG);

	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, "{x", 2), EPROTO);
	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, bad_type,
				 strlen(bad_type)), EPROTO);
	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, no_session,
				 strlen(no_session)), EPROTO);
	ASSERT_TRUE(d == NULL);

	mem_deref(json);
	mem_deref(e);
}


TEST(catalog, bye_without_session)
{
	struct catalog_entry *e, *d = NULL;
	enum catalog_msg_type type;
	char *json = NULL;

	e = make_entry("alice", "sess-a", 5, 1);
	ASSERT_EQ(catalog_encode(&json, CATALOG_MSG_BYE, CATALOG_ID, e), 0);
	ASSERT_TRUE(strstr(json, "sess-a") == NULL);
	ASSERT_EQ(catalog_decode(&type, &d, CATALOG_ID, json, strlen(json)), 0);
	ASSERT_EQ(type, CATALOG_MSG_BYE);
	ASSERT_EQ(d->joined, 5u);

	mem_deref(json);
	mem_deref(d);
	mem_deref(e);
}


TEST(catalog, apply_ordering)
{
	struct catalog *cat = NULL;
	struct catalog_entry *e, *stored;
	bool changed;

	ASSERT_EQ(catalog_alloc(&cat, CATALOG_ID, "self", "_"), 0);

	/* own entry is ignored */
	e = make_entry("self", "sess-self", 1, 1);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_HELLO, e, &changed, NULL), 0);
	ASSERT_FALSE(changed);
	ASSERT_EQ(list_count(catalog_peers(cat)), 0u);
	mem_deref(e);

	e = make_entry("alice", "sess-a", 100, 2);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_HELLO, e, &changed, &stored), 0);
	ASSERT_TRUE(changed);
	ASSERT_TRUE(stored != NULL);
	ASSERT_EQ(list_count(catalog_peers(cat)), 1u);
	mem_deref(e);

	/* same or older version of the same join: ignored */
	e = make_entry("alice", "sess-old", 100, 2);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_UPDATE, e, &changed, NULL), 0);
	ASSERT_FALSE(changed);
	mem_deref(e);

	/* newer version */
	e = make_entry("alice", "sess-a", 100, 3);
	e->keysync = 7;
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_UPDATE, e, &changed, &stored), 0);
	ASSERT_TRUE(changed);
	ASSERT_EQ(stored->keysync, 7u);
	mem_deref(e);

	/* rejoin with lower version but later join time wins */
	e = make_entry("alice", "sess-b", 200, 0);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_HELLO, e, &changed, &stored), 0);
	ASSERT_TRUE(changed);
	ASSERT_STREQ(stored->session_id, "sess-b");
	mem_deref(e);

	/* bye of the earlier join does not remove the rejoin */
	e = make_entry("alice", NULL, 100, 9);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_BYE, e, &changed, NULL), 0);
	ASSERT_FALSE(changed);
	ASSERT_EQ(list_count(catalog_peers(cat)), 1u);
	mem_deref(e);

	e = make_entry("alice", NULL, 200, 1);
	ASSERT_EQ(catalog_apply(cat, CATALOG_MSG_BYE, e, &changed, NULL), 0);
	ASSERT_TRUE(changed);
	ASSERT_EQ(list_count(catalog_peers(cat)), 0u);
	ASSERT_TRUE(catalog_find(cat, "alice", "_") == NULL);
	mem_deref(e);

	mem_deref(cat);
}


TEST(catalog, self_version)
{
	struct catalog *cat = NULL;

	ASSERT_EQ(catalog_alloc(&cat, CATALOG_ID, "self", "_"), 0);
	ASSERT_EQ(catalog_self(cat)->version, 0u);
	catalog_self_changed(cat);
	catalog_self_changed(cat);
	ASSERT_EQ(catalog_self(cat)->version, 2u);
	ASSERT_STREQ(catalog_id(cat), CATALOG_ID);

	mem_deref(cat);
}
