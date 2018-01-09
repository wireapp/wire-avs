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
/* libavs -- simple sync engine
 *
 * Conversation management
 */

#include <re.h>
#include "avs_dict.h"
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_store.h"
#include "avs_string.h"
#include "avs_engine.h"
#include "module.h"
#include "engine.h"
#include "sync.h"
#include "event.h"
#include "user.h"
#include "call.h"
#include "utils.h"
#include "conv.h"

#define ENGINE_USER_DEFAULT_SELF_NAME "You"


struct engine_conv_data {
	struct dict *convd;
	struct engine_lsnr user_lsnr;
};


/*** trigger events
 */

static void send_add_conv(struct engine_conv *conv)
{
	struct le *le;

	LIST_FOREACH(&conv->engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->addconvh)
			lsnr->addconvh(conv, lsnr->arg);
	}
}


void engine_send_conv_update(struct engine_conv *conv,
			     enum engine_conv_changes changes)
{
	struct le *le;

	LIST_FOREACH(&conv->engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->convupdateh)
			lsnr->convupdateh(conv, changes, lsnr->arg);
	}
}


/*** struct engine_conv
 */

static void engine_conv_destructor(void *arg)
{
	struct engine_conv *conv = arg;

	mem_deref(conv->id);
	mem_deref(conv->name);
	list_flush(&conv->memberl);
	mem_deref(conv->last_event);
	mem_deref(conv->last_read);
	mem_deref(conv->arg);
}


static int conv_alloc(struct engine_conv **convp, struct engine *engine,
		      const char *id)
{
	struct engine_conv *conv;
	int err;

	conv = mem_zalloc(sizeof(*conv), engine_conv_destructor);
	if (!conv)
		return ENOMEM;

	err = str_dup(&conv->id, id);
	if (err)
		goto out;

	conv->engine = engine;

	err = dict_add(engine->conv->convd, id, conv);
	if (err)
		goto out;

	/* Ownership goes to convd.  */
	mem_deref(conv);

	*convp = conv;

 out:
	if (err)
		mem_deref(conv);
	return err;
}


/*** engine_save_conv
 */

int engine_save_conv(struct engine_conv *conv)
{
	struct sobject *so;
	struct le *le;
	int err;

	if (!conv)
		return EINVAL;

	if (!conv->engine->store)
		return 0;

	err = store_user_open(&so, conv->engine->store, "conv", conv->id,
			      "wb");
	if (err)
		return err;

	err = sobject_write_u8(so, conv->type);
	if (err)
		goto out;

	err = sobject_write_lenstr(so, conv->name);
	if (err)
		goto out;

	err = sobject_write_u32(so, list_count(&conv->memberl));
	if (err)
		goto out;

	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		err = sobject_write_lenstr(so, mbr->user->id);
		if (err)
			goto out;

		err = sobject_write_u8(so, mbr->active | (0 << 1));
		if (err)
			goto out;

		err = sobject_write_dbl(so, .0);
		if (err)
			goto out;
	}

	err = sobject_write_u8(so,   (conv->active)
				   | (conv->archived << 1)
				   | (conv->muted << 2)
				   | ((0 & 0x03) << 3)
			           | (0 << 5)
				   | (0 << 6));
	if (err)
		goto out;

	err = sobject_write_lenstr(so, conv->last_event);
	if (err)
		goto out;
	err = sobject_write_lenstr(so, conv->last_read);
	if (err)
		goto out;

 out:
	if (err) {
		warning("Writing conversation '%s' failed: %m.\n", conv->id,
			err);
	}
	mem_deref(so);
	return err;
}


/*** load conversation
 */

static int load_member(struct engine_conv_member **mbrp,
		       struct engine *engine, struct sobject *so)
{
	struct engine_conv_member *mbr;
	double quality;
	char *user_id;
	uint8_t v8;
	int err;

	mbr = mem_zalloc(sizeof(*mbr), NULL);
	if (!mbr)
		return ENOMEM;

	err = sobject_read_lenstr(&user_id, so);
	if (err)
		goto out;

	err = engine_lookup_user(&mbr->user, engine, user_id, true);
	if (err)
		goto out;

