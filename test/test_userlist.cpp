/*
* Wire
* Copyright (C) 2020 Wire Swiss GmbH
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
#include "avs_wcall.h"
extern "C" {
#include "avs_audio_level.h"
};
#include <gtest/gtest.h>

#define KEYSZ (32)
#define MAX_USERS (8)

static void userlist_add_user_handler(const struct userinfo *user, void *arg);
static void userlist_remove_user_handler(const struct userinfo *user, bool call_vstateh, void *arg);
static void userlist_sync_users_handler(void *arg);
static void userlist_kg_change_handler(struct userinfo *keygenerator,
				       bool is_me,
				       bool is_first,
				       void *arg);
static void userlist_vstate_handler(const struct userinfo *user,
				    enum icall_vstate new_vstate,
				    void *arg);

struct user_state {
	bool     added;
	bool     is_kg;
	uint32_t ssrca;
	uint32_t ssrcv;
	enum icall_vstate vstate;
};

class UserlistTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		userlist_alloc(&list,
			       "userid_self",
			       "clientid_self",
			       userlist_add_user_handler,
			       userlist_remove_user_handler,
			       userlist_sync_users_handler,
			       userlist_kg_change_handler,
			       userlist_vstate_handler,
			       this);

		memset(users, 0, sizeof(users));
		InitSeList(&selist1, 1);
		InitSeList(&selist2, 2);
		InitSeList(&selist3, 3);
	}

	virtual void TearDown() override
	{
		mem_deref(list);
		list_flush(&selist1);
		list_flush(&selist2);
		list_flush(&selist3);
		list_flush(&sftlist1);
		list_flush(&sftlist2);
		list_flush(&sftlist3);
	}

	void InitSeList(struct list *list, size_t sz)
	{
		size_t i;

		sz = sz < MAX_USERS ? sz : MAX_USERS;
		for (i = 0; i < sz; i++) {
			char userid[ECONN_ID_LEN];
			char clientid[ECONN_ID_LEN];

			snprintf(userid, ECONN_ID_LEN-1, "user_%05zu", i);
			snprintf(clientid, ECONN_ID_LEN-1, "client_%05zu", i);
			struct icall_client *cli = icall_client_alloc(userid, clientid);
			list_append(list, &cli->le, cli);
		}
	}

	void SetInSubconv(struct list *list, size_t sz)
	{
		struct le *le = NULL;
		size_t i = 0;

		LIST_FOREACH(list, le) {
			struct icall_client *cli = (struct icall_client*)le->data;
			cli->in_subconv = (i < sz);
			i++;
		}
	}

	void InitSftList(struct list *list,
			 size_t start,
			 size_t sz,
			 const uint8_t *secret,
			 size_t secretlen)
	{
		size_t i;

		sz = sz < 1000 ? sz : 1000;
		for (i = 0; i < sz; i++) {
			char userid[ECONN_ID_LEN];
			char clientid[ECONN_ID_LEN];
			char *userid_hash = NULL;

			size_t id = start + i;

			snprintf(userid, ECONN_ID_LEN-1, "user_%05zu", id);
			snprintf(clientid, ECONN_ID_LEN-1, "client_%05zu", id);
			hash_user(secret, secretlen, userid, clientid, &userid_hash);
			struct econn_group_part *cli = econn_part_alloc(userid_hash, "_");

			cli->ssrca = 1000 + id;
			cli->ssrcv = 2000 + id;
			list_append(list, &cli->le, cli);
			mem_deref(userid_hash);
		}
	}


	void SetMuted(struct list *list, size_t sz)
	{
		struct le *le = NULL;
		size_t i = 0;

		LIST_FOREACH(list, le) {
			struct econn_group_part *cli = (struct econn_group_part*)le->data;
			cli->muted_state = (i < sz) ? MUTED_STATE_MUTED : MUTED_STATE_UNMUTED;
			i++;
		}
	}

	void InitAudioLevelList(struct list *list,
				size_t start,
				size_t sz,
				size_t active)
	{
		size_t i;

		sz = sz < 1000 ? sz : 1000;
		for (i = 0; i < sz; i++) {
			char userid[ECONN_ID_LEN];
			char clientid[ECONN_ID_LEN];

			size_t id = start + i;

			snprintf(userid, ECONN_ID_LEN-1, "user_%05zu", id);
			snprintf(clientid, ECONN_ID_LEN-1, "client_%05zu", id);
			struct audio_level *cli;

			uint8_t level = i < active ? 40 : 0;
			audio_level_alloc(&cli, list, false, userid, clientid, level, level);
		}
	}
				
	int UserIndex(const struct userinfo *user)
	{
		if (!user || strncmp(user->userid_real, "user_", 5) != 0)
			return -1;

		int idx = atoi(user->userid_real+strlen("user_"));
		if (idx < 0 || idx >= MAX_USERS)
			return -1;
		return idx;
	}

	void add_user_handler(const struct userinfo *user)
	{
		int idx = UserIndex(user);
		if (idx >= 0) {
			users[idx].added = true;
			users[idx].ssrca = user->ssrca;
			users[idx].ssrcv = user->ssrcv;
		}
	}

	void remove_user_handler(const struct userinfo *user)
	{
		int idx = UserIndex(user);
		if (idx >= 0) {
			users[idx].added = false;
		}
	}

	void sync_users_handler()
	{
		users_synced = true;
	}

	void kg_change_handler(struct userinfo *kg,
			       bool is_me,
			       bool is_first)
	{
		for(int i = 0; i < MAX_USERS; i++) {
			users[i].is_kg = false;
		}
		int idx = UserIndex(kg);
		if (idx >= 0) {
			users[idx].is_kg = true;
		}
	}

	void vstate_handler(const struct userinfo *user,
			    enum icall_vstate new_vstate)
	{
		int idx = UserIndex(user);
		if (idx >= 0) {
			users[idx].vstate = new_vstate;
		}
	}

protected:
	struct userlist *list = NULL;
	struct list selist1 = LIST_INIT;
	struct list selist2 = LIST_INIT;
	struct list selist3 = LIST_INIT;
	struct list sftlist1 = LIST_INIT;
	struct list sftlist2 = LIST_INIT;
	struct list sftlist3 = LIST_INIT;

	struct user_state users[MAX_USERS];
	bool users_synced = false;
};

static void userlist_add_user_handler(const struct userinfo *user, void *arg)
{
	UserlistTest *test = (UserlistTest*)arg;
	test->add_user_handler(user);
}

static void userlist_remove_user_handler(const struct userinfo *user, bool call_vstateh, void *arg)
{
	UserlistTest *test = (UserlistTest*)arg;
	test->remove_user_handler(user);
}

static void userlist_sync_users_handler(void *arg)
{
	UserlistTest *test = (UserlistTest*)arg;
	test->sync_users_handler();
}

static void userlist_kg_change_handler(struct userinfo *keygenerator,
				       bool is_me,
				       bool is_first,
				       void *arg)
{
	UserlistTest *test = (UserlistTest*)arg;
	test->kg_change_handler(keygenerator, is_me, is_first);
}

static void userlist_vstate_handler(const struct userinfo *user,
				    enum icall_vstate new_vstate,
				    void *arg)
{
	UserlistTest *test = (UserlistTest*)arg;
	test->vstate_handler(user, new_vstate);
}

TEST_F(UserlistTest, add_from_selist)
{
	uint8_t secret1[32] = "secret1                        ";
	bool changed = false;
	bool removed = false;

	ASSERT_EQ(userlist_get_count(list), 0);

	userlist_update_from_selist(list, &selist1, 0, secret1, sizeof(secret1), &changed, &removed);

	ASSERT_TRUE(changed);
	ASSERT_EQ(userlist_get_count(list), 1);
	const struct userinfo *u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_real, "user_00000") == 0);
	ASSERT_TRUE(strcmp(u->clientid_real, "client_00000") == 0);
	ASSERT_TRUE(u->userid_hash != NULL);
	ASSERT_TRUE(u->clientid_hash != NULL);

	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);

	ASSERT_TRUE(changed);
	ASSERT_EQ(userlist_get_count(list), 3);
	ASSERT_TRUE(userlist_find_by_real(list, "user_00000", "client_00000") != NULL);
	ASSERT_TRUE(userlist_find_by_real(list, "user_00001", "client_00001") != NULL);
	ASSERT_TRUE(userlist_find_by_real(list, "user_00002", "client_00002") != NULL);

	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_TRUE(!changed);
}

TEST_F(UserlistTest, update_secret)
{
	uint8_t secret1[32] = "secret1                        ";
	uint8_t secret2[32] = "secret2                        ";
	char *self_userid_hash = NULL;
	char *other_userid_hash = NULL;
	const struct userinfo *u;
	bool changed = false;
	bool removed = false;

	userlist_set_secret(list, secret1, sizeof(secret1));
	userlist_update_from_selist(list, &selist1, 0, secret1, sizeof(secret1), &changed, &removed);

	ASSERT_EQ(userlist_get_count(list), 1);

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	str_dup(&other_userid_hash, u->userid_hash);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);

	u = userlist_get_self(list);
	ASSERT_TRUE(u != NULL);
	str_dup(&self_userid_hash, u->userid_hash);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);

	userlist_set_secret(list, secret2, sizeof(secret2));

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_hash, other_userid_hash) != 0);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);
	
	u = userlist_get_self(list);
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_hash, self_userid_hash) != 0);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);

	mem_deref(self_userid_hash);
	mem_deref(other_userid_hash);
}

TEST_F(UserlistTest, add_from_sftlist)
{
	uint8_t secret1[32] = "secret1                        ";
	char *userid_hash = NULL;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;

	ASSERT_EQ(userlist_get_count(list), 0);

	InitSftList(&sftlist1, 0, 1, secret1, sizeof(secret1));
	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));

	userlist_update_from_sftlist(list, &sftlist1, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 1);

	hash_user(secret1, sizeof(secret1), "user_00000", "client_00000", &userid_hash);
	const struct userinfo *u = userlist_find_by_hash(list, userid_hash, "_");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_hash, userid_hash) == 0);

	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);

	ASSERT_EQ(userlist_get_count(list), 3);

	mem_deref(userid_hash);
}

TEST_F(UserlistTest, merge_sftlist_into_selist)
{
	uint8_t secret1[32] = "secret1                        ";
	char *userid_hash = NULL;
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	hash_user(secret1, sizeof(secret1), "user_00000", "client_00000", &userid_hash);
	InitSftList(&sftlist1, 0, 1, secret1, sizeof(secret1));
	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));
	userlist_set_secret(list, secret1, sizeof(secret1));

	ASSERT_EQ(userlist_get_count(list), 0);

	/* selist: [user_00000,user_00001,user_00002] */
	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* sftlist: [user_00000] */
	userlist_update_from_sftlist(list, &sftlist1, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_hash, userid_hash) == 0);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);
	ASSERT_TRUE(u->se_approved);
	ASSERT_TRUE(u->incall_now);
	ASSERT_EQ(u->ssrca, 1000);
	ASSERT_EQ(u->ssrcv, 2000);

	u = userlist_find_by_real(list, "user_00001", "client_00001");
	ASSERT_TRUE(u != NULL);

	ASSERT_TRUE(u->se_approved);
	ASSERT_FALSE(u->incall_now);
	ASSERT_EQ(u->ssrca, 0);
	ASSERT_EQ(u->ssrcv, 0);

	ASSERT_TRUE(users[0].added);
	ASSERT_TRUE(users[0].is_kg);
	ASSERT_EQ(users[0].ssrca, 1000);
	ASSERT_EQ(users[0].ssrcv, 2000);
	ASSERT_EQ(users[1].added, false);
	ASSERT_TRUE(users_synced);
	users_synced = false;

	/* sftlist: [user_00000,user_00001,user_00002] */
	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	u = userlist_find_by_real(list, "user_00001", "client_00001");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(u->se_approved);
	ASSERT_TRUE(u->incall_now);
	ASSERT_EQ(u->ssrca, 1001);
	ASSERT_EQ(u->ssrcv, 2001);

	ASSERT_TRUE(users[0].is_kg);
	ASSERT_TRUE(users[1].added);
	ASSERT_EQ(users[1].is_kg, false);
	ASSERT_EQ(users[1].ssrca, 1001);
	ASSERT_EQ(users[1].ssrcv, 2001);
	ASSERT_TRUE(users_synced);
	users_synced = false;

	mem_deref(userid_hash);
}

