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
 * Message handling
 */

#include <sys/stat.h>

#include <re.h>
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_uuid.h"
#include "avs_string.h"
#include "avs_zapi.h"
#include "avs_engine.h"
#include "module.h"
#include "engine.h"
#include "event.h"
#include "user.h"
#include "conv.h"
#include "utils.h"

enum {
	ENGINE_MESSAGE_PAGE_SIZE = 100
};


/*** struct engine_msg
 */

static void engine_msg_text_destructor(struct engine_msg_text *text)
{
	mem_deref(text->content);
	mem_deref(text->nonce);
}


static void engine_msg_destructor(void *arg)
{
	struct engine_msg *msg = arg;

	list_unlink(&msg->le);
	mem_deref(msg->id);
	
	switch (msg->type) {
	case ENGINE_MSG_TEXT:
		engine_msg_text_destructor(&msg->data.text);
		break;
	default:
		break;
	}
}


static int import_msg_text(struct engine_msg_text *text,
			   struct engine *engine, struct json_object *jdata)
{
	int err;

	(void) engine;

	if (!jdata)
		return EPROTO;

	err = jzon_strdup(&text->content, jdata, "content");
	if (err)
		return err;
	err = jzon_strdup(&text->nonce, jdata, "nonce");
	if (err)
		return err;

	return 0;
}


static int import_msg(struct engine_msg **msgp, struct engine *engine,
		      struct json_object *jobj)
{
	struct engine_msg *msg;
	struct json_object *jdata = NULL;
	const char *type;
	const char *convid;
	const char *fromid;
	int err;
	
	msg = mem_zalloc(sizeof(*msg), engine_msg_destructor);
	if (!msg)
		return ENOMEM;

	type = jzon_str(jobj, "type");
	if (!type) {
		err = EPROTO;
		goto out;
	}

	jzon_object(&jdata, jobj, "data");

	convid = jzon_str(jobj, "conversation");
	if (!convid) {
		err = EPROTO;
		goto out;
	}
	err = engine_lookup_conv(&msg->conv, engine, convid);
	if (err)
		goto out;

	fromid = jzon_str(jobj, "from");
	if (!fromid) {
		err = EPROTO;
		goto out;
	}
	err = engine_lookup_user(&msg->from, engine, fromid, true);
	if (err)
		goto out;

	err = jzon_strdup(&msg->id, jobj, "id");
	if (err)
		goto out;

	if (streq(type, "conversation.message-add")) {
		msg->type = ENGINE_MSG_TEXT;
		err = import_msg_text(&msg->data.text, engine, jdata);
		if (err)
			goto out;
	}
	else {
		msg->type = ENGINE_MSG_UNKNOWN;
	}

	*msgp = msg;

out:
	if (err)
		mem_deref(msg);
	return err;
}


/*** engine_apply_messages
 */

struct apply_message_data {
	struct engine_conv *conv;
	bool forward;
	char end[64];
	engine_msg_apply_h *h;
	void *arg;
};


static void conversation_events_handler (int err, const struct http_msg *msg,
				         struct mbuf *mb,
					 struct json_object *jobj, void *arg);


static int page_forwards(struct engine_conv *conv, const char *start,
			 struct apply_message_data *data)
{
	if (start) {
		return rest_get(NULL, conv->engine->rest, 0,
				conversation_events_handler, data,
			        "/conversations/%s/events?size=%i"
			        "&start=%s&exclude_start=1", conv->id,
			        ENGINE_MESSAGE_PAGE_SIZE, start);
	}
	else {
		return rest_get(NULL, conv->engine->rest, 0,
				conversation_events_handler, data,
			        "/conversations/%s/events?size=%i",
			        conv->id, ENGINE_MESSAGE_PAGE_SIZE);
	}
}


static int page_backwards(struct engine_conv *conv, const char *start,
			  struct apply_message_data *data)
{
	if (start) {
		return rest_get(NULL, conv->engine->rest, 0,
				conversation_events_handler, data,
			        "/conversations/%s/events?size=-%i"
				"&start=%s&exclude_start=1&end=0.0",
				conv->id, ENGINE_MESSAGE_PAGE_SIZE, start);
	}
	else {
		return rest_get(NULL, conv->engine->rest, 0,
				conversation_events_handler, data,
			        "/conversations/%s/events?size=-%i"
				"&start=%s&end=0.0",
				conv->id, ENGINE_MESSAGE_PAGE_SIZE,
				conv->last_event);
	}
}


