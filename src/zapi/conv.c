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

#include <re.h>
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_jzon.h"
#include "avs_uuid.h"
#include "avs_string.h"
#include "avs_zapi.h"
#include "generic.h"



static int message_decode(struct json_object *jdata, struct zapi_message *msg)
{
	msg->content = jzon_str(jdata, "content");
	msg->nonce = jzon_str(jdata, "nonce");
	return 0;
}


static int knock_decode(struct json_object *jdata, struct zapi_knock *knock)
{
	knock->nonce = jzon_str(jdata, "nonce");
	return 0;
}


static int hot_knock_decode(struct json_object *jdata,
			    struct zapi_hot_knock *knock)
{
	knock->nonce = jzon_str(jdata, "nonce");
	knock->ref = jzon_str(jdata, "ref");
	return 0;
}

static int data_decode(struct json_object *jdata, struct zapi_event *ev)
{
	if (strcaseeq(ev->type, "conversation.message-add"))
		return message_decode(jdata, &ev->data.message);
	else if (strcaseeq(ev->type, "conversation.knock"))
		return knock_decode(jdata, &ev->data.knock);
	else if (strcaseeq(ev->type, "conversation.hot-knock"))
		return hot_knock_decode(jdata, &ev->data.hot_knock);
	else
		return EPROTO;
}


int zapi_event_decode(struct json_object *jobj, struct zapi_event *ev)
{
	struct json_object *jdata;
	int err;

	if (!jobj || !ev)
		return EINVAL;

	ev->conversation = jzon_str(jobj, "conversation");
	ev->type = jzon_str(jobj, "type");
	ev->from = jzon_str(jobj, "from");
	ev->id = jzon_str(jobj, "id");
	ev->time = jzon_str(jobj, "time");

	err = jzon_object(&jdata, jobj, "data");
	if (err)
		return err;
	
	return data_decode(jdata, ev);
}


int zapi_conversation_put_self(struct rest_cli *cli, const char *conv,
			       const char *last_read, const bool *muted,
			       const char *archived,
			       zapi_error_h *errh, void *arg)
{
	struct zapi_error_data *data;
	struct json_object *jobj = NULL;
	struct json_object *inner;
	struct rest_req *rr = NULL;
	char *jstr = NULL;
	int err;

	if (!cli || !conv)
		return EINVAL;

	err = zapi_error_data_alloc(&data, errh, arg);
	if (err)
		goto out;

	jobj = json_object_new_object();
	if (!jobj) {
		err = ENOMEM;
		goto out;
	}

	if (last_read) {
		inner = json_object_new_string(last_read);
		if (!inner) {
			err = ENOMEM;
			goto out;
		}
		json_object_object_add(jobj, "last_read", inner);
	}
	if (muted) {
		inner = json_object_new_boolean(*muted);
		if (!inner) {
			err = ENOMEM;
			goto out;
		}
		json_object_object_add(jobj, "muted", inner);
	}
	if (archived) {
		inner = json_object_new_string(archived);
		if (!inner) {
			err = ENOMEM;
			goto out;
		}
		json_object_object_add(jobj, "archived", inner);
	}

	err = rest_req_alloc(&rr, zapi_error_handler, data, "PUT",
			     "/conversations/%s/self", conv);
	if (err)
		goto out;

	err = jzon_encode(&jstr, jobj);
	if (err)
		goto out;
	err = rest_req_add_body(rr, "application/json",
				jstr);
	if (err)
		goto out;

	err = rest_req_start(NULL, rr, cli, 0);
	if (err)
		goto out;

 out:
	mem_deref(jstr);
	if (err)
		mem_deref(data);
	mem_deref(jobj);

	return err;
}


int zapi_conversation_post_message(struct rest_cli *cli, const char *conv,
				   const char *content, const char *nonce,
			   	   zapi_error_h *errh, void *arg)
{
	char *uuid = NULL;
	struct rest_req *rr = NULL;
	struct zapi_error_data *data;
	int err;

	if (!nonce) {
		err = uuid_v4(&uuid);
		if (err)
			return err;
		nonce = uuid;
	}

	err = zapi_error_data_alloc(&data, errh, arg);
	if (err)
		goto out;

	err = rest_req_alloc(&rr, zapi_error_handler, data, "POST",
			     "/conversations/%s/messages", conv);
	if (err)
		goto out;

	err = rest_req_add_json(rr, "ss", "content", content, "nonce", nonce);
	if (err)
		goto out;

	err = rest_req_start(NULL, rr, cli, 0);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(data);
	mem_deref(uuid);
	return err;
}