TEST_F(UserlistTest, merge_selist_into_sftlist)
{
	uint8_t secret1[32] = "secret1                        ";
	char *userid_hash = NULL;
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	hash_user(secret1, sizeof(secret1), "user_00000", "client_00000", &userid_hash);
	InitSftList(&sftlist1, 0, 1, secret1, sizeof(secret1));
	userlist_set_secret(list, secret1, sizeof(secret1));

	ASSERT_EQ(userlist_get_count(list), 0);

	/* sftlist: [user_00000] */
	userlist_update_from_sftlist(list, &sftlist1, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 1);

	ASSERT_TRUE(!users[0].added);
	ASSERT_TRUE(!users[0].is_kg);
	ASSERT_TRUE(!users[1].added);
	ASSERT_TRUE(!users[1].is_kg);
	ASSERT_TRUE(!users[2].added);
	ASSERT_TRUE(!users[2].is_kg);

	u = userlist_find_by_hash(list, userid_hash, "_");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(!u->se_approved);
	ASSERT_TRUE(u->incall_now);
	ASSERT_EQ(u->ssrca, 1000);
	ASSERT_EQ(u->ssrcv, 2000);

	/* selist: [user_00000,user_00001,user_00002] */
	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(strcmp(u->userid_hash, userid_hash) == 0);
	ASSERT_TRUE(strcmp(u->clientid_hash, "_") == 0);

	ASSERT_TRUE(u->incall_now);
	ASSERT_EQ(u->ssrca, 1000);
	ASSERT_EQ(u->ssrcv, 2000);

	u = userlist_find_by_real(list, "user_00001", "client_00001");
	ASSERT_TRUE(u != NULL);

	ASSERT_FALSE(u->incall_now);
	ASSERT_EQ(u->ssrca, 0);
	ASSERT_EQ(u->ssrcv, 0);

	/* incall: [user_00000] kg: user_00000 */
	ASSERT_TRUE(users[0].added);
	ASSERT_TRUE(users[0].is_kg);
	ASSERT_EQ(users[0].ssrca, 1000);
	ASSERT_EQ(users[0].ssrcv, 2000);
	ASSERT_TRUE(!users[1].added);
	ASSERT_TRUE(!users[2].added);

	mem_deref(userid_hash);
}

