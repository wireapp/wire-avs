/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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

struct userinfo {
	struct le le;
	char *userid_real;
	char *userid_hash;
	char *clientid_real;
	char *clientid_hash;

	uint32_t ssrca;
	uint32_t ssrcv;
	enum icall_vstate video_state;
	bool muted;
	bool in_subconv;
	bool was_in_subconv;
	uint32_t first_epoch;
	uint32_t latest_epoch;

	bool needs_key;

	bool incall_now;
	bool incall_prev;
	bool se_approved;
	bool force_decoder;
	uint32_t listpos;
};

typedef void (userlist_add_user_h)(const struct userinfo *user,
				   void *arg);
typedef void (userlist_remove_user_h)(const struct userinfo *user,
				      bool call_vstateh,
				      void *arg);
typedef void (userlist_sync_users_h)(void *arg);

typedef void (userlist_kg_change_h)(struct userinfo *keygenerator,
				    bool is_me,
				    bool is_first,
				    void *arg);

typedef void (userlist_vstate_h)(const struct userinfo *user,
				 enum icall_vstate new_vstate,
				 void *arg);

struct userlist {
	struct list            users;
	struct userinfo        *self;
	struct userinfo        *keygenerator;
	userlist_add_user_h    *addh;
	userlist_remove_user_h *removeh;
	userlist_sync_users_h  *synch;
	userlist_kg_change_h   *kgchangeh;
	userlist_vstate_h      *vstateh;
	void                   *arg;
};

int userlist_alloc(struct userlist **listp,
		   const char *userid_self,
		   const char *clientid_self,
		   userlist_add_user_h *addh,
		   userlist_remove_user_h *removeh,
		   userlist_sync_users_h *synch,
		   userlist_kg_change_h *kgchangeh,
		   userlist_vstate_h *vstateh,
		   void *arg);

int userlist_update_from_sftlist(struct userlist *list,
				 const struct list *partlist,
				 bool *changed,
				 bool *missing_parts);

void userlist_update_from_selist(struct userlist* list,
				 struct list *clientl,
				 uint32_t epoch,
				 uint8_t *secret,
				 size_t secret_len,
				 bool *changed,
				 bool *removed);

const struct userinfo *userlist_get_self(const struct userlist *list);

struct userinfo *userlist_find_by_real(const struct userlist *list,
				       const char *userid_real,
				       const char *clientid_real);

struct userinfo *userlist_find_by_hash(const struct userlist *list,
				       const char *userid_hash,
				       const char *clientid_hash);

bool userlist_is_keygenerator_me(struct userlist *list);

const struct userinfo *userlist_get_keygenerator(struct userlist *list);

void userlist_reset_keygenerator(struct userlist *list);

size_t userlist_get_count(struct userlist *list);

int userlist_set_secret(struct userlist *list,
			const uint8_t *secret,
			size_t secret_len);

int userlist_set_latest_epoch(struct userlist *list,
			      uint32_t epoch);

int userlist_get_latest_epoch(struct userlist *list,
			      uint32_t *pepoch);

int userlist_set_latest_epoch_for_client(struct userlist *list,
					 const char *userid_real,
					 const char *clientid_real,
					 uint32_t epoch);

uint32_t userlist_get_key_index(struct userlist *list);

void userlist_incall_clear(struct userlist *list,
			   bool force_decoder);

uint32_t userlist_incall_count(struct userlist *list);

int userlist_get_key_targets(struct userlist *list,
			     struct list *targets,
			     bool send_to_all);

int userlist_get_members(struct userlist *list,
			 struct wcall_members **mmp,
			 enum icall_audio_state astate,
			 enum icall_vstate vstate);

int userlist_get_partlist(struct userlist *list,
			  struct list *msglist,
			  bool require_subconv);

int userlist_get_my_clients(struct userlist *list,
			    struct list *targets);

int userlist_reset_video_states(const struct userlist* list);

int userlist_debug(struct re_printf *pf,
		   const struct userlist* list);

int hash_conv(const uint8_t *secret,
	      uint32_t secretlen,
	      const char *convid, 
	      char **destid_hash);

int hash_user(const uint8_t *secret,
	      uint32_t secretlen,
	      const char *userid, 
	      const char *clientid,
	      char **destid_hash);