	err = sobject_read_u8(&v8, so);
	if (err)
		goto out;
	mbr->active = v8 & 0x01;

	(void)sobject_read_dbl(&quality, so);
	(void)quality;

	*mbrp = mbr;

 out:
	if (err)
		mem_deref(mbr);
	mem_deref(user_id);
	return err;
}


static int load_conv(struct engine_conv *conv)
{
	struct sobject *so;
	char *dst;
	uint8_t v8;
	uint32_t cnt, i;
	int err;

	err = store_user_open(&so, conv->engine->store, "conv", conv->id,
			      "rb");
	if (err)
		return err;

	err = sobject_read_u8(&v8, so);
	if (err)
		goto out;
	conv->type = v8;

	err = sobject_read_lenstr(&dst, so);
	if (err)
		goto out;
	mem_deref(conv->name);
	conv->name = dst;

	err = sobject_read_u32(&cnt, so);
	if (err)
		goto out;

	for (i = 0; i < cnt; ++i) {
		struct engine_conv_member *mbr = NULL;

		err = load_member(&mbr, conv->engine, so);
		if (err)
			goto out;
		list_append(&conv->memberl, &mbr->le, mbr);
	}

	err = sobject_read_u8(&v8, so);
	if (err)
		goto out;
	conv->active = v8 & (1 << 0);
	conv->archived = v8 & (1 << 1);
	conv->muted = v8 & (1 << 2);

	err = sobject_read_lenstr(&dst, so);
	if (err)
		goto out;
	mem_deref(conv->last_event);
	conv->last_event = dst;

	err = sobject_read_lenstr(&dst, so);
	if (err)
		goto out;
	mem_deref(conv->last_read);
	conv->last_read = dst;

	engine_update_conv_unread(conv);

 out:
	mem_deref(so);
	return err;
}


/*** update conversation
 */

static enum engine_conv_type decode_conv_type(struct json_object *jobj,
					      const char *key)
{
	int i;
	int err;

	err = jzon_int(&i, jobj, key);
	if (err)
		return ENGINE_CONV_UNKNOWN;

	switch (i) {
	case 0:
		return ENGINE_CONV_REGULAR;
	case 1:
		return ENGINE_CONV_SELF;
	case 2:
		return ENGINE_CONV_ONE;
	case 3:
		return ENGINE_CONV_CONNECT;
	default:
		return ENGINE_CONV_UNKNOWN;
	}
}


static bool decode_conv_status(struct json_object *jobj, const char *key)
{
	int i;
	int err;

	err = jzon_int(&i, jobj, key);
	if (err)
		return true;

	return i != 1;
}


static bool decode_conv_archived(struct json_object *jobj, const char *key)
{
	return jzon_str(jobj, key) != NULL;
}


static bool decode_conv_muted(struct json_object *jobj, const char *key)
{
	bool res = false;

	jzon_bool(&res, jobj, key);
	return res;
}


static bool find_member_handler(struct le *le, void *arg)
{
	struct engine_user *user = arg;
	struct engine_conv_member *mbr = le->data;

	return user == mbr->user;
}


static int update_member(struct engine_conv *conv, struct json_object *jobj,
			 enum engine_conv_changes *changes)
{
	const char *user_id;
	struct engine_user *user;
	struct le *le;
	struct engine_conv_member *mbr;
	bool b;
	int err;

	user_id = jzon_str(jobj, "id");
	if (!user_id)
		return EPROTO;

	err = engine_lookup_user(&user, conv->engine, user_id, true);
	if (err)
		return err;

	le = list_apply(&conv->memberl, true, find_member_handler, user);
	if (le)
		mbr = le->data;
	else {
		mbr = mem_zalloc(sizeof(*mbr), NULL);
		if (!mbr)
			return ENOMEM;

		mbr->user = user;

		list_append(&conv->memberl, &mbr->le, mbr);
		*changes |= ENGINE_CONV_MEMBERS;
	}

	b = decode_conv_status(jobj, "status");
	if (b != mbr->active) {
		mbr->active = b;
		*changes |= ENGINE_CONV_MEMBERS;
	}

	return err;
}