/* Test client leaving sft list */
TEST_F(UserlistTest, kg_change)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist1, 0, 1, secret1, sizeof(secret1));
	InitSftList(&sftlist2, 1, 2, secret1, sizeof(secret1));
	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));
	userlist_set_secret(list, secret1, sizeof(secret1));

	ASSERT_EQ(userlist_get_count(list), 0);

	/* selist: [user_00000,user_00001,user_00002] */
	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* sftlist: [user_00000] */
	userlist_update_from_sftlist(list, &sftlist1, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* incall: [user_00000] kg: user_00000 */
	ASSERT_TRUE(users[0].added);
	ASSERT_TRUE(!users[1].added);
	ASSERT_TRUE(!users[2].added);
	ASSERT_TRUE(users[0].is_kg);
	ASSERT_TRUE(users_synced);
	users_synced = false;

	/* sftlist: [user_00000,user_00001,user_00002] */
	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* incall: [user_00000,user_00001,user_00002] kg: user_00000 */
	ASSERT_TRUE(users[0].added);
	ASSERT_TRUE(users[1].added);
	ASSERT_TRUE(users[2].added);
	ASSERT_TRUE(users[0].is_kg);
	ASSERT_TRUE(!users[1].is_kg);
	ASSERT_TRUE(!users[2].is_kg);
	ASSERT_TRUE(users_synced);
	users_synced = false;

	/* sftlist: [user_0001,user_0002] */
	userlist_update_from_sftlist(list, &sftlist2, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* incall: [user_00001,user_00002] kg: user_00001 */
	ASSERT_TRUE(!users[0].added);
	ASSERT_TRUE(users[1].added);
	ASSERT_TRUE(users[2].added);
	ASSERT_TRUE(!users[0].is_kg);
	ASSERT_TRUE(users[1].is_kg);
	ASSERT_TRUE(!users[2].is_kg);
	ASSERT_TRUE(users_synced);
	users_synced = false;
}

TEST_F(UserlistTest, epoch_breakout)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool removed = false;

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	/* epoch1 breakout: [user_00000] */
	SetInSubconv(&selist3, 1);
	userlist_update_from_selist(list, &selist3, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* epoch2 breakout: [user_00000,user_00001,user_00002] */
	SetInSubconv(&selist3, 3);
	userlist_update_from_selist(list, &selist3, 2, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* epoch3 breakout: [user_00000,user_00001] */
	SetInSubconv(&selist3, 2);
	userlist_update_from_selist(list, &selist3, 3, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(u->in_subconv);
	ASSERT_EQ(u->first_epoch, 1);

	u = userlist_find_by_real(list, "user_00001", "client_00001");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(u->in_subconv);
	ASSERT_EQ(u->first_epoch, 2);

	u = userlist_find_by_real(list, "user_00002", "client_00002");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(!u->in_subconv);
	ASSERT_EQ(u->first_epoch, 0);
}

TEST_F(UserlistTest, epoch_addremove)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool removed = false;

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	/* epoch1: [user_00000] */
	SetInSubconv(&selist1, 1);
	userlist_update_from_selist(list, &selist1, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 1);

	/* epoch2: [user_00000,user_00001,user_00002] */
	SetInSubconv(&selist3, 3);
	userlist_update_from_selist(list, &selist3, 2, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	/* epoch3: [user_00000,user_00001] */
	SetInSubconv(&selist2, 2);
	userlist_update_from_selist(list, &selist2, 3, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	u = userlist_find_by_real(list, "user_00000", "client_00000");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(u->in_subconv);
	ASSERT_EQ(u->first_epoch, 1);

	u = userlist_find_by_real(list, "user_00001", "client_00001");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(u->in_subconv);
	ASSERT_EQ(u->first_epoch, 2);

	u = userlist_find_by_real(list, "user_00002", "client_00002");
	ASSERT_TRUE(u != NULL);
	ASSERT_TRUE(!u->se_approved);
	ASSERT_TRUE(!u->in_subconv);
	ASSERT_EQ(u->first_epoch, 0);
}

TEST_F(UserlistTest, keysync_breakout)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist3, 1);
	userlist_update_from_selist(list, &selist3, 1, secret1, sizeof(secret1), &changed, &removed);
	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	userlist_set_latest_epoch(list, 1);
	userlist_set_latest_epoch_for_client(list, "user_00000", "client_00000", 4);
	userlist_set_latest_epoch_for_client(list, "user_00001", "client_00001", 3);
	userlist_set_latest_epoch_for_client(list, "user_00002", "client_00002", 2);

	/* key = 1 (self) */
	ASSERT_EQ(1, userlist_get_key_index(list));

	userlist_set_latest_epoch(list, 5);
	/* key = 4 (user_00000) */
	ASSERT_EQ(4, userlist_get_key_index(list));

	SetInSubconv(&selist3, 3);
	userlist_update_from_selist(list, &selist3, 1, secret1, sizeof(secret1), &changed, &removed);
	/* key = 2 (user_00002) */
	ASSERT_EQ(2, userlist_get_key_index(list));

	SetInSubconv(&selist3, 2);
	userlist_update_from_selist(list, &selist3, 1, secret1, sizeof(secret1), &changed, &removed);
	/* key = 3 (user_00001) */
	ASSERT_EQ(3, userlist_get_key_index(list));

}

TEST_F(UserlistTest, keysync_newclient)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist1, 1);
	userlist_update_from_selist(list, &selist1, 1, secret1, sizeof(secret1), &changed, &removed);
	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	userlist_set_latest_epoch(list, 5);
	userlist_set_latest_epoch_for_client(list, "user_00000", "client_00000", 4);

	SetInSubconv(&selist2, 2);
	userlist_update_from_selist(list, &selist2, 2, secret1, sizeof(secret1), &changed, &removed);

	/* key = 4 (user_00000) */
	ASSERT_EQ(4, userlist_get_key_index(list));

	userlist_set_latest_epoch_for_client(list, "user_00001", "client_00001", 3);
	/* key = 3 (user_00001) */
	ASSERT_EQ(3, userlist_get_key_index(list));
}