static void conversation_events_handler (int err, const struct http_msg *msg,
				         struct mbuf *mb,
					 struct json_object *jobj, void *arg)
{
	struct apply_message_data *data = arg;
	struct json_object *jevents;
	int count, i;
	const char *id = NULL;

	err = engine_rest_err(err, msg);
	if (err) {
		warning("engine_apply_messages failed: %m.\n", err);
		data->h(err, NULL, data->arg);
		goto out;
	}

	err = jzon_array(&jevents, jobj, "events");
	if (err) {
		warning("engine_apply_messages: "
			"no 'events' array in reply.\n");
		data->h(err, NULL, data->arg);
		goto out;
	}

	count = json_object_array_length(jevents);
	for (i = 0; i < count; ++i) {
		struct json_object *jitem;
		struct engine_msg *emsg;

		jitem = json_object_array_get_idx(jevents, i);
		if (jitem == NULL)
			continue;

		id = jzon_str(jitem, "id");
		err = import_msg(&emsg, data->conv->engine, jitem);
		if (err == ENOENT)
			continue;

		if (err) {
			data->h(err, NULL, data->arg);
			goto out;
		}

		if (data->h(0, emsg, data->arg)) {
			mem_deref(emsg);
			err = ENOENT;
			goto out;
		}
		else
			mem_deref(emsg);

		if (*data->end && streq(data->end, id)) {
			data->h(0, NULL, data->arg);
			err = ENOENT;
			goto out;
		}
	}

	if (id && jzon_bool_opt(jobj, "has_more", false)) {
		if (data->forward)
			err = page_forwards(data->conv, id, data);
		else
			err = page_backwards(data->conv, id, data);
	}
	else {
		data->h(0, NULL, data->arg);
		err = ENOENT;
	}

 out:
	if (err)
		mem_deref(data);
}


int engine_apply_messages(struct engine_conv *conv, bool forward,
			  const char *start, const char *end,
			  engine_msg_apply_h *h, void *arg)
{
	struct apply_message_data *data;

	if (!conv || !h)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->conv = conv;
	data->forward = forward;
	data->h = h;
	data->arg = arg;

	if (end)
		str_ncpy(data->end, end, sizeof(data->end));

	if (forward)
		return page_forwards(conv, start, data);
	else
		return page_backwards(conv, start, data);
}


/*** engine_set_last_read
 */

int engine_set_last_read(struct engine_conv *conv, const char *msg_id)
{
	int err;

	err = zapi_conversation_put_self(conv->engine->rest, conv->id,
					 msg_id, NULL, NULL, NULL, NULL);
	if (err)
		return err;

	err = engine_str_repl(&conv->last_read, msg_id);
	if (err)
		return err;

	engine_update_conv_unread(conv);

	return 0;
}

/*** engine_send_text_message
 */

int engine_send_text_message(struct engine_conv *conv, const char *msg)
{
	char *uuid = NULL;
	struct rest_req *rr = NULL;
	int err;
	
	err = uuid_v4(&uuid);
	if (err)
		return err;

	err = rest_req_alloc(&rr, NULL, NULL, "POST",
			     "/conversations/%s/messages", conv->id);
	if (err)
		goto out;

	err = rest_req_add_json(rr, "ss", "content", msg, "nonce", uuid);
	if (err)
		goto out;

	err = rest_req_start(NULL, rr, conv->engine->rest, 0);
	if (err)
		goto out;

 out:
	mem_deref(uuid);
	return err;
}


int engine_send_text_message_vf(struct engine_conv *conv, const char *msg,
				va_list ap)

{
	char *str;
	int err;

	err = re_vsdprintf(&str, msg, ap);
	if (err)
		return err;
	err = engine_send_text_message(conv, str);
	mem_deref(str);
	return err;
}


int engine_send_text_message_f(struct engine_conv *conv, const char *msg,
			       ...)
{
	va_list ap;
	int err;

	va_start(ap, msg);
	err = engine_send_text_message_vf(conv, msg, ap);
	va_end(ap);
	return err;
}

/*** engine_send_data
 */