static int update_conv(struct engine_conv *conv, struct json_object *jconv,
		       bool existing)
{
	enum engine_conv_changes changes = 0;
	enum engine_conv_type type;
	struct json_object *members, *self, *others;
	const char *name;
	const char *id;
	bool b;
	int otherc, i;
	int err;

	type = decode_conv_type(jconv, "type");
	if (type == ENGINE_CONV_UNKNOWN)
		return EPROTO;
	if (type != conv->type) {
		conv->type = type;
		changes |= ENGINE_CONV_TYPE;
	}

	id = jzon_str(jconv, "last_event");
	if (id && (!conv->last_event || !streq(id, conv->last_event))) {
		err = engine_str_repl(&conv->last_event, id);
		if (err)
			return err;
		changes |= ENGINE_CONV_LAST_EVENT;
	}

	err = jzon_object(&members, jconv, "members");
	if (err)
		return err;

	err = jzon_object(&self, members, "self");
	if (err)
		return err;

	b = decode_conv_status(self, "status");
	if (b != conv->active) {
		conv->active = b;
		changes |= ENGINE_CONV_ACTIVE;
	}
	b = decode_conv_archived(self, "archived");
	if (b != conv->archived) {
		conv->archived = b;
		changes |= ENGINE_CONV_ARCHIVED;
	}
	b = decode_conv_muted(self, "muted");
	if (b != conv->muted) {
		conv->muted = b;
		changes |= ENGINE_CONV_MUTED;
	}

	id = jzon_str(self, "last_read");
	if (id && (!conv->last_read || !streq(id, conv->last_read))) {
		err = engine_str_repl(&conv->last_read, id);
		if (err)
			return err;
		changes |= ENGINE_CONV_LAST_READ;
	}

	engine_update_conv_unread(conv);

	err = jzon_array(&others, members, "others");
	if (err)
		return err;

	otherc = json_object_array_length(others);
	for (i = 0; i < otherc; ++i) {
		struct json_object *jmbr;

		jmbr = json_object_array_get_idx(others, i);
		if (!jmbr)
			continue;

		err = update_member(conv, jmbr, &changes);
		if (err)
			return err;
	}

	/* On self and one-on-one, we ignore the service provided name.
	 */
	name = jzon_str(jconv, "name");
	if (name && conv->type != ENGINE_CONV_ONE
	    && conv->type != ENGINE_CONV_SELF
	    && (!conv->name || !streq(conv->name, name)))
	{
		char *dst = NULL;
		err = str_dup(&dst, name);
		if (err)
			return err;
		changes |= ENGINE_CONV_NAME;
		mem_deref(conv->name);
		conv->name = dst;
	}

	if (conv->type == ENGINE_CONV_ONE) {
		if (conv->memberl.head) {
			struct engine_conv_member *mbr =
				conv->memberl.head->data;
			mbr->user->conv = conv;
		}
	}

	if (changes)
		engine_save_conv(conv);
	if (existing) {
		if (changes)
			engine_send_conv_update(conv, changes);
	}
	else
		send_add_conv(conv);

	return 0;
}


/*** engine_update_conv_unread
 */

void engine_update_conv_unread(struct engine_conv *conv)
{
	if (!conv->last_read || !conv->last_event)
		conv->unread = true;
	else
		conv->unread = !streq(conv->last_read, conv->last_event);
}


/*** engine_lookup_conv
 */

int engine_lookup_conv(struct engine_conv **convp, struct engine *engine,
		       const char *id)
{
	struct engine_conv *conv;

	conv = dict_lookup(engine->conv->convd, id);
	if (conv) {
		*convp = conv;
		return 0;
	}
	else
		return ENOENT;
}


static int lookup_or_create_conv(struct engine_conv **convp,
				 struct engine *engine, const char *id,
				 bool *existing)
{
	struct engine_conv *conv;
	int err;

	conv = dict_lookup(engine->conv->convd, id);
	if (conv) {
		*convp = conv;
		if (existing)
			*existing = true;
		return 0;
	}

	err = conv_alloc(convp, engine, id);
	if (err)
		return err;
	if (existing)
		*existing = false;
	return 0;
}


/*** engine_fetch_conv
 */

