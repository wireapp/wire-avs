/*
* Wire
* Copyright (C) 2022 Wire Swiss GmbH
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
#include <assert.h>
#include "avs_wcall.h"
#include "avs_audio_level.h"

#define LIST_POS_NONE 0xFFFFFFFF

static void userinfo_destructor(void *arg)
{
	struct userinfo *ui = arg;

	list_unlink(&ui->le);
	ui->userid_real = mem_deref(ui->userid_real);
	ui->userid_hash = mem_deref(ui->userid_hash);
	ui->clientid_real = mem_deref(ui->clientid_real);
	ui->clientid_hash = mem_deref(ui->clientid_hash);
}

static int userinfo_alloc(struct userinfo **userp,
			  const char *userid_real,
			  const char *clientid_real,
			  const char *userid_hash,
			  const char *clientid_hash)
{
	struct userinfo *u = NULL;

	u = mem_zalloc(sizeof(*u), userinfo_destructor);
	if (!u) {
		warning("userinfo_alloc failed\n");
		return ENOMEM;
	}

	str_dup(&u->userid_real, userid_real);
	str_dup(&u->clientid_real, clientid_real);
	str_dup(&u->userid_hash, userid_hash);
	str_dup(&u->clientid_hash, clientid_hash);

	*userp = u;

	return 0;
}

static void userlist_destructor(void *arg)
{
	struct userlist *list = arg;

	list_flush(&list->users);
	mem_deref(list->self);

}

int userlist_alloc(struct userlist **listp,
		   const char *userid_self,
		   const char *clientid_self,
		   userlist_add_user_h *addh,
		   userlist_remove_user_h *removeh,
		   userlist_sync_users_h *synch,
		   userlist_kg_change_h *kgchangeh,
		   userlist_vstate_h *vstateh,
		   void *arg)
{
	struct userlist *lp = NULL;
	int err = 0;

	lp = mem_zalloc(sizeof(*lp), userlist_destructor);
	if (!lp) {
		err = ENOMEM;
		goto out;
	}

	err = userinfo_alloc(&lp->self,
			     userid_self,
			     clientid_self,
			     NULL,
			     NULL);
	if (err)
		goto out;

	lp->addh = addh;
	lp->removeh = removeh;
	lp->synch = synch;
	lp->kgchangeh = kgchangeh;
	lp->vstateh = vstateh;
	lp->arg = arg;
	lp->self->latest_epoch = 0;

	*listp = lp;

out:
	if (err)
		mem_deref(lp);

	return err;
}

static bool userlist_sort_h(struct le *le1, struct le *le2, void *arg)
{
	struct userinfo *a, *b;

	a = le1->data;
	b = le2->data;

	return a->listpos <= b->listpos;
}

size_t userlist_get_count(struct userlist *list)
{
	return list_count(&list->users);
}

const struct userinfo *userlist_get_self(const struct userlist *list)
{
	if (!list)
		return NULL;

	return list->self;
}

struct userinfo *userlist_find_by_real(const struct userlist *list,
				       const char *userid_real,
				       const char *clientid_real)
{
	struct le *le;

	if (!list || !userid_real || !clientid_real) {
		return NULL;
	}

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;

		if (u && u->userid_real && u->clientid_real) {
			if (strcaseeq(u->userid_real, userid_real) &&
			    strcaseeq(u->clientid_real, clientid_real)) {
				return u;
			}
		}
	}
	return NULL;
}

struct userinfo *userlist_find_by_hash(const struct userlist *list,
				       const char *userid_hash,
				       const char *clientid_hash)
{
	struct le *le;

	if (!list || !userid_hash || !clientid_hash) {
		return NULL;
	}

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;

		if (u && u->userid_hash && u->clientid_hash) {
			if (strcaseeq(u->userid_hash, userid_hash) &&
			    strcaseeq(u->clientid_hash, clientid_hash)) {
				return u;
			}
		}
	}
	return NULL;
}


bool userlist_is_keygenerator_me(struct userlist *list)
{
	if (!list)
		return false;

	return list->keygenerator == list->self;
}

const struct userinfo *userlist_get_keygenerator(struct userlist *list)
{
	if (!list)
		return NULL;

	return list->keygenerator;
}

void userlist_reset_keygenerator(struct userlist *list)
{
	if (!list)
		return;

	list->keygenerator = NULL;
}

static void hash_userinfo(struct userinfo *info,
			  const uint8_t *secret,
			  size_t secret_len)
{

	info->userid_hash = mem_deref(info->userid_hash);
	info->clientid_hash = mem_deref(info->clientid_hash);
	hash_user(secret,
		  secret_len,
		  info->userid_real,
		  info->clientid_real,
		  &info->userid_hash);

	str_dup(&info->clientid_hash, "_");
}

int userlist_set_secret(struct userlist *list,
			const uint8_t *secret,
			size_t secret_len)
{
	struct le *le;
	int err = 0;

	hash_userinfo(list->self, secret, secret_len);

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		hash_userinfo(u, secret, secret_len);
	}

	return err;
}

static void track_keygenerator_change(struct userlist *list,
				      struct userinfo *prev_keygenerator)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	bool is_me = list->keygenerator == list->self;

	if (!list ||
	    list->keygenerator == NULL ||
	    list->keygenerator == prev_keygenerator)
		return;

	info("userlist(%p): track_keygenerator: new keygenerator is %s.%s %s\n",
	      list,
	      anon_id(userid_anon, list->keygenerator->userid_real),
	      anon_client(clientid_anon, list->keygenerator->clientid_real),
	      is_me ? "(me)" : "");

	if (list->kgchangeh)
		list->kgchangeh(list->keygenerator,
				is_me,
				prev_keygenerator == NULL,
				list->arg);
}

static struct le *find_first_approved(struct userlist *list, const struct list *partlist)
{
	struct le *le;

	LIST_FOREACH(partlist, le) {	
		struct econn_group_part *p = le->data;
		struct userinfo *u;

		if (p && strcaseeq(list->self->userid_hash, p->userid) &&
		    strcaseeq(list->self->clientid_hash, p->clientid)) {
			return le;
		}

		u = userlist_find_by_hash(list, p->userid, p->clientid);
		if (u && u->se_approved)
			return le;
	}

	return NULL;
}

int userlist_update_from_sftlist(struct userlist *list,
				 const struct list *partlist,
				 bool *changed,
				 bool *self_changed,
				 bool *missing_parts)
{
	struct le *le, *cle;
	struct le *first_approved = NULL;
	bool list_changed = false;
	bool sync_decoders = false;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct userinfo *prev_keygenerator;
	bool missing = false;
	uint32_t listpos = 0;

	if (!list || !partlist || !changed || !self_changed)
		return EINVAL;

	info("userlist(%p): update_from_sftlist %u members\n", list, list_count(partlist));
	list->self->listpos = LIST_POS_NONE;
	prev_keygenerator = list->keygenerator;
	list->keygenerator = NULL;

	LIST_FOREACH(&list->users, cle) {
		struct userinfo *u = cle->data;
		if (!u)
			continue;
		u->incall_prev = u->incall_now;
		u->incall_now = false;
		u->listpos = LIST_POS_NONE;
	}

	first_approved = find_first_approved(list, partlist);	
	LIST_FOREACH(partlist, le) {
		struct econn_group_part *p = le->data;

		if (!p)
			continue;

		if (strcaseeq(list->self->userid_hash, p->userid) &&
		    strcaseeq(list->self->clientid_hash, p->clientid)) {
			if (le == first_approved) {
				info("userlist(%p): setting self as keygenerator\n", list);
				list->keygenerator = list->self;
			}
			*self_changed = false;
			if (list->self->ssrca != p->ssrca
			    || list->self->ssrcv != p->ssrcv) {
				info("userlist(%p): update_from_sftlist: setting self ssrca:%u->%u ssrcv%u->%u\n",
				     list,
				     list->self->ssrca,
				     p->ssrca,
				     list->self->ssrcv,
				     p->ssrcv);

				list->self->ssrca = p->ssrca;
				list->self->ssrcv = p->ssrcv;
				*self_changed = true;
			}
			list->self->listpos = listpos;
			listpos++;
			continue;
		}

		struct userinfo *u = userlist_find_by_hash(list, p->userid, p->clientid);
		if (u) {
			bool muted;

			info("userlist(%p): update_from_sftlist found user %s.%s %s"
			     "ssrca: %u ssrcv:%u incall_prev: %s\n",
			     list, 
			     anon_id(userid_anon, u->userid_real),
			     anon_client(clientid_anon, u->clientid_real),
			     u->userid_hash,
			     p->ssrca, p->ssrcv,
			     u->incall_prev ? "YES" : "NO");
			if (le == first_approved) {
				info("userlist(%p): setting %s.%s as keygenerator\n",
				     list,
				     anon_id(userid_anon, u->userid_real),
				     anon_client(clientid_anon, u->clientid_real));

				list->keygenerator = u;
			}
			if (u->incall_prev && 
			    (u->ssrca != p->ssrca ||
			    u->ssrcv != p->ssrcv)) {
				if (list->removeh)
					list->removeh(u, p->ssrcv == 0, list->arg);
				//ccall->someone_left = true;
				u->incall_prev = false;
				sync_decoders = true;
				list_changed = true;
			}

			u->incall_now = true;
			u->ssrca = p->ssrca;
			u->ssrcv = p->ssrcv;

			switch (p->muted_state) {
			case MUTED_STATE_UNKNOWN:
				muted = u->muted;
				break;
			case MUTED_STATE_MUTED:
				muted = true;
				break;
			case MUTED_STATE_UNMUTED:
				muted = false;
				break;
			}

			if (muted != u->muted) {
				u->muted = muted;
				list_changed = true;
			}

			u->listpos = listpos;
			listpos++;
		}
		else {
			warning("userlist(%p): update_from_sftlist didnt find part for %s\n",
			     list, anon_id(userid_anon, p->userid));
			u = mem_zalloc(sizeof(*u), userinfo_destructor);
			if (!u) {
				warning("userlist(%p): update_from_sftlist"
					"couldnt alloc part of %s\n",
				     list, anon_id(userid_anon, p->userid));
				return ENOMEM;
			}
			str_dup(&u->userid_hash, p->userid);
			str_dup(&u->clientid_hash, p->clientid);
			u->ssrca = p->ssrca;
			u->ssrcv = p->ssrcv;
			u->incall_now = true;
			u->latest_epoch = 0;
			list_append(&list->users, &u->le, u);
			missing = true;
			u->listpos = listpos;
			listpos++;
		}

	}

	LIST_FOREACH(&list->users, cle) {
		struct userinfo *u = cle->data;

		if (u && u->se_approved) {
			if (u->force_decoder ||
			   (u->incall_now && !u->incall_prev)) {
				if (list->addh && (u->ssrca != 0 || u->ssrcv != 0)) {
					list->addh(u, list->arg);
					sync_decoders = true;
				}
				u->force_decoder = false;
				u->needs_key = true;
				list_changed = true;
				info("userlist(%p): update_from_sftlist %s.%s joined the call\n",
				     list,
				     anon_id(userid_anon, u->userid_real),
				     anon_client(clientid_anon, u->clientid_real));
			}
			else if (!u->incall_now && u->incall_prev) {
				if (list->removeh)
					list->removeh(u, true, list->arg);
				//ccall->someone_left = true;
				sync_decoders = true;

				u->ssrca = u->ssrcv = 0;
				u->video_state = ICALL_VIDEO_STATE_STOPPED;
				list_changed = true;
			}
		}
	}

	info("userlist(%p): update_from_sftlist sync: %s changed: %s missing: %s\n",
	     list,
	     sync_decoders ? "YES" : "NO",
	     list_changed ? "YES" : "NO",
	     missing ? "YES" : "NO");
	if (sync_decoders && list->synch) {
		info("userlist(%p): sync_decoders\n", list);
		list->synch(list->arg);
	}

	track_keygenerator_change(list, prev_keygenerator);
	list_sort(&list->users, userlist_sort_h, NULL);

	*changed = list_changed;
	*missing_parts = missing;
	return 0;
}

void userlist_update_from_selist(struct userlist* list,
				 struct list *clientl,
				 uint32_t epoch,
				 uint8_t *secret,
				 size_t secret_len,
				 bool *changed,
				 bool *removed)
{
	struct le *le;
	bool list_changed = false;
	bool list_removed = false;
	bool sync_decoders = false;
	struct userinfo *user;
	struct userinfo *prev_keygenerator;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!list || !clientl)
		return;

	prev_keygenerator = list->keygenerator;

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (!u)
			continue;
		u->was_se_approved = u->se_approved;
		u->se_approved = false;
		u->was_in_subconv = u->in_subconv;
		u->in_subconv = false;
	}

	LIST_FOREACH(clientl, le) {
		struct icall_client *cli = le->data;

		if (!cli)
			continue;

		/* Skip self if we happen to get self in the SE list */
		if (strcaseeq(list->self->userid_real, cli->userid) &&
		    strcaseeq(list->self->clientid_real, cli->clientid)) {
			continue;
		}

		user = userlist_find_by_real(list, cli->userid, cli->clientid);
		if (user) {
			hash_userinfo(user, secret, secret_len);
			info("userlist(%p): update_from_selist updating found client %s.%s\n",
			     list,
			     anon_id(userid_anon, cli->userid),
			     anon_client(clientid_anon, cli->clientid));
		}
		else {
			struct userinfo *u = mem_zalloc(sizeof(*u), userinfo_destructor);
			if (!u) {
				warning("userlist(%p): set_clients couldnt alloc user\n", list);
				return;
			}
			str_dup(&u->userid_real, cli->userid);
			str_dup(&u->clientid_real, cli->clientid);
			hash_userinfo(u, secret, secret_len);
			user = userlist_find_by_hash(list, u->userid_hash, u->clientid_hash);
			if (user && !user->se_approved) {
				info("userlist(%p): update_from_selist approving found "
				     "client %s.%s\n",
				     list,
				     anon_id(userid_anon, cli->userid),
				     anon_client(clientid_anon, cli->clientid));
				user->userid_real = mem_deref(user->userid_real);
				user->clientid_real = mem_deref(user->clientid_real);
				str_dup(&user->userid_real, cli->userid);
				str_dup(&user->clientid_real, cli->clientid);
				user->first_epoch = epoch;
				list_changed = true;

				if (list->addh && (user->ssrca != 0 || user->ssrcv != 0)) {
					list->addh(user, list->arg);
					sync_decoders = true;
				}

				if (user->listpos == 0) {
					list->keygenerator = user;
					info("userlist(%p): setting keygenerator from "
					     "updated list\n",
					     list);

					track_keygenerator_change(list,
								  prev_keygenerator);
				}

				user->needs_key = true;
				user->force_decoder = false;
				mem_deref(u);
			}
			else {
				info("userlist(%p): update_from_selist adding new "
				     "client %s.%s\n",
				     list,
				     anon_id(userid_anon, cli->userid),
				     anon_client(clientid_anon, cli->clientid));
				user = u;
				list_append(&list->users, &u->le, u);
				list_changed = true;
			}

		}
		if (user) {
			user->se_approved = true;
			user->in_subconv = cli->in_subconv;
			if (user->in_subconv && user->first_epoch == 0)
				user->first_epoch = epoch;
		}
	}

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (!u)
			continue;

		if (!u->in_subconv) {
			u->first_epoch = 0;
			if (u->was_in_subconv) {
				list_removed = true;
				list_changed = true;
			}
		}
		if (u->se_approved != u->was_se_approved) {
			list_removed = true;
			list_changed = true;
		}
	}

	if (sync_decoders && list->synch) {
		info("userlist(%p): sync_decoders\n", list);
		list->synch(list->arg);
	}

	if (NULL != changed)
		*changed = list_changed;

	if (NULL != removed)
		*removed = list_removed;
}

