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
#include <avs_audio_level.h>

static void cm_destructor(void *arg)
{
	struct conf_member *cm = (struct conf_member *)arg;

	list_unlink(&cm->le);

	mem_deref(cm->userid);
	mem_deref(cm->clientid);
	mem_deref(cm->userid_hash);
	mem_deref(cm->label);
	mem_deref(cm->cname);
	mem_deref(cm->msid);
	mem_deref(cm->uinfo);
}


int conf_member_alloc(struct conf_member **cmp,
		      struct list *membl,
		      struct iflow *flow,
		      const char *userid,
		      const char *clientid,
		      const char *userid_hash,
		      uint32_t ssrca,
		      uint32_t ssrcv,
		      const char *label)
{
	struct conf_member *cm;
	int err = 0;

	cm = (struct conf_member *)mem_zalloc(sizeof(*cm), cm_destructor);
	if (!cm)
		return ENOMEM;

	cm->flow = flow;
	cm->ssrca = ssrca;
	cm->ssrcv = ssrcv;
	cm->active = true;
	err = str_dup(&cm->userid, userid);
	err |= str_dup(&cm->clientid, clientid);
	err |= str_dup(&cm->userid_hash, userid_hash);
	err |= str_dup(&cm->cname, label);
	err |= str_dup(&cm->msid, label);				
	err |= str_dup(&cm->label, label);				
	if (err)
		goto out;

	list_append(membl, &cm->le, cm);
 out:
	if (err)
		mem_deref(cm);
	else
		*cmp = cm;

	return err;
}


struct conf_member *conf_member_find_by_userclient(struct list *membl,
						   const char *userid,
						   const char *clientid)
{
	struct conf_member *cm;	
	bool found = false;
	struct le *le;

	for(le = membl->head; !found && le; le = le->next) {
		cm = (struct conf_member *)le->data;

		found = streq(cm->userid, userid)
			&& streq(cm->clientid, clientid);
	}

	return found ? cm : NULL;
}

struct conf_member *conf_member_find_active_by_userclient(struct list *membl,
							  const char *userid,
							  const char *clientid)
{
	struct conf_member *cm;
	bool found = false;
	struct le *le;

	for(le = membl->head; !found && le; le = le->next) {
		cm = (struct conf_member *)le->data;

		found = streq(cm->userid, userid)
			&& streq(cm->clientid, clientid)
			&& cm->active;
	}

	return found ? cm : NULL;
}


struct conf_member *conf_member_find_by_label(struct list *membl,
					      const char *label)
{
	struct conf_member *cm;	
	bool found = false;
	struct le *le;

	for(le = membl->head; !found && le; le = le->next) {
		cm = (struct conf_member *)le->data;

		found = streq(cm->label, label);
	}

	return found ? cm : NULL;
}

static struct conf_member *find_ssrc(struct list *membl,
				     uint32_t ssrc, bool audio)
{
	struct conf_member *cm;	
	bool found = false;
	struct le *le;

	for(le = membl->head; !found && le; le = le->next) {
		cm = (struct conf_member *)le->data;

		found = audio ? ssrc == cm->ssrca : ssrc == cm->ssrcv;
	}

	return found ? cm : NULL;
	
}


struct conf_member *conf_member_find_by_ssrca(struct list *membl,
					      uint32_t ssrc)
{
	return find_ssrc(membl, ssrc, true);
}


struct conf_member *conf_member_find_by_ssrcv(struct list *membl,
					      uint32_t ssrc)
{
	return find_ssrc(membl, ssrc, false);
}
	
	
void conf_member_set_audio_level(struct conf_member *cm, int level)
{
	if (!cm)
		return;

	cm->audio_level = level > AUDIO_LEVEL_FLOOR ? level : 0;
	if (level > AUDIO_LEVEL_FLOOR)
		cm->audio_level_smooth = AUDIO_LEVEL_CEIL;
	else if (cm->audio_level_smooth > 0)
		cm->audio_level_smooth--;		
}