static struct engine_conv *import_conv(struct engine *engine,
				       struct json_object *jobj)
{
	const char *id;
	struct engine_conv *conv;
	bool existing;
	int err;

	id = jzon_str(jobj, "id");
	if (!id)
		return NULL;

	err = lookup_or_create_conv(&conv, engine, id, &existing);
	if (err)
		return NULL;
	update_conv(conv, jobj, existing);

	info("conv: import: %H\n", engine_conv_debug, conv);

	return conv;
}


static void fetch_conv_handler(int err, const struct http_msg *msg,
			       struct mbuf *mb, struct json_object *jobj,
			       void *arg)
{
	struct engine *engine = arg;

	err = engine_rest_err(err, msg);
	if (err) {
		info("fetching conversation failed: %m.\n", err);
		return;
	}

	import_conv(engine, jobj);
}


int engine_fetch_conv(struct engine *engine, const char *id)
{
	return rest_get(NULL, engine->rest, 0, fetch_conv_handler, engine,
		        "/conversations/%s", id);
}


/* Handling assets */

struct asset_elem {
	struct engine *engine;
	char *aid;
	char *path;
};

static void asset_elem_destructor(void *arg)
{
	struct asset_elem *elem = arg;

	mem_deref(elem->aid);
	mem_deref(elem->path);
}

static void get_asset_handler(int err, const struct http_msg *msg,
			      struct mbuf *mb, struct json_object *jobj,
			      void *arg)
{
	struct asset_elem *elem = arg;
	FILE *fp;
	size_t n;

	debug("get_asset_handler: len=%zu err=%d status=%d\n",
	      mb->end, err, msg ? msg->scode : 0);

	if (err || (msg->scode < 200 || msg->scode >= 300)) {
		warning("get_asset_handler: failed: err=%d status=%d\n",
			err, msg ? msg->scode : 0);
		goto out;
	}

	fp = fopen(elem->path, "wb");
	if (!fp) {
		error("get_asset_handler: failed to open file: %s: %m\n",
		      elem->path, errno);
		goto out;
	}

	n = fwrite(mb->buf, 1, mb->end, fp);
	fclose(fp);

	info("%zu bytes of asset: %s written to %s\n",
	     n, elem->aid, elem->path);

 out:
	mem_deref(elem);
}


static void asset_loc_handler(int err, const struct http_msg *msg,
			      struct mbuf *mb, struct json_object *jobj,
			      void *arg)
{
	struct asset_elem *elem = arg;
	const struct http_hdr *hdr;
	struct rest_req *rr;
	char *location;

	if (err) {
		info("fetching asset failed: %m\n", err);
		goto out;
	}

	if (msg->scode != 302) {
		info("fetching asset not redirecting\n");
		goto out;
	}

	hdr = http_msg_hdr(msg, HTTP_HDR_LOCATION);
	if (!hdr) {
		info("fetching asset has no location\n");
		goto out;
	}

	err = pl_strdup(&location, &hdr->val);
	if (err) {
		info("fetching asset cannot parse location\n");
		goto out;
	}

	info("Fetching asset from: %s\n", location);
	err = rest_req_alloc(&rr, get_asset_handler, elem, "GET",
			     "%s", location);
	err |= rest_req_set_raw(rr, true);
	err |= rest_req_start(NULL, rr, elem->engine->rest, 0);

	mem_deref(location);

	if (err) {
		warning("asset_loc_handler: rest_get failed\n");
		goto out;
	}

	return;

 out:
	mem_deref(elem);
}


int engine_fetch_asset(struct engine *engine,
		       const char *cid, const char *aid, const char *path)
{
	struct asset_elem *elem;

	elem = mem_zalloc(sizeof(*elem), asset_elem_destructor);
	if (!elem)
		return ENOMEM;

	elem->engine = engine;
	str_dup(&elem->aid, aid);
	str_dup(&elem->path, path);

	return rest_get(NULL, engine->rest, 0, asset_loc_handler,
			elem, "/conversations/%s/assets/%s", cid, aid);
}


/*** engine_apply_convs
 */

struct apply_convs_data {
	engine_conv_apply_h *applyh;
	void *arg;
};