int userlist_set_latest_epoch(struct userlist *list,
			      uint32_t epoch)
{
	if (!list)
		return EINVAL;

	if (!list->self)
		return ENOENT;

	list->self->latest_epoch = epoch;

	return 0;
}

int userlist_get_latest_epoch(struct userlist *list,
			      uint32_t *pepoch)
{
	if (!list)
		return EINVAL;

	if (!list->self)
		return ENOENT;

	*pepoch = list->self->latest_epoch;
	return 0;
}

int userlist_set_latest_epoch_for_client(struct userlist *list,
					 const char *userid_real,
					 const char *clientid_real,
					 uint32_t epoch)
{
	struct userinfo *user;

	if (!list)
		return EINVAL;

	user = userlist_find_by_real(list,
				     userid_real,
				     clientid_real);
	if (!user)
		return ENOENT;

	user->latest_epoch = epoch;

	return 0;
}

uint32_t userlist_get_key_index(struct userlist *list)
{
	struct le *le;
	uint32_t min_key;

	if (!list)
		return EINVAL;

	info("userlist(%p): get_key_index self: %u\n", list, list->self->latest_epoch);
	min_key = list->self->latest_epoch;
	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
#if 0
		info("userlist(%p): get_key_index cmp m: %u a: %s s: %s c: %s e: %u\n",
		     list,
		     min_key,
		     u->se_approved ? "YES" : "NO",
		     u->in_subconv ? "YES" : "NO",
		     u->incall_now ? "YES" : "NO",
		     u->latest_epoch);
#endif
		if (u && u->se_approved && u->in_subconv && u->incall_now) {
			if (u->latest_epoch > 0 &&
			    (min_key == 0 || u->latest_epoch < min_key)) {
				min_key = u->latest_epoch;
			}
		}
	}

	return min_key;
}

