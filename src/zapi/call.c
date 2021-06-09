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

#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_uuid.h"
#include "avs_zapi.h"


int zapi_iceservers_encode(struct json_object *jobj,
			   const struct zapi_ice_server *srvv,
			   size_t srvc)
{
	struct json_object *jices;
	size_t i;
	int r, err;

	if (!jobj || !srvv || !srvc)
		return EINVAL;

	jices = json_object_new_array();
	if (!jices)
		return ENOMEM;

	for (i=0; i<srvc; i++) {
		struct json_object *jice;
		const struct zapi_ice_server *srv = &srvv[i];

		err = jzon_creatf(&jice, "sss",
				  "urls",       srv->url,
				  "username",   srv->username,
				  "credential", srv->credential);
		if (err)
			return err;

		r = json_object_array_add(jices, jice);
		if (r)
			return ENOMEM;
	}

	json_object_object_add(jobj, "ice_servers", jices);

	return 0;
}


int zapi_iceservers_decode(struct json_object *jarr,
			   struct zapi_ice_server **srvvp, size_t *srvc)
{
	struct zapi_ice_server *srvv;
	int i, n;
	int err = 0;

	if (!jarr || !srvvp || !srvc)
		return EINVAL;

	if (!jzon_is_array(jarr)) {
		warning("zapi: json object is not an array\n");
		return EINVAL;
	}

	n = json_object_array_length(jarr);
	srvv = mem_zalloc(n * sizeof(*srvv), NULL);

	for (i = 0; i < n; ++i) {
		struct json_object *jice;
		struct zapi_ice_server *srv = &srvv[i];
		const char *url, *username, *credential;
		struct json_object *urls_arr;

		jice = json_object_array_get_idx(jarr, i);
		if (!jice) {
			warning("zapi: ice_servers[%d] is missing\n", i);
			err = ENOENT;
			goto out;
		}

		if (0 == jzon_array(&urls_arr, jice, "urls")) {

			struct json_object *jurl;
			/* NOTE: we use only first server */
			jurl = json_object_array_get_idx(urls_arr, 0);
			url = json_object_get_string(jurl);
		}
		else {
			url        = jzon_str(jice, "urls");
			if (!url)
				url= jzon_str(jice, "url");
		}

		username   = jzon_str(jice, "username");
		credential = jzon_str(jice, "credential");

		if (!url) {
			warning("zapi: ice_servers[%d] is missing"
				" url\n", i);
			err = EPROTO;
			goto out;
		}

		str_ncpy(srv->url, url, sizeof(srv->url));
		str_ncpy(srv->username, username, sizeof(srv->username));
		str_ncpy(srv->credential, credential, sizeof(srv->credential));
	}

 out:
	if (err)
		mem_deref(srvv);
	else {
		*srvvp = srvv;
		*srvc = n;
	}

	return err;
}