static bool apply_convs_handler(char *key, void *val, void *arg)
{
	struct apply_convs_data *data = arg;
	struct engine_conv *conv = val;

	return data->applyh(conv, data->arg);
}


struct engine_conv *engine_apply_convs(struct engine *engine,
				       engine_conv_apply_h *applyh,
				       void *arg)
{
	struct apply_convs_data data;

	if (!engine || !engine->conv || !applyh)
		return NULL;

	data.applyh = applyh;
	data.arg = arg;

	return dict_apply(engine->conv->convd, apply_convs_handler, &data);
}


/*** engine_print_conv_name
 */

static int print_name_regular(struct re_printf *pf, struct engine_conv *conv)
{
	struct le *le;
	int err;

	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		err = re_hprintf(pf, "%s", mbr->user->display_name);
		if (err)
			return err;

		if (le->next) {
			err = re_hprintf(pf, ", ");
			if (err)
				return err;
		}
	}

	return 0;
}


static int print_name_self(struct re_printf *pf, struct engine_conv *conv)
{
	struct engine_user *self = engine_get_self(conv->engine);

	if (!self || !self->name) {
		return re_hprintf(pf, ENGINE_USER_DEFAULT_SELF_NAME);
	}
	else
		return re_hprintf(pf, "%s", self->name);
}

static int print_name_one(struct re_printf *pf, struct engine_conv *conv)
{
	struct engine_conv_member *mbr;

	if (list_isempty(&conv->memberl)) {
		/* Quietly don't print anything.  */
		return 0;
	}

	mbr = conv->memberl.head->data;
	return re_hprintf(pf, "%s", mbr->user->display_name);
}


static int print_name_unknown(struct re_printf *pf, struct engine_conv *conv)
{
	/* XXX Do something more meaningful.
	 */
	return re_hprintf(pf, "Huh?");
}

int engine_print_conv_name(struct re_printf *pf, struct engine_conv *conv)
{
	if (!pf || !conv)
		return EINVAL;

	if (conv->name) {
		return re_hprintf(pf, "%s", conv->name);
	}
	else {
		switch (conv->type) {

		case ENGINE_CONV_REGULAR:
			return print_name_regular(pf, conv);
		case ENGINE_CONV_SELF:
			return print_name_self(pf, conv);
		case ENGINE_CONV_ONE:
		case ENGINE_CONV_CONNECT:
			return print_name_one(pf, conv);
		default:
			return print_name_unknown(pf, conv);
		}
	}
}


/*** user update handler
 */

static bool user_name_update_handler(char *key, void *val, void *arg)
{
	struct engine_user *user = arg;
	struct engine_conv *conv = val;
	struct le *le;

	if (conv->name)
		return false;

	le = list_apply(&conv->memberl, true, find_member_handler, user);
	if (!le)
		return false;

	LIST_FOREACH(&conv->engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->convupdateh)
			lsnr->convupdateh(conv, ENGINE_CONV_NAME, lsnr->arg);
	}
	return false;
}


static void user_update_handler(struct engine_user *user,
				enum engine_user_changes changes,
				void *arg)
{
	(void) arg;

	if (!(changes & ENGINE_USER_NAME))
		return;

	/* Send a name update for all conversations with no name and the
	 * user as a member.
	 */
	dict_apply(user->engine->conv->convd, user_name_update_handler,
		   user);
}


/*** conversation.create events
 */

static void conv_create_handler(struct engine *engine, const char *type,
				struct json_object *jobj, bool catchup)
{
	struct json_object *jdata;
	const char *id;
	struct engine_conv *conv;
	bool existing;
	int err;

	(void) type;
	(void) catchup;

	err = jzon_object(&jdata, jobj, "data");
	if (err) {
		info("broken conversation.create event: %m.\n", err);
		return;
	}

	id = jzon_str(jdata, "id");
	if (!id) {
		info("conversation.create event without id.\n");
		return;
	}

	err = lookup_or_create_conv(&conv, engine, id, &existing);
	if (err) {
		info("conversation.create: failed to created conv: %m.\n",
		     err);
		return;
	}

