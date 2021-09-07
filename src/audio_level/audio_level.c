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
#include "avs_log.h"
#include "avs_string.h"
#include "avs_jzon.h"
#include "avs_audio_level.h"


#define LEVEL_K 0.1f // 0.3f


struct audio_level {
	char *userid;
	char *clientid;
	uint8_t aulevel;
	uint8_t aulevel_smooth;

	bool is_self;

	struct le le;
};

static void destructor(void *arg)
{
	struct audio_level *level = arg;

	mem_deref(level->userid);
	mem_deref(level->clientid);
}


int audio_level_alloc(struct audio_level **levelp,
		      struct list *levell, bool is_self,
		      const char *userid, const char *clientid,
		      uint8_t aulevel, uint8_t aulevel_smooth)
{
	struct audio_level *level;
	int err;

	if (!levelp)
		return EINVAL;

	level = mem_zalloc(sizeof(*level), destructor);
	if (!level)
		return ENOMEM;

	err = str_dup(&level->userid, userid);
	err |= str_dup(&level->clientid, clientid);
	if (err)
		goto out;

	level->aulevel = aulevel;
	level->aulevel_smooth = aulevel_smooth;
	level->is_self = is_self;

	if (levell)
		list_append(levell, &level->le, level);

 out:
	if (err)
		mem_deref(level);

	return err;
}


int audio_level(struct audio_level *a)
{
	if (!a)
		return 0;
	
	return a->aulevel;
}

bool audio_level_eq(struct audio_level *a, struct audio_level *b)
{
	return streq(a->userid, b->userid)
	    && streq(a->clientid, b->clientid);
}

int audio_level_json(struct list *levell,
		     const char *userid_self, const char *clientid_self,
		     char **json_str, char **anon_str)
{
	struct json_object *jobj;
	struct json_object *jarr;
	char uid_anon[ANON_ID_LEN];
	char cid_anon[ANON_CLIENT_LEN];
	struct mbuf *pmb = NULL;
	int err = 0;
	struct le *le;

	if (!levell || !json_str)
		return EINVAL;
	
	jobj = jzon_alloc_object();
	if (!jobj)
		return ENOMEM;

	jarr = jzon_alloc_array();
	if (!jarr) {
		err = ENOMEM;
		goto out;
	}

	if (anon_str) {
		pmb = mbuf_alloc(512);
		mbuf_printf(pmb, "%zu levels: ", list_count(levell));
	}
	
	LIST_FOREACH(levell, le) {
		struct audio_level *a = le->data;
		struct json_object *ja;
		const char *userid = a->userid;
		const char *clientid = a->clientid;

		if (a->is_self) {
			if (userid_self)
				userid = userid_self;
			if (clientid_self)
				clientid = clientid_self;
		}

		ja = jzon_alloc_object();
		if (ja) {
			jzon_add_str(ja, "userid", "%s", userid);
			jzon_add_str(ja, "clientid", "%s", clientid);
			jzon_add_int(ja, "audio_level",
				     (int32_t)a->aulevel_smooth);
			jzon_add_int(ja, "audio_level_now",
				     (int32_t)a->aulevel);
		}
		json_object_array_add(jarr, ja);

		/* add to info string */
		if (pmb) {
			anon_id(uid_anon, userid);
			anon_client(cid_anon, clientid);
			mbuf_printf(pmb, "{[%s.%s] audio_level: %d/%d}",
				    uid_anon, cid_anon,
				    a->aulevel_smooth, a->aulevel);
			if (le != levell->tail)
				mbuf_printf(pmb, ",");
		}		
	}
	json_object_object_add(jobj, "audio_levels", jarr);

	if (pmb) {
		pmb->pos = 0;
		mbuf_strdup(pmb, anon_str, pmb->end);
		mem_deref(pmb);
	}

	jzon_encode(json_str, jobj);
 out:	
	mem_deref(jobj);

	return err;
}


int audio_level_json_print(struct re_printf *pf, const struct audio_level *a)
{
	int err = 0;

	if (!a)
		return 0;
	
	err |= re_hprintf(pf, "{ userid: %s, clientid: %s, level: %d }",
			  a->userid, a->clientid, a->aulevel);

	return err;
}

uint8_t audio_level_smoothen(uint8_t level, uint8_t new_level)
{
	return (uint8_t)(LEVEL_K * (float)new_level
			 + (1.0 - LEVEL_K) * (float)level);
	
}

uint8_t audio_level_smoothen_withk(uint8_t level, uint8_t new_level, float k)
{
	return (uint8_t)(k * (float)new_level + (1.0 - k) * (float)level);
	
}



bool audio_level_list_cmp(struct le *le1, struct le *le2, void *arg)
{
	struct audio_level *au1 = list_ledata(le1);
	struct audio_level *au2 = list_ledata(le2);

	if (!au1 || !au2)
		return true;
	
	return au1->aulevel_smooth >= au2->aulevel_smooth;
}


int audio_level_list_debug(struct re_printf *pf, const struct list *levell)
{
	struct le *le;
	int err = 0;

	LIST_FOREACH(levell, le) {
		struct audio_level *a = le->data;

		err |= re_hprintf(pf, "\t%H\n", audio_level_json, a);
	}

	return err;
}
