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


struct zapi_error_data {
	zapi_error_h *errh;
	void *arg;
};


void zapi_error_handler(int err, const struct http_msg *msg, struct mbuf *mb,
			struct json_object *jobj, void *arg)
{
	struct zapi_error_data *data = arg;
	int code;
	const char *message;
	const char *label;

	if (err || !msg) {
		if (data->errh)
			data->errh(err, 0, NULL, NULL, data->arg);
		goto out;
	}

	if (!jobj) {
		if (data->errh)
			data->errh(err, msg->scode, NULL, NULL, data->arg);
		goto out;
	}

	err = jzon_int(&code, jobj, "code");
	if (err)
		code = 0;
	message = jzon_str(jobj, "message");
	label = jzon_str(jobj, "label");
	
	if (data->errh)
		data->errh(err, code, message, label, data->arg);

 out:
	mem_deref(data);
}


int zapi_error_data_alloc(struct zapi_error_data **datap, zapi_error_h *errh,
			  void *arg)
{
	struct zapi_error_data *data;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->errh = errh;
	data->arg = arg;

	*datap = data;

	return 0;
}