	err = update_conv(conv, jdata, existing);
	if (err) {
		/* XXX We should probably do something. Drop the
		 *     conversation?
		 */
		info("conversation.create: failed to update conv: %m.\n",
		     err);
		return;
	}
}

static struct engine_event_lsnr conv_create_lsnr = {
	.type = "conversation.create",
	.eventh = conv_create_handler
};


static void conv_asset_add_handler(struct engine *engine, const char *type,
				   struct json_object *jobj, bool catchup)
{
	struct json_object *jdata;
	const char *id;
	const char *ctype;
	const char *aid;
	struct engine_conv *conv;
	bool existing;
	int err;

	(void) type;
	(void) catchup;

	err = jzon_object(&jdata, jobj, "data");
	if (err) {
		info("broken conversation.asset-add event: %m.\n", err);
		return;
	}

	id = jzon_str(jobj, "conversation");
	if (!id) {
		info("conversation.asset-add event without id.\n");
		return;
	}

	err = lookup_or_create_conv(&conv, engine, id, &existing);
	if (err) {
		info("conversation.asset-add: failed to created conv: %m.\n",
		     err);
		return;
	}

	ctype = jzon_str(jdata, "content_type");
	aid = jzon_str(jdata, "id");
	info("conversation: [%s] added %s asset: %s\n",
	     conv->name, ctype, aid);
}


static struct engine_event_lsnr conv_asset_add_lsnr = {
	.type = "conversation.asset-add",
	.eventh = conv_asset_add_handler
};


/*** conversation.rename events
 */

static void conv_rename_handler(struct engine *engine, const char *type,
				struct json_object *jobj, bool catchup)
{
	const char *id;
	struct engine_conv *conv;
	char *dst;
	int err;

	(void) type;
	(void) catchup;

	id = jzon_str(jobj, "id");
	if (!id) {
		info("conversation.rename without id.\n");
		return;
	}

	err = engine_lookup_conv(&conv, engine, id);
	if (err) {
		info("conversation.rename for unknown conversation '%s'.\n",
		     id);
		return;
	}

	if (conv->type == ENGINE_CONV_ONE || conv->type == ENGINE_CONV_SELF)
		return;

	err = jzon_strdup(&dst, jobj, "name");
	if (err)
		return;

	mem_deref(conv->name);
	conv->name = dst;
	engine_send_conv_update(conv, ENGINE_CONV_NAME);
}

static struct engine_event_lsnr conv_rename_lsnr = {
	.type = "conversation.rename",
	.eventh = conv_rename_handler
};


/*** conversation.member-join and -leave
 *
 * We just refetch the conversation to be sure.
 */

static void get_conversation_handler(int err, const struct http_msg *msg,
				     struct mbuf *mb,
				     struct json_object *jobj, void *arg)
{
	struct engine_conv *conv = arg;

	if (err || (msg && msg->scode >= 300))
		return;

	update_conv(conv, jobj, false);
}


static void conv_refetch_handler(struct engine *engine, const char *type,
			         struct json_object *jobj, bool catchup)
{
	const char *id;
	struct engine_conv *conv;
	int err;

	id = jzon_str(jobj, "conversation");
	if (!id)
		return;

	err = engine_lookup_conv(&conv, engine, id);
	if (err == ENOENT) {
		/* member-join also happens when you are added to an
		 * existing conversation
		 */
		engine_fetch_conv(engine, id);
		return;
	}
	if (err)
		return;

	rest_get(NULL, engine->rest, 0, get_conversation_handler, conv,
		 "/conversations/%s", id);
}


static struct engine_event_lsnr conv_member_join_lsnr = {
	.type = "conversation.member-join",
	.eventh = conv_refetch_handler
};

static struct engine_event_lsnr conv_member_leave_lsnr = {
	.type = "conversation.member-leave",
	.eventh = conv_refetch_handler
};


/*** conversation.member-update handler
 */

static int update_last_read(struct engine_conv *conv, const char *ev,
			    enum engine_conv_changes *changes)
{
	int err;

	if (!conv->last_read || !streq(conv->last_read, ev)) {
		err = engine_str_repl(&conv->last_read, ev);
		if (err)
			return err;

		engine_update_conv_unread(conv);

		*changes |= ENGINE_CONV_LAST_READ;
	}