void userlist_incall_clear(struct userlist *list,
			   bool force_decoder,
			   bool again)
{
	struct le *le;
	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (!u)
			continue;
		u->incall_now = u->incall_now && force_decoder;
		u->force_decoder = u->incall_now;
		u->incall_prev = false;
		if (!again) {
			u->ssrca = 0;
			u->ssrcv = 0;
		}
	}
}

uint32_t userlist_incall_count(struct userlist *list)
{
	struct le *le;
	uint32_t incall = 0;

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (u && u->incall_now) {
			incall++;
		}
	}
	return incall;
}

int userlist_get_key_targets(struct userlist *list,
			     struct list *targets,
			     bool send_to_all)
{
	struct le *le = NULL;
	int err = 0;

	if (!list || !targets)
		return EINVAL;

	list_flush(targets);
	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (u && u->incall_now && u->se_approved &&
		    (u->needs_key || send_to_all)) {
			struct icall_client *c;

			c = icall_client_alloc(u->userid_real,
					       u->clientid_real);
			if (!c) {
				warning("userlist(%p): send_keys "
					"unable to alloc target\n", list);
				err = ENOMEM;
				goto out;
			}
			list_append(targets, &c->le, c);
			u->needs_key = false;
		}
	}

out:
	return err;
}

static void members_destructor(void *arg)
{
	struct wcall_members *mm = arg;
	size_t i;

	for (i = 0; i < mm->membc; ++i) {
		mem_deref(mm->membv[i].userid);
		mem_deref(mm->membv[i].clientid);
	}

	mem_deref(mm->membv);
}