TEST_F(UserlistTest, keysync_sftlist)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist1, 0, 1, secret1, sizeof(secret1));
	InitSftList(&sftlist2, 0, 2, secret1, sizeof(secret1));
	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist3, 3);
	userlist_update_from_selist(list, &selist3, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	userlist_set_latest_epoch(list, 1);
	userlist_set_latest_epoch_for_client(list, "user_00000", "client_00000", 4);
	userlist_set_latest_epoch_for_client(list, "user_00001", "client_00001", 3);
	userlist_set_latest_epoch_for_client(list, "user_00002", "client_00002", 2);

	userlist_update_from_sftlist(list, &sftlist1, &changed, &self_changed, &missing);

	/* key = 1 (self) */
	ASSERT_EQ(1, userlist_get_key_index(list));

	userlist_set_latest_epoch(list, 5);
	/* key = 4 (user_00000) */
	ASSERT_EQ(4, userlist_get_key_index(list));

	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);
	/* key = 2 (user_00002) */
	ASSERT_EQ(2, userlist_get_key_index(list));

	userlist_update_from_sftlist(list, &sftlist2, &changed, &self_changed, &missing);
	/* key = 3 (user_00001) */
	ASSERT_EQ(3, userlist_get_key_index(list));

}

/* selist:   [user_00000, user_00001]
   sftlist:  [user_00001, user_00002]
   expected: [self, user_00001, user_00000] (incall first)
*/
TEST_F(UserlistTest, get_members)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	struct wcall_members *members = NULL;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist2, 1, 2, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist2, 2);
	userlist_update_from_selist(list, &selist2, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 2);

	userlist_update_from_sftlist(list, &sftlist2, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	ASSERT_EQ(0, userlist_get_members(list,
					  &members,
					  ICALL_AUDIO_STATE_ESTABLISHED,
					  ICALL_VIDEO_STATE_STARTED));

	ASSERT_EQ(members->membc, 3);
	ASSERT_EQ(strcmp(members->membv[0].userid, "userid_self"), 0);
	ASSERT_EQ(strcmp(members->membv[0].clientid, "clientid_self"), 0);
	ASSERT_EQ(members->membv[0].audio_state, ICALL_AUDIO_STATE_ESTABLISHED);
	ASSERT_EQ(members->membv[0].video_recv, ICALL_VIDEO_STATE_STARTED);

	ASSERT_EQ(strcmp(members->membv[1].userid, "user_00001"), 0);
	ASSERT_EQ(strcmp(members->membv[1].clientid, "client_00001"), 0);
	ASSERT_EQ(members->membv[1].audio_state, ICALL_AUDIO_STATE_ESTABLISHED);

	ASSERT_EQ(strcmp(members->membv[2].userid, "user_00000"), 0);
	ASSERT_EQ(strcmp(members->membv[2].clientid, "client_00000"), 0);
	ASSERT_EQ(members->membv[2].audio_state, ICALL_AUDIO_STATE_CONNECTING);

	mem_deref(members);
}

