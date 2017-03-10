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
#include "avs_zapi.h"
#include "generic.h"


/*** zapi_connection_encode
 */

int zapi_connection_encode(struct json_object *jobj,
			   const struct zapi_connection *conn)
{
	if (!jobj || !conn)
		return EINVAL;

	if (conn->status)
		json_object_object_add(jobj, "status",
				json_object_new_string(conn->status));
	if (conn->conversation)
		json_object_object_add(jobj, "conversation",
				json_object_new_string(conn->conversation));
	if (conn->to)
		json_object_object_add(jobj, "to",
				json_object_new_string(conn->to));
	if (conn->from)
		json_object_object_add(jobj, "from",
				json_object_new_string(conn->from));
	if (conn->last_update)
		json_object_object_add(jobj, "last_update",
				json_object_new_string(conn->last_update));
	if (conn->message)
		json_object_object_add(jobj, "message",
				json_object_new_string(conn->message));

	return 0;
}


/*** zapi_connection_decode
 */

int zapi_connection_decode(struct json_object *jobj,
			   struct zapi_connection *conn)
{
	if (!jobj || !conn)
		return EINVAL;

	conn->status = jzon_str(jobj, "status");
	conn->conversation = jzon_str(jobj, "conversation");
	conn->to = jzon_str(jobj, "to");
	conn->from = jzon_str(jobj, "from");
	conn->last_update = jzon_str(jobj, "last_update");
	conn->message = jzon_str(jobj, "message");

	return 0;
}


/*** zapi_connection_apply
 */

struct connection_apply_data {
	struct rest_cli *cli;
	int pri;
	zapi_connection_h *connh;
	void *arg;
};


static void connection_apply_handler(int err, const struct http_msg *msg,
				     struct mbuf *mb,
				     struct json_object *jobj, void *arg)
{
	struct connection_apply_data *data = arg;
	int count, i;
	struct zapi_connection conn;

	err = rest_err(err, msg);
	if (err)
		goto out;

	if (!jzon_is_array(jobj)) {
		err = EPROTO;
		goto out;
	}

	conn.to = NULL;
	count = json_object_array_length(jobj);
	for (i = 0; i < count; ++i) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jobj, i);
		if (!jitem)
			continue;

		err = zapi_connection_decode(jitem, &conn);
		if (err)
			goto out;

		if (data->connh(0, &conn, data->arg)) {
			err = ENOENT;
			goto out;
		}
	}

	if (conn.to) {
		err = rest_get(NULL, data->cli, data->pri,
			       connection_apply_handler, data,
			       "/connections?start=%s", conn.to);
		if (err)
			goto out;
	}

 out:
	if (err) {
		if (err != ENOENT)
			data->connh(err, NULL, data->arg);
		mem_deref(data);
	}
}


int zapi_connection_apply(struct rest_cli *cli, int pri,
			  zapi_connection_h *connh, void *arg)
{
	struct connection_apply_data *data;
	int err;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->cli = cli;
	data->pri = pri;
	data->connh = connh;
	data->arg = arg;

	err = rest_get(NULL, cli, pri, connection_apply_handler, data,
		       "/connections");
	if (err)
		mem_deref(data);
	return err;
}


/*** zapi_connection_update
 */

int zapi_connection_update(struct rest_cli *cli, int pri, const char *id,
			   const struct zapi_connection *conn,
			   zapi_error_h *errh, void *arg)
{
	struct json_object *jobj;
	struct zapi_error_data *data;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	err = zapi_connection_encode(jobj, conn);
	if (err)
		goto out;

	if (errh) {
		err = zapi_error_data_alloc(&data, errh, arg);
		if (err)
			goto out;
	}
	else
		data = NULL;

	err = rest_request_jobj(NULL, cli, pri, "PUT",
				data ? zapi_error_handler : NULL, data, jobj,
				"/connections/%s", id);
	if (err)
		goto out;
	
 out:
	mem_deref(jobj);
	if (err)
		mem_deref(data);
	return err;
}