int userlist_get_members(struct userlist *list,
			 struct wcall_members **mmp,
			 enum icall_audio_state astate,
			 enum icall_vstate vstate)
{
	struct wcall_members *mm;
	struct le *le;
	size_t n = 0;
	int err = 0;

	if (!list || !mmp)
		return EINVAL;

	mm = mem_zalloc(sizeof(*mm), members_destructor);
	if (!mm) {
		err = ENOMEM;
		goto out;
	}

	n = 1;
	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (u && u->se_approved && (u->incall_now || u->in_subconv)) {
			n++;
		}
	}

	mm->membv = mem_zalloc(sizeof(*(mm->membv)) * n, NULL);
	if (!mm->membv) {
		err = ENOMEM;
		goto out;
	}
	{
		struct userinfo *u = list->self;
		struct wcall_member *memb = &(mm->membv[mm->membc]);

		str_dup(&memb->userid, u->userid_real);
		str_dup(&memb->clientid, u->clientid_real);

		memb->audio_state = astate;
		memb->video_recv = vstate;
		memb->muted = msystem_get_muted() ? 1 : 0;

		(mm->membc)++;
	}

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;

		if (u && u->se_approved && (u->incall_now || u->in_subconv)) {
			struct wcall_member *memb = &(mm->membv[mm->membc]);

			assert(mm->membc < n);
			str_dup(&memb->userid, u->userid_real);
			str_dup(&memb->clientid, u->clientid_real);
			memb->audio_state = u->ssrca > 0 ?
					    ICALL_AUDIO_STATE_ESTABLISHED :
					    ICALL_AUDIO_STATE_CONNECTING;
			memb->video_recv = u->ssrcv > 0 ?
					   u->video_state :
					   ICALL_VIDEO_STATE_STOPPED;
			memb->muted = (u->muted && !u->active_audio) ? 1 : 0;

			(mm->membc)++;
		}
	}

 out:
	if (err)
		mem_deref(mm);
	else {
		*mmp = mm;
	}

	return err;
}

