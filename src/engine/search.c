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
 * Search
 */


#include <re.h>
#include "avs_rest.h"
#include "avs_jzon.h"
#include "avs_engine.h"
#include "engine.h"


/*** struct engine_user_search
 */

static void found_user_destructor(void *arg)
{
	struct engine_found_user *fuser = arg;

	mem_deref(fuser->email);
	mem_deref(fuser->phone);
	mem_deref(fuser->name);
	mem_deref(fuser->id);
}


static int load_found_user(struct engine_found_user **fuserp,
			   struct json_object *jobj)
{
	struct engine_found_user *fuser;
	int err;

	fuser = mem_zalloc(sizeof(*fuser), found_user_destructor);
	if (!fuser)
		return ENOMEM;

	err = jzon_strdup_opt(&fuser->email, jobj, "email", NULL);
	if (err)
		goto out;

	err = jzon_strdup_opt(&fuser->phone, jobj, "phone", NULL);
	if (err)
		goto out;

	fuser->connected = jzon_bool_opt(jobj, "connected", false);

	err = jzon_int_opt(&fuser->weight, jobj, "weight", -1);
	if (err)
		goto out;

	err = jzon_strdup(&fuser->name, jobj, "name");
	if (err)
		goto out;

	err = jzon_strdup(&fuser->id, jobj, "id");
	if (err)
		goto out;

	err = jzon_int_opt(&fuser->accent_id, jobj, "accent_id", -1);
	if (err)
		goto out;

	fuser->blocked = jzon_bool_opt(jobj, "blocked", false);

	err = jzon_int_opt(&fuser->level, jobj, "level", -1);
	if (err)
		goto out;

	*fuserp = fuser;

 out:
	if (err)
		mem_deref(fuser);

	return err;
}


/*** struct engine_user_search
 */

static void user_search_destructor(void *arg)
{
	struct engine_user_search *search = arg;

	list_flush(&search->userl);
}


static int load_user_search(struct engine_user_search **searchp,
			    struct json_object *jobj)
{
	struct engine_user_search *search;
	struct json_object *jdocs;
	int i, count;
	int err;

	search = mem_zalloc(sizeof(*search), user_search_destructor);
	if (!search)
		return ENOMEM;

	err = jzon_int(&search->took, jobj, "took");
	if (err)
		goto out;

	err = jzon_int(&search->found, jobj, "found");
	if (err)
		goto out;

	err = jzon_int(&search->returned, jobj, "returned");
	if (err)
		goto out;

	err = jzon_array(&jdocs, jobj, "documents");
	if (err)
		goto out;

	count = json_object_array_length(jdocs);
	if (count > 0) {
		for (i = 0; i < count; ++i) {
			struct json_object *jitem;
			struct engine_found_user *fuser;

			jitem = json_object_array_get_idx(jdocs, i);
			if (!jitem)
				continue;

			err = load_found_user(&fuser, jitem);
			if (err)
				goto out;
			list_append(&search->userl, &fuser->le, fuser);
		}
	}

	*searchp = search;

 out:
	if (err)
		mem_deref(search);

	return err;
}


/*** rest request handler
 */

struct user_search_data {
	engine_user_search_h *h;
	void *arg;
};


static void user_search_handler(int err, const struct http_msg *msg,
			        struct mbuf *mb, struct json_object *jobj,
			        void *arg)
{
	struct user_search_data *data = arg;
	struct engine_user_search *search = NULL;

	err = rest_err(err, msg);
	if (err)
		goto out;

	err = load_user_search(&search, jobj);
	if (err)
		goto out;

 out:
	data->h(err, search, data->arg);
	mem_deref(data);
	mem_deref(search);
}


/*** engine_search_contacts
 */

static int d_arg_handler(struct re_printf *pf, void *arg)
{
	bool *value = arg;

	if (value && *value)
		return pf->vph("&d=1", 4, pf->arg);
	else
		return 0;
}


static int l_arg_handler(struct re_printf *pf, void *arg)
{
	int *value = arg;

	if (value)
		return re_hprintf(pf, "&l=%i", *value);
	else
		return 0;
}


static int q_arg_handler(struct re_printf *pf, void *arg)
{
	char *value = arg;

	if (value && *value)
		return re_hprintf(pf, "&q=%H", rest_urlencode, value);
	else
		return 0;
}


int engine_search_contacts(struct engine *engine, const char *query,
			   int size, bool d, int l,
			   engine_user_search_h *h, void *arg)
{
	struct user_search_data *data;
	int err;

	if (!engine || !h)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->h = h;
	data->arg = arg;

	err = rest_get(NULL, engine->rest, 0, user_search_handler, data,
		       "/search/contacts?size=%i%H%H%H", size,
		       q_arg_handler, query, l_arg_handler, &l,
		       d_arg_handler, &d);
	if (err)
		mem_deref(data);
	return err;
}


int engine_search_top(struct engine *engine, int size,
		      engine_user_search_h *h, void *arg)
{
	struct user_search_data *data;
	int err;

	if (!engine || !h)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->h = h;
	data->arg = arg;

	err = rest_get(NULL, engine->rest, 0, user_search_handler, data,
		       "/search/top?size=%i", size);
	if (err)
		mem_deref(data);
	return err;
}


int engine_search_suggestions(struct engine *engine, int size, int l,
			      engine_user_search_h *h, void *arg)
{
	struct user_search_data *data;
	int err;

	if (!engine || !h)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->h = h;
	data->arg = arg;

	err = rest_get(NULL, engine->rest, 0, user_search_handler, data,
		       "/search/top?size=%i%H", size, l_arg_handler, &l);
	if (err)
		mem_deref(data);
	return err;
}


int engine_search_common(struct engine *engine, struct engine_user *user,
			 engine_user_search_h *h, void *arg)
{
	struct user_search_data *data;
	int err;

	if (!engine || !user || !h)
		return EINVAL;

	data = mem_zalloc(sizeof(*data), NULL);
	if (!data)
		return ENOMEM;

	data->h = h;
	data->arg = arg;

	err = rest_get(NULL, engine->rest, 0, user_search_handler, data,
		       "/search/common/%s", user->id);
	if (err)
		mem_deref(data);
	return err;
}