	return 0;
}


static int update_muted(struct engine_conv *conv, bool muted,
			enum engine_conv_changes *changes)
{
	if (conv->muted != muted) {
		conv->muted = muted;

		*changes |= ENGINE_CONV_MUTED;
	}

	return 0;
}


static int update_archived(struct engine_conv *conv, const char *archived,
			   enum engine_conv_changes *changes)
{
	if (streq(archived, "false") && conv->archived) {
		conv->archived = false;
		*changes |= ENGINE_CONV_ARCHIVED;
	}
	else if (!conv->archived) {
		conv->archived = true;
		*changes |= ENGINE_CONV_ARCHIVED;
	}

	return 0;
}


static void conv_member_update_handler(struct engine *engine,
				       const char *type,
				       struct json_object *jobj, bool catchup)
{
	const char *id;
	struct engine_conv *conv;
	struct json_object *jdata;
	const char *ev;
	bool muted;
	enum engine_conv_changes changes = 0;
	int err;

	id = jzon_str(jobj, "conversation");
	if (!id)
		return;

	err = engine_lookup_conv(&conv, engine, id);
	if (err)
		return;

	err = jzon_object(&jdata, jobj, "data");
	if (err)
		return;

	ev = jzon_str(jdata, "last_read");
	if (ev) {
		err = update_last_read(conv, ev, &changes);
		if (err)
			return;
	}

	err = jzon_bool(&muted, jdata, "muted");
	if (!err) {
		err = update_muted(conv, muted, &changes);
		if (err)
			return;
	}

	ev = jzon_str(jdata, "archived");
	if (ev) {
		err = update_archived(conv, ev, &changes);
		if (err)
			return;
	}

	if (changes) {
		engine_save_conv(conv);
		engine_send_conv_update(conv, changes);
	}
}

static struct engine_event_lsnr conv_member_update_lsnr = {
	.type = "conversation.member-update",
	.eventh = conv_member_update_handler
};


/*** sync handler
 */

static void get_convlist_handler(int err, const struct http_msg *msg,
				 struct mbuf *mb, struct json_object *jobj,
				 void *arg)
{
	struct engine_sync_step *step = arg;
	struct json_object *carray;
	const char *id = NULL;
	int count, i;
	bool more = false;

	(void) mb;

	err = rest_err(err, msg);
	if (err) {
		warning("requesting conversation list failed: %m\n", err);
		goto out;
	}

	err = jzon_array(&carray, jobj, "conversations");
	if (err)
		goto out;

	count = json_object_array_length(carray);
	for (i = 0; i < count; ++i) {
		struct json_object *cobj;
		struct engine_conv *conv;

		cobj = json_object_array_get_idx(carray, i);
		if (cobj == NULL)
			continue;

		id = jzon_str(cobj, "id");
		conv = import_conv(step->engine, cobj);
	}

	err = jzon_bool(&more, jobj, "has_more");
	if (err || !more)
		goto out;
	if (!id) {
		more = false;
		goto out;
	}

	err = rest_get(NULL, step->engine->rest, 1, get_convlist_handler,
		       step, "/conversations?start=%s", id);
	if (err) {
		more = false;
		warning("requestion more conversations failed: %m\n",
			err);
		goto out;
	}

 out:
	if (!more) {
		engine_sync_next(step);
	}
}


static void sync_handler(struct engine_sync_step* step)
{
	int err;

	err = rest_get(NULL, step->engine->rest, 1, get_convlist_handler,
		       step, "/conversations");
	if (err) {
		error("sync error: failed to fetch conversation list (%m).\n",
		      err);
		engine_sync_next(step);
	}
}


/*** init handler
 */

static int init_handler(void)
{
	engine_event_register(&conv_create_lsnr);
	engine_event_register(&conv_asset_add_lsnr);
	engine_event_register(&conv_rename_lsnr);
	engine_event_register(&conv_member_join_lsnr);
	engine_event_register(&conv_member_leave_lsnr);
	engine_event_register(&conv_member_update_lsnr);
	return 0;
}


/*** alloc handler
 */