int userlist_get_partlist(struct userlist *list,
			  struct list *msglist,
			  bool require_subconv)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct le *le;
	int err = 0;

	list_flush(msglist);

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (u && u->se_approved && u->incall_now &&
		    (u->in_subconv || !require_subconv)) {
			struct econn_group_part *part = econn_part_alloc(u->userid_hash,
									 u->clientid_hash);
			if (!part) {
				err = ENOMEM;
				goto out;
			}
			part->ssrca = u->ssrca;
			part->ssrcv = u->ssrcv;
			part->authorized = true;
			list_append(msglist, &part->le, part);

			info("userlist(%p) get_members adding %s.%s hash %s.%s "
			     " ssrca %u ssrcv %u\n",
			     list,
			     anon_id(userid_anon, u->userid_real),
			     anon_client(clientid_anon, u->clientid_real),
			     u->userid_hash,
			     u->clientid_hash,
			     u->ssrca,
			     u->ssrcv);
		}
	}

out:
	if (err)
		list_flush(msglist);

	return err;

}

int userlist_get_my_clients(struct userlist *list, struct list *targets)
{
	struct le *le = NULL;
	int err = 0;

	list_flush(targets);

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;
		if (u && strcaseeq(u->userid_real, list->self->userid_real) &&
		    !strcaseeq(u->clientid_real, list->self->clientid_real)) {
			struct icall_client *c;

			c = icall_client_alloc(u->userid_real,
					       u->clientid_real);
			if (!c) {
				err = ENOMEM;
				goto out;
			}
			list_append(targets, &c->le, c);
		}
	}

