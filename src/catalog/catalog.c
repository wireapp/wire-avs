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
	struct catalog_entry *self;
	struct list peers;
};


static void entry_destructor(void *arg)
{
	struct catalog_entry *e = arg;

	list_unlink(&e->le);
	mem_deref(e->userid);
	mem_deref(e->clientid);
	mem_deref(e->session_id);
	mem_deref(e->audio_track);
	mem_deref(e->video_track);
	mem_deref(e->dc_name);
}


int catalog_entry_alloc(struct catalog_entry **ep,
			const char *userid,
			const char *clientid)
{
	struct catalog_entry *e;
	int err;

	if (!ep || !userid || !clientid)
		return EINVAL;

	e = mem_zalloc(sizeof(*e), entry_destructor);
	if (!e)
		return ENOMEM;

	err = str_dup(&e->userid, userid);
	if (!err)
		err = str_dup(&e->clientid, clientid);

	if (err)
		mem_deref(e);
	else
		*ep = e;

	return err;
}


static int replace_str(char **dst, const char *src)
{
	char *s = NULL;
	int err;

	if (src) {
		err = str_dup(&s, src);
		if (err)
			return err;
	}
	mem_deref(*dst);
	*dst = s;

	return 0;
}


int catalog_entry_set_session(struct catalog_entry *e,
			      const char *session_id,
			      const char *audio_track,
			      const char *video_track,
			      const char *dc_name)
{
	int err;

	if (!e)
		return EINVAL;

	err = replace_str(&e->session_id, session_id);
	if (!err)
		err = replace_str(&e->audio_track, audio_track);
	if (!err)
		err = replace_str(&e->video_track, video_track);
	if (!err)
		err = replace_str(&e->dc_name, dc_name);

	return err;
}


/* Copy everything except list membership and owner data */
static int entry_copy(struct catalog_entry *dst,
		      const struct catalog_entry *src)
{
	int err;

	err = catalog_entry_set_session(dst, src->session_id,
					src->audio_track, src->video_track,
					src->dc_name);
	if (err)
		return err;

	dst->joined  = src->joined;
	dst->version = src->version;
	dst->keysync = src->keysync;
	dst->vstate  = src->vstate;
	dst->muted   = src->muted;

	return 0;
}


static const struct {
	enum catalog_msg_type type;
	const char *name;
} msg_names[] = {
	{CATALOG_MSG_HELLO,   "hello"},
	{CATALOG_MSG_WELCOME, "welcome"},
	{CATALOG_MSG_UPDATE,  "update"},
	{CATALOG_MSG_BYE,     "bye"},
};


const char *catalog_msg_name(enum catalog_msg_type type)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(msg_names); ++i) {
		if (msg_names[i].type == type)
			return msg_names[i].name;
	}

	return "???";
}


static int msg_type_from_name(enum catalog_msg_type *typep, const char *name)
{
	size_t i;

	if (!name)
		return EPROTO;

	for (i = 0; i < ARRAY_SIZE(msg_names); ++i) {
		if (streq(msg_names[i].name, name)) {
			*typep = msg_names[i].type;
			return 0;
		}
	}

	return EPROTO;
}