static void engine_conv_data_destructor(void *arg)
{
	struct engine_conv_data *data = arg;

	mem_deref(data->convd);
	engine_lsnr_unregister(&data->user_lsnr);
}


static int alloc_handler(struct engine *engine,
			 struct engine_module_state *state)
{
	struct engine_conv_data *data;
	int err;

	data = mem_zalloc(sizeof(*data), engine_conv_data_destructor);
	if (!data) {
		err = ENOMEM;
		goto out;
	}

	err = dict_alloc(&data->convd);
	if (err)
		goto out;

	engine->conv = data;

	data->user_lsnr.userh = user_update_handler;
	err = engine_lsnr_register(engine, &data->user_lsnr);
	if (err)
		goto out;

	list_append(&engine->modulel, &state->le, state);

	err = engine_sync_register(engine, "fetching conversations",
				   5., sync_handler);
	if (err)
		goto out;

 out:
	if (err) {
		mem_deref(state);
		mem_deref(data);
	}
	return err;
}


/*** startup handler
 */

static int conv_dir_handler(const char *id, void *arg)
{
	struct engine *engine = arg;
	struct engine_conv *conv;
	struct le *le;
	int err;

	err = conv_alloc(&conv, engine, id);
	if (err) {
		info("Loading conversation '%s' failed in creation: %m.\n",
		     id, err);
		engine->need_sync = true;
		return 0;
	}
	err = load_conv(conv);
	if (err) {
		info("Loading conversation '%s' failed: %m.\n", id, err);
		engine->need_sync = true;
		return 0;
	}

	LIST_FOREACH(&engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->addconvh)
			lsnr->addconvh(conv, lsnr->arg);
	}

	return 0;
}


static void startup_handler(struct engine *engine,
			    struct engine_module_state *state)
{
	int err;

	if (!engine->store) {
		err = ENOENT;
		goto out;
	}

	err = store_user_dir(engine->store, "conv", conv_dir_handler,
			     engine);
	if (err)
		goto out;

 out:
	if (err)
		engine->need_sync = true;
	state->state = ENGINE_STATE_ACTIVE;
	engine_active_handler(engine);
}


/*** close_handler
 */

static void close_handler(void)
{
}


int engine_conv_debug(struct re_printf *pf, const struct engine_conv *conv)
{
	struct le *le;
	int err = 0;

	if (!conv)
		return 0;
	
	err |= re_hprintf(pf, "CONV %p:\n", conv);
	err |= re_hprintf(pf, "ID:   %s\n", conv->id);
	err |= re_hprintf(pf, "Type: ");
	switch (conv->type) {
	case ENGINE_CONV_REGULAR:
		err |= re_hprintf(pf, "GROUP\n");
		break;
		
	case ENGINE_CONV_SELF:
		err |= re_hprintf(pf, "SELF\n");
		break;
		
	case ENGINE_CONV_ONE:
		err |= re_hprintf(pf, "1-1\n");
		break;
		
	case ENGINE_CONV_CONNECT:
		err |= re_hprintf(pf, "CONNECTING\n");
		break;
		
	default:
		err |= re_hprintf(pf, "???\n");
		break;
	}
	err |= re_hprintf(pf, "Members:\n");
	LIST_FOREACH(&conv->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		err |= re_hprintf(pf, "  %s %s", mbr->user->id,
				  mbr->user->display_name);
		if (!mbr->active)
			err |= re_hprintf(pf, " [left]");
		err |= re_hprintf(pf, "\n");
	}
	if (!conv->active)
		err |= re_hprintf(pf, "You have left the conversation.\n");
	if (conv->archived)
		err |= re_hprintf(pf, "You have archived the conversation.\n");
	if (conv->muted)
		err |= re_hprintf(pf, "You have muted the conversation.\n");

	err |= re_hprintf(pf, "Last event: %s\n", conv->last_event);
	err |= re_hprintf(pf, "Last read:  %s\n", conv->last_read);

	return err;	
}


/*** engine_conv_module
 */

struct engine_module engine_conv_module = {
	.name = "conv",
	.inith = init_handler,
	.alloch = alloc_handler,
	.startuph = startup_handler,
	.closeh = close_handler
};