out:
	if (err)
		list_flush(targets);

	return err;
}

void userlist_update_audio_level(struct userlist *list,
				 const struct list *levell,
				 bool *list_changed)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct userinfo *u = NULL;
	struct le *le = NULL;

	*list_changed = false;

	if (!list || !levell)
		return;

	info("userlist(%p): update_audio_level\n", list);
	LIST_FOREACH(&list->users, le) {
		u = le->data;
		if (!u)
			continue;
		u->active_prev = u->active_audio;
		u->active_audio = false;
	}

	LIST_FOREACH(levell, le) {
		const char *uid = NULL;
		const char *cid = NULL;
		struct audio_level *a = le->data;

		if (!a)
			continue;

		uid = audio_level_userid(a);
		cid = audio_level_clientid(a);

		if (!uid || !cid)
			continue;

		info("userlist(%p): update_audio_level user %s.%s level %d\n",
		     list,
		     anon_id(userid_anon, uid),
		     anon_client(clientid_anon, cid),
		     audio_level(a));

		u = userlist_find_by_real(list, uid, cid);
		if (u) {
			if (u->muted) {
				audio_level_set(a, 0, 0);
			}
			u->active_audio = audio_level(a) > 0;
			if (u->muted && (u->active_audio != u->active_prev)) {
				*list_changed = true;
			}
		}
	}

	info("userlist(%p): update_audio_level list changed: %s\n",
	     list,
	     *list_changed ? "YES" : "NO");
}

int userlist_debug(struct re_printf *pf,
		   const struct userlist* list)
{
	char userid_anon[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct le *le = NULL;
	struct userinfo *u = NULL;
	int err = 0;

	LIST_FOREACH(&list->users, le) {
		u = le->data;
		err = re_hprintf(pf,
				 "user hash: %s user: %s.%s "
				 "incall: %s auth: %s subc: %s "
				 "ssrca: %u ssrcv: %u "
				 "muted: %s auactive: %s vidstate: %s\n",
				 anon_id(userid_anon, u->userid_hash),
				 anon_id(userid_anon2, u->userid_real),
				 anon_client(clientid_anon, u->clientid_real),
				 u->incall_now ? "true" : "false",
				 u->se_approved ? "true" : "false",
				 u->in_subconv ? "true" : "false",
				 u->ssrca, u->ssrcv,
				 u->muted ? "true" : "false",
				 u->active_audio ? "true" : "false",
				 icall_vstate_name(u->video_state));
		if (err)
			goto out;
	}

out:
	return err;
}

int userlist_reset_video_states(const struct userlist *list)
{
	struct le *le = NULL;

	if (!list)
		return EINVAL;

	LIST_FOREACH(&list->users, le) {
		struct userinfo *u = le->data;

		if (u && u->incall_now &&
		    u->video_state != ICALL_VIDEO_STATE_STOPPED) {
			if (list->vstateh)
				list->vstateh(u, ICALL_VIDEO_STATE_STOPPED, list->arg);

			u->video_state = ICALL_VIDEO_STATE_STOPPED;
		}
	}

	return 0;
}

int userlist_get_active_counts(const struct userlist *list,
			       uint32_t *active,
			       uint32_t *active_a,
			       uint32_t *active_v)
{
	struct le *le;
	struct userinfo *u;
	uint32_t ap = 1, aa = 0, av = 0;

	if (!list || !active || !active_a || !active_v)
		return EINVAL;

	LIST_FOREACH(&list->users, le) {
		u = le->data;

		if (u && u->se_approved && u->incall_now) {
			ap++;
			if (u->ssrca > 0 && !u->muted)
				aa++;

			if (u->ssrcv > 0 && u->video_state != ICALL_VIDEO_STATE_STOPPED)
				av++;
		}
	}

	*active = ap;
	*active_a = aa;
	*active_v = av;

	return 0;
}