int catalog_encode(char **jsonp,
		   enum catalog_msg_type type,
		   const char *catalog_id,
		   const struct catalog_entry *e)
{
	struct json_object *jobj;
	int err = 0;

	if (!jsonp || !catalog_id || !e)
		return EINVAL;

	jobj = jzon_alloc_object();
	if (!jobj)
		return ENOMEM;

	err |= jzon_add_str(jobj, "type", "%s", catalog_msg_name(type));
	err |= jzon_add_str(jobj, "catalogId", "%s", catalog_id);
	err |= jzon_add_str(jobj, "userid", "%s", e->userid);
	err |= jzon_add_str(jobj, "clientid", "%s", e->clientid);
	err |= jzon_add_str(jobj, "joined", "%llu", (unsigned long long)e->joined);
	err |= jzon_add_int(jobj, "version", (int32_t)e->version);

	if (type != CATALOG_MSG_BYE) {
		if (e->session_id)
			err |= jzon_add_str(jobj, "sessionId", "%s", e->session_id);
		if (e->audio_track)
			err |= jzon_add_str(jobj, "audio", "%s", e->audio_track);
		if (e->video_track)
			err |= jzon_add_str(jobj, "video", "%s", e->video_track);
		if (e->dc_name)
			err |= jzon_add_str(jobj, "dc", "%s", e->dc_name);
		err |= jzon_add_int(jobj, "keysync", (int32_t)e->keysync);
		err |= jzon_add_int(jobj, "vstate", e->vstate);
		err |= jzon_add_bool(jobj, "muted", e->muted);
	}
	if (err) {
		err = ENOMEM;
		goto out;
	}

	err = jzon_encode(jsonp, jobj);

 out:
	mem_deref(jobj);
	return err;
}


int catalog_decode(enum catalog_msg_type *typep,
		   struct catalog_entry **ep,
		   const char *catalog_id,
		   const char *json,
		   size_t len)
{
	struct json_object *jobj = NULL;
	struct catalog_entry *e = NULL;
	enum catalog_msg_type type;
	const char *cid;
	const char *joined;
	struct pl pl;
	int v;
	int err;

	if (!typep || !ep || !catalog_id || !json)
		return EINVAL;

	err = jzon_decode(&jobj, json, len);
	if (err)
		return EPROTO;

	err = msg_type_from_name(&type, jzon_str(jobj, "type"));
	if (err)
		goto out;

	cid = jzon_str(jobj, "catalogId");
	if (!cid) {
		err = EPROTO;
		goto out;
	}
	if (!streq(cid, catalog_id)) {
		err = EBADMSG;
		goto out;
	}

	err = catalog_entry_alloc(&e, jzon_str(jobj, "userid"),
				  jzon_str(jobj, "clientid"));
	if (err) {
		err = err == EINVAL ? EPROTO : err;
		goto out;
	}

	joined = jzon_str(jobj, "joined");
	if (!joined) {
		err = EPROTO;
		goto out;
	}
	pl_set_str(&pl, joined);
	e->joined = pl_u64(&pl);

	err = jzon_int(&v, jobj, "version");
	if (err) {
		err = EPROTO;
		goto out;
	}
	e->version = (uint32_t)v;

	if (type != CATALOG_MSG_BYE) {
		err = catalog_entry_set_session(e,
						jzon_str(jobj, "sessionId"),
						jzon_str(jobj, "audio"),
						jzon_str(jobj, "video"),
						jzon_str(jobj, "dc"));
		if (err)
			goto out;

		if (!e->session_id) {
			err = EPROTO;
			goto out;
		}

		jzon_int_opt(&v, jobj, "keysync", 0);
		e->keysync = (uint32_t)v;
		jzon_int_opt(&e->vstate, jobj, "vstate", 0);
		e->muted = jzon_bool_opt(jobj, "muted", false);
	}

	*typep = type;
	*ep = e;
	e = NULL;

 out:
	mem_deref(e);
	mem_deref(jobj);
	return err;
}


static void destructor(void *arg)
{
	struct catalog *cat = arg;

	list_flush(&cat->peers);
	mem_deref(cat->self);
	mem_deref(cat->catalog_id);
}


int catalog_alloc(struct catalog **catp, const char *catalog_id,
		  const char *userid, const char *clientid)
{
	struct catalog *cat;
	int err;

	if (!catp || !catalog_id || !userid || !clientid)
		return EINVAL;

	cat = mem_zalloc(sizeof(*cat), destructor);
	if (!cat)
		return ENOMEM;

	list_init(&cat->peers);
	err = str_dup(&cat->catalog_id, catalog_id);
	if (!err)
		err = catalog_entry_alloc(&cat->self, userid, clientid);

	if (err)
		mem_deref(cat);
	else
		*catp = cat;

	return err;
}


