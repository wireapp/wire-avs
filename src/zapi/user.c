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


/*** zapi_user_encode
 */

int zapi_user_encode(struct json_object *jobj, const struct zapi_user *user)
{
	if (!jobj || !user)
		return EINVAL;

	if (user->email)
		json_object_object_add(jobj, "email",
				       json_object_new_string(user->email));
	if (user->phone)
		json_object_object_add(jobj, "phone",
				       json_object_new_string(user->phone));
	if (user->accent_id > -1)
		json_object_object_add(jobj, "accent_id",
				       json_object_new_int(user->accent_id));
	if (user->name)
		json_object_object_add(jobj, "name",
				       json_object_new_string(user->name));
	if (user->id)
		json_object_object_add(jobj, "id",
				       json_object_new_string(user->id));

	return 0;
}


/*** zapi_user_decode
 */

int zapi_user_decode(struct json_object *jobj, struct zapi_user *user)
{
	int err;

	if (!jobj || !user)
		return EINVAL;

	user->email = jzon_str(jobj, "email");
	user->phone = jzon_str(jobj, "phone");
	err = jzon_int(&user->accent_id, jobj, "accent_id");
	if (err)
		user->accent_id = -1;
	user->name = jzon_str(jobj, "name");
	user->id = jzon_str(jobj, "id");

	return 0;
}


/*** zapi_user_get
 */

struct user_get_data {
	zapi_user_h *userh;
	void *arg;
};


static void user_get_handler(int err, const struct http_msg *msg,
			     struct mbuf *mb, struct json_object *jobj,
			     void *arg)
{
	struct user_get_data *data = arg;
	struct zapi_user user;

	err = rest_err(err, msg);
	if (err)
		goto out;

	if (!jzon_is_object(jobj)) {
		err = EPROTO;
		goto out;
	}

	err = zapi_user_decode(jobj, &user);
	if (err)
		goto out;

	if (data)
		data->userh(0, &user, data->arg);

 out:
	if (err && data)
		data->userh(err, NULL, data->arg);
	mem_deref(data);
}


int zapi_user_get(struct rest_cli *cli, int pri, const char *id,
		  zapi_user_h *userh, void *arg)
{
	struct user_get_data *data;
	int err;

	if (userh) {
		data = mem_zalloc(sizeof(*data), NULL);
		if (!data)
			return ENOMEM;
		
		data->userh = userh;
		data->arg = arg;
	}
	else
		data = NULL;

	err = rest_get(NULL, cli, pri, user_get_handler, data,
		       "/users/%s", id);

	if (err)
		mem_deref(data);
	return err;
}