int engine_send_data(struct engine_conv *conv, const char *ctype,
		     uint8_t *data, size_t len)
{
	char *uuid = NULL;
	struct rest_req *rr = NULL;
	int err;
	uint8_t dgst[MD5_SIZE];
	char dgst64[2*MD5_SIZE];
	size_t dgst64_len = sizeof(dgst64);
	char disp[128];

	if (!conv || !ctype || !data || !len)
		return EINVAL;
	
	err = uuid_v4(&uuid);
	if (err)
		return err;

	err = rest_req_alloc(&rr, NULL, NULL, "POST", "/assets");
	if (err)
		goto out;

	md5(data, len, dgst);
	base64_encode(dgst, sizeof(dgst), dgst64, &dgst64_len);
	re_snprintf(disp, sizeof(disp), "zasset;conv_id=%s;md5=%b",
		    conv->id, dgst64, dgst64_len);
	
	err = rest_req_add_header(rr, "Content-Disposition: %s\r\n", disp);
	err = rest_req_add_body_raw(rr, ctype, data, len);
	if (err)
		goto out;

	err = rest_req_start(NULL, rr, conv->engine->rest, 0);
	if (err)
		goto out;

 out:
	mem_deref(uuid);
	return err;
}


int engine_send_file(struct engine_conv *conv, const char *ctype,
		     const char *path)
{
	FILE *fp;
	uint8_t *buf;
	size_t len;
	struct stat st;
	int err = 0;

	if (!conv || !path)
		return EINVAL;

	if (stat(path, &st) < 0)
		return errno;
	
	fp = fopen(path, "rb");
	if (!fp)
		return errno;
	
	buf = mem_alloc(st.st_size*sizeof(*buf), NULL);
	if (!buf)
		return ENOMEM;

	len = fread(buf, sizeof(*buf), st.st_size, fp);
	if ((off_t)len < st.st_size) {
		err = errno;
		goto out;
	}
	
	engine_send_data(conv, ctype, buf, len);

 out:
	mem_deref(buf);
	return err;
}


/*** conversation.message-add events
 */

static void conv_message_add_handler(struct engine *engine, const char *type,
				     struct json_object *jobj, bool catchup)
{
	const char *conv_id;
	struct engine_conv *conv;
	const char *from_id;
	struct engine_user *from;
	struct json_object *jdata;
	const char *content;
	const char *event_id;
	struct le *le;
	int err;

	conv_id = jzon_str(jobj, "conversation");
	if (!conv_id)
		return;

	err = engine_lookup_conv(&conv, engine, conv_id);
	if (err) {
		info("message in unknown conversation '%s'.\n", conv_id);
		return;
	}

	event_id = jzon_str(jobj, "id");
	if (!event_id)
		return;

	from_id = jzon_str(jobj, "from");
	if (!from_id)
		return;

	err = engine_lookup_user(&from, engine, from_id, true);
	if (err)
		return;

	err = jzon_object(&jdata, jobj, "data");
	if (err)
		return;

	content = jzon_str(jdata, "content");
	if (!content)
		return;

	LIST_FOREACH(&engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->addmsgh) {
			lsnr->addmsgh(conv, from, event_id, content,
				      lsnr->arg);
		}
	}
}

struct engine_event_lsnr conv_message_add_lsnr = {
	.type = "conversation.message-add",
	.eventh = conv_message_add_handler
};


/*** Handle all events for updating last event.
 */

static void any_event_handler(struct engine *engine, const char *type,
			      struct json_object *jobj, bool catchup)
{
	const char *conv_id;
	const char *event_id;
	struct engine_conv *conv;
	int err;

	conv_id = jzon_str(jobj, "conversation");
	event_id = jzon_str(jobj, "id");

	if (!conv_id || !event_id)
		return;

	err = engine_lookup_conv(&conv, engine, conv_id);
	if (err) {
		info("message in unknown conversation '%s'.\n", conv_id);
		return;
	}

	if (!conv->last_event || streq(conv->last_event, event_id)) {
		err = engine_str_repl(&conv->last_event, event_id);
		if (err)
			return;
		engine_update_conv_unread(conv);
		engine_save_conv(conv);
		engine_send_conv_update(conv, ENGINE_CONV_LAST_EVENT);
	}
}

struct engine_event_lsnr any_event_lsnr = {
	.type = NULL,
	.eventh = any_event_handler
};


/*** init handler
 */

static int init_handler(void)
{
	engine_event_register(&conv_message_add_lsnr);
	engine_event_register(&any_event_lsnr);
	return 0;
}


/*** engine_message_module
 */

struct engine_module engine_message_module = {
	.name = "message",
	.inith = init_handler,
};