/* selist:    [user_00000, user_00001, user_00002]
   sftlist:   [muted,      muted,      unmuted]
   audiolist: [active,     inactive,   inactive]
   expected:  [unmuted,    muted,      unmuted]
*/
TEST_F(UserlistTest, get_members_mute_state)
{
	uint8_t secret1[32] = "secret1                        ";
	const struct userinfo *u;
	struct list levell = LIST_INIT;
	struct wcall_members *members = NULL;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	InitSftList(&sftlist3, 0, 3, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist2, 3);
	userlist_update_from_selist(list, &selist3, 0, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	SetMuted(&sftlist3, 2);
	userlist_update_from_sftlist(list, &sftlist3, &changed, &self_changed, &missing);

	InitAudioLevelList(&levell, 0, 3, 1);
	userlist_update_audio_level(list, &levell, &changed);

	//re_printf("%H\n", userlist_debug, list);
	ASSERT_EQ(userlist_get_count(list), 3);

	ASSERT_EQ(0, userlist_get_members(list,
					  &members,
					  ICALL_AUDIO_STATE_ESTABLISHED,
					  ICALL_VIDEO_STATE_STARTED));

	ASSERT_EQ(members->membc, 4);
	ASSERT_EQ(strcmp(members->membv[1].userid, "user_00000"), 0);
	ASSERT_EQ(strcmp(members->membv[1].clientid, "client_00000"), 0);
	ASSERT_EQ(members->membv[1].muted, 1);

	ASSERT_EQ(strcmp(members->membv[2].userid, "user_00001"), 0);
	ASSERT_EQ(strcmp(members->membv[2].clientid, "client_00001"), 0);
	ASSERT_EQ(members->membv[2].muted, 1);

	ASSERT_EQ(strcmp(members->membv[3].userid, "user_00002"), 0);
	ASSERT_EQ(strcmp(members->membv[3].clientid, "client_00002"), 0);
	ASSERT_EQ(members->membv[3].muted, 0);

	list_flush(&levell);
	mem_deref(members);
}

/* selist:   [user00000, user00001]
   sftlist:  [user00001, user00002]
   expected: [user00001] (hashed)
*/
TEST_F(UserlistTest, get_partlist)
{
	uint8_t secret1[32] = "secret1                        ";
	struct list partlist = LIST_INIT;
	struct econn_group_part *p = NULL;
	char *userid_hash = NULL;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	bool removed = false;

	hash_user(secret1, sizeof(secret1), "user_00001", "client_00001", &userid_hash);
	InitSftList(&sftlist2, 1, 2, secret1, sizeof(secret1));

	userlist_set_secret(list, secret1, sizeof(secret1));
	ASSERT_EQ(userlist_get_count(list), 0);

	SetInSubconv(&selist2, 0);
	userlist_update_from_selist(list, &selist2, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 2);

	userlist_update_from_sftlist(list, &sftlist2, &changed, &self_changed, &missing);
	ASSERT_EQ(userlist_get_count(list), 3);

	ASSERT_EQ(0, userlist_get_partlist(list, &partlist, false));
	ASSERT_EQ(list_count(&partlist), 1);

	p = (struct econn_group_part*)partlist.head->data;
	ASSERT_EQ(strcmp(p->userid, userid_hash), 0);
	ASSERT_EQ(strcmp(p->clientid, "_"), 0);

	ASSERT_EQ(0, userlist_get_partlist(list, &partlist, true));
	ASSERT_EQ(list_count(&partlist), 0);

	SetInSubconv(&selist2, 2);
	userlist_update_from_selist(list, &selist2, 1, secret1, sizeof(secret1), &changed, &removed);
	ASSERT_EQ(userlist_get_count(list), 3);

	ASSERT_EQ(0, userlist_get_partlist(list, &partlist, true));
	ASSERT_EQ(list_count(&partlist), 1);

	list_flush(&partlist);
	mem_deref(userid_hash);
}