const char *catalog_id(const struct catalog *cat)
{
	return cat ? cat->catalog_id : NULL;
}


struct catalog_entry *catalog_self(struct catalog *cat)
{
	return cat ? cat->self : NULL;
}


void catalog_self_changed(struct catalog *cat)
{
	if (cat)
		cat->self->version++;
}


struct catalog_entry *catalog_find(const struct catalog *cat,
				   const char *userid,
				   const char *clientid)
{
	struct le *le;

	if (!cat || !userid || !clientid)
		return NULL;

	LIST_FOREACH(&cat->peers, le) {
		struct catalog_entry *e = le->data;

		if (streq(e->userid, userid) && streq(e->clientid, clientid))
			return e;
	}

	return NULL;
}


/* true if a is newer than b */
static bool is_newer(const struct catalog_entry *a,
		     const struct catalog_entry *b)
{
	if (a->joined != b->joined)
		return a->joined > b->joined;

	return a->version > b->version;
}


int catalog_apply(struct catalog *cat,
		  enum catalog_msg_type type,
		  const struct catalog_entry *e,
		  bool *changedp,
		  struct catalog_entry **entryp)
{
	struct catalog_entry *cur;
	int err = 0;

	if (!cat || !e || !changedp)
		return EINVAL;

	*changedp = false;
	if (entryp)
		*entryp = NULL;

	if (streq(e->userid, cat->self->userid) &&
	    streq(e->clientid, cat->self->clientid))
		return 0;

	cur = catalog_find(cat, e->userid, e->clientid);

	if (type == CATALOG_MSG_BYE) {
		/* A bye of an earlier join must not remove a rejoin */
		if (cur && e->joined >= cur->joined) {
			mem_deref(cur);
			*changedp = true;
		}
		return 0;
	}

	if (cur) {
		if (!is_newer(e, cur)) {
			if (entryp)
				*entryp = cur;
			return 0;
		}
	}
	else {
		err = catalog_entry_alloc(&cur, e->userid, e->clientid);
		if (err)
			return err;
		list_append(&cat->peers, &cur->le, cur);
	}

	err = entry_copy(cur, e);
	if (err) {
		mem_deref(cur);
		return err;
	}

	*changedp = true;
	if (entryp)
		*entryp = cur;

	return 0;
}


void catalog_remove(struct catalog *cat, struct catalog_entry *e)
{
	(void)cat;

	mem_deref(e);
}


void catalog_flush(struct catalog *cat)
{
	if (cat)
		list_flush(&cat->peers);
}


struct list *catalog_peers(struct catalog *cat)
{
	return cat ? &cat->peers : NULL;
}


static int entry_debug(struct re_printf *pf, const struct catalog_entry *e)
{
	return re_hprintf(pf, "%s.%s sess=%s a=%s v=%s dc=%s joined=%llu "
			  "ver=%u keysync=%u vstate=%d muted=%d\n",
			  e->userid, e->clientid,
			  e->session_id ? e->session_id : "-",
			  e->audio_track ? e->audio_track : "-",
			  e->video_track ? e->video_track : "-",
			  e->dc_name ? e->dc_name : "-",
			  (unsigned long long)e->joined, e->version,
			  e->keysync, e->vstate, e->muted);
}


int catalog_debug(struct re_printf *pf, const struct catalog *cat)
{
	struct le *le;
	int err = 0;

	if (!cat)
		return 0;

	err |= re_hprintf(pf, "catalog %s self: ", cat->catalog_id);
	err |= entry_debug(pf, cat->self);
	LIST_FOREACH(&cat->peers, le) {
		err |= re_hprintf(pf, "\tpeer: ");
		err |= entry_debug(pf, le->data);
	}

	return err;
}
