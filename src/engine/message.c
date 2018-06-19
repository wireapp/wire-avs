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
#include "avs_ztime.h"
#include "avs_engine.h"
#include "module.h"
#include "engine.h"
#include "message.h"
#include "event.h"
#include "user.h"
#include "conv.h"
#include "utils.h"


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

struct otr_context {
	engine_missing_client_h *missingh;
	engine_status_h *resph;
	void *arg;
};


static bool missing_user_handler(struct le *le, void *arg)
{
	const struct odict_entry *ue;
	struct otr_context *ctx = arg;
	const char *userid;
	size_t count, i;

	if (!ctx || !ctx->missingh) {
		return true;
	}
	ue = (const struct odict_entry*)le->data;
	if (!ue) {
		warning("engine: missing_user_handler user has no data\n");
		return false;
	}
	userid = ue->key;

	count = odict_count(ue->u.odict, false);

	for (i=0; i<count; i++) {
		const struct odict_entry *ce;
		char key[16];

		re_snprintf(key, sizeof(key), "%zu", i);

		ce = odict_lookup(ue->u.odict, key);
		if (!ce || ce->type != ODICT_STRING)
			continue;

		ctx->missingh(userid, ce->u.str, ctx->arg);
		
	}

	return false;
}

static void otr_resp_handler(int err, const struct http_msg *msg,
			     struct mbuf *mb, struct json_object *jobj,
			     void *arg)
{
	struct otr_context *ctx = arg;

	if (err) {
		warning("engine: otr message failed (%m)\n", err);
		goto out;
	}

	if (msg->scode == 412) {
		struct odict *dict;
		const struct odict_entry *missing;

		dict = jzon_get_odict(jobj);
		missing  = odict_lookup(dict, "missing");

		list_apply(&missing->u.odict->lst, true, missing_user_handler, ctx);

		err = EAGAIN;
	}
	else if (msg->scode == 408 || (msg->scode >= 500 && msg->scode < 600)) {
		/* Server error, retry */
		warning("engine: otr message failed (%u %r) retrying\n",
			msg->scode, &msg->reason);
		err = EAGAIN;
	}
	else if (msg->scode >= 300) {
		warning("engine: otr message failed (%u %r)\n",
			msg->scode, &msg->reason);

		re_printf("%H\n", jzon_encode_odict_pretty,
			  jzon_get_odict(jobj));

		err = EPROTO;
		goto out;
	}

	info("engine: OTR message response: %u %r\n",
	     msg->scode, &msg->reason);

#if 0
	re_printf("%H\n", http_msg_print, msg);
	re_printf("%b\n", mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

 out:
	if (ctx->resph)
		ctx->resph(err, ctx->arg);

	mem_deref(ctx);
}


static void client_msg_destructor(void *arg)
{
	struct client_msg *msg = arg;

	mem_deref(msg->cipher);
}


static void recipient_msg_destructor(void *arg)
{
	struct recipient_msg *rmsg = arg;

	list_flush(&rmsg->msgl);
}


int engine_client_msg_alloc(struct client_msg **msgp, struct list *msgl)
{
	struct client_msg *msg;

	if (!msgp)
		return EINVAL;

	msg = mem_zalloc(sizeof(*msg), client_msg_destructor);
	if (!msg)
		return ENOMEM;

	if (msgl)
		list_append(msgl, &msg->le, msg);
	
	*msgp = msg;
	
	return 0;
}


int engine_recipient_msg_alloc(struct recipient_msg **rmsgp)
{
	struct recipient_msg *rmsg;

	if (!rmsgp)
		return EINVAL;

	rmsg = mem_zalloc(sizeof(*rmsg), recipient_msg_destructor);
	if (!rmsg)
		return ENOMEM;

	*rmsgp = rmsg;
	
	return 0;
}


int engine_send_message(struct engine_conv *conv,
			    const char *sender_clientid,
			    struct list *msgl,
			    bool transient,
			    bool ignore_missing,
			    engine_status_h *resph,
			    engine_missing_client_h *missingh,
			    void *arg)
{
	struct json_object *jobj, *recipients, *map;
	struct otr_context *ctx;
	struct le *le;
	int priority = 0;
	int err;

	if (!conv || !sender_clientid || !msgl)
		return EINVAL;

	ctx = mem_zalloc(sizeof(*ctx), NULL);
	ctx->resph = resph;
	ctx->missingh = missingh;
	ctx->arg = arg;

	jobj       = json_object_new_object();
	recipients = json_object_new_object();

	for(le = msgl->head; le != NULL; le = le->next) {
		struct recipient_msg *rmsg = le->data;
		struct le *rle;

		map = json_object_new_object();
		for(rle = rmsg->msgl.head; rle; rle = rle->next) {
			const struct client_msg *cm = rle->data;
			
			err = jzon_add_base64(map, cm->clientid,
					      cm->cipher, cm->cipher_len);
			if (err)
				goto out;
		}
		json_object_object_add(recipients, rmsg->userid, map);
	}
	
	/* Build the outer object */
	err = jzon_add_str(jobj, "sender", sender_clientid);
	if (err)
		goto out;

	json_object_object_add(jobj, "recipients", recipients);

	err = jzon_add_bool(jobj, "transient", transient);
	if (err)
		goto out;

#if 0
	re_printf("sending OTR message:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	err = rest_request_jobj(NULL, conv->engine->rest, priority,
				"POST", otr_resp_handler, ctx, jobj,
				"/conversations/%s/otr/messages%s", conv->id,
				ignore_missing ? "?ignore_missing=true" : "");
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(ctx);
	mem_deref(jobj);

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


/*** conversation.otr-message-add events
 */

static void conv_otr_message_add_handler(struct engine *engine,
					 const char *type,
					 struct json_object *jobj,
					 bool catchup)
{
	const char *conv_id;
	struct engine_conv *conv;
	const char *from_id, *time;
	struct engine_user *from;
	struct json_object *jdata;
	const char *text, *sender, *recipient;
	struct le *le;
	uint8_t *buf = NULL;
	size_t buf_len;
	struct ztime zt;
	int err;

#if 0
	re_printf("receive OTR message:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	conv_id = jzon_str(jobj, "conversation");
	if (!conv_id) {
		warning("engine: otr-message: entry \"conversation\""
			" is missing\n");
		return;
	}

	err = engine_lookup_conv(&conv, engine, conv_id);
	if (err) {
		info("engine: otr-message in unknown conversation"
			" '%s'.\n", conv_id);
		return;
	}

	from_id = jzon_str(jobj, "from");
	if (!from_id) {
		warning("engine: otr-message: entry \"from\""
			" is missing\n");
		return;
	}

	err = engine_lookup_user(&from, engine, from_id, true);
	if (err) {
		warning("engine: otr-message from unknown user"
			" '%s'.\n", from_id);
		return;
	}

	err = jzon_object(&jdata, jobj, "data");
	if (err) {
		warning("engine: otr-message: entry \"data\""
			" is missing\n");
		return;
	}

	time = jzon_str(jobj, "time");
	if (!time) {
		warning("engine: otr-message: entry \"time\""
			" is missing\n");
		return;
	}

	err = ztime_decode(&zt, time);
	if (err) {
		warning("engine: otr-message: could not decode timestamp"
			" (%s)\n", time);
		goto out;
	}

	text      = jzon_str(jdata, "text");
	sender    = jzon_str(jdata, "sender");
	recipient = jzon_str(jdata, "recipient");
	if (!text || !sender || !recipient) {
		warning("engine: otr-message: entry \"text\""
			" or \"sender\" or \"recipient\" is missing\n");
		return;
	}

	buf_len = str_len(text) * 3 / 4;
	buf = mem_alloc(buf_len, NULL);
	if (!buf)
		return;

	err = base64_decode(text, str_len(text), buf, &buf_len);
	if (err) {
		warning("engine: otr-message: base64_decode failed "
			" (%zu bytes) (%m)\n", str_len(text), err);
		goto out;
	}

	/*
	 * relay the incoming message to all interrested parties
	 */
	LIST_FOREACH(&engine->lsnrl, le) {
		struct engine_lsnr *lsnr = le->data;

		if (lsnr->otraddmsgh) {
			lsnr->otraddmsgh(conv, from, &zt, buf, buf_len,
					 sender, recipient, lsnr->arg);
		}
	}

 out:
	mem_deref(buf);
}

static struct engine_event_lsnr conv_otr_message_add_lsnr = {
	.type = "conversation.otr-message-add",
	.eventh = conv_otr_message_add_handler
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
	engine_event_register(&conv_otr_message_add_lsnr);
	engine_event_register(&any_event_lsnr);
	return 0;
}


/*** engine_message_module
 */

struct engine_module engine_message_module = {
	.name = "message",
	.inith = init_handler,
};
