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
/* libavs -- simple backend interface
 */

#include <re.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_nevent.h"
#include "avs_rest.h"
#include "avs_mill.h"


enum {
	MILL_CONF_REST_MAXOPEN = 4,
};


struct mill {
	/* Configuration
	 */
	char *request_uri;
	char *notification_uri;
	char *email;
	char *password;

	/* Services we use.
	 */
	struct dnsc *dnsc;
	struct http_cli *http;
	struct rest_cli *rest;
	struct login *login;
	struct websock *websock;
	struct nevent *nevent;

	/* Handlers
	 */
	mill_ready_h *readyh;
	mill_error_h *errorh;
	mill_shut_h  *shuth;
	void *arg;
};


static int clear_cookies_login(struct mill *mill);


/*** mill_alloc
 */

static void mill_destructor(void *arg)
{
	struct mill *ml = arg;

	mem_deref(ml->request_uri);
	mem_deref(ml->notification_uri);
	mem_deref(ml->email);
	mem_deref(ml->password);

	mem_deref(ml->nevent);
	mem_deref(ml->websock);
	mem_deref(ml->login);
	mem_deref(ml->rest);
	mem_deref(ml->http);
	mem_deref(ml->dnsc);
}


static int dns_init(struct dnsc **dnscp)
{
	struct sa nsv[16];
	uint32_t nsn;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		error("dns srv get: %m\n", err);
		goto out;
	}

	err = dnsc_alloc(dnscp, NULL, nsv, nsn);
	if (err) {
		error("dnsc alloc: %m\n", err);
		goto out;
	}

 out:
	return err;
}


static void websock_shutdown_handler(void *arg)
{
	struct mill *mill = arg;

	if (mill->shuth)
		mill->shuth(mill, mill->arg);
}


static void nevent_estab_handler(void *arg)
{
	struct mill *mill = arg;

	if (mill->readyh)
		mill->readyh(mill, mill->arg);
}


static int start_nevent(struct mill *mill, const struct login_token *token)
{
	int err;

	err = websock_alloc(&mill->websock, websock_shutdown_handler, mill);
	if (err)
		return err;

	return nevent_alloc(&mill->nevent, mill->websock, mill->http,
			    mill->notification_uri, token->access_token,
			    nevent_estab_handler, NULL, NULL, mill);
}


static void login_handler(int err, const struct login_token *token, void *arg)
{
	struct mill *mill = arg;

	if (err)
		goto out;

	rest_client_set_token(mill->rest, token);
	if (mill->nevent) {
		err = nevent_set_access_token(mill->nevent,
					      token->access_token);
		if (err)
			goto out;
	}
	else {
		err = start_nevent(mill, token);
		if (err)
			goto out;
	}

 out:
	if (err) {
		if (mill->errorh)
			mill->errorh(err, mill, mill->arg);
		mill_shutdown(mill);
	}
}


static int start_login(struct mill *mill)
{
	int err;
	
	err = login_request(&mill->login, mill->rest, mill->request_uri,
			    mill->email, mill->password, login_handler, mill);
	return err;
}


int mill_alloc(struct mill **millp, const char *request_uri,
	       const char *notification_uri,
	       bool clear_cookies, const char *email,
	       const char *password, const char *user_agent,
	       mill_ready_h *readyh, mill_error_h *errorh,
	       mill_shut_h *shuth, void *arg)
{
	struct mill *mill;
	int err;

	if (!millp || !request_uri || !notification_uri || !email
	    || !password)
	{
		return EINVAL;
	}

	mill = mem_zalloc(sizeof(*mill), mill_destructor);
	if (!mill)
		return ENOMEM;

	err = str_dup(&mill->request_uri, request_uri);
	if (err)
		goto out;

	err = str_dup(&mill->notification_uri, notification_uri);
	if (err)
		goto out;

	err = str_dup(&mill->email, email);
	if (err)
		goto out;

	err = str_dup(&mill->password, password);
	if (err)
		goto out;
	

	err = dns_init(&mill->dnsc);
	if (err)
		goto out;

	err = http_client_alloc(&mill->http, mill->dnsc);
	if (err)
		goto out;

	err = rest_client_alloc(&mill->rest, mill->http, mill->request_uri,
				NULL, MILL_CONF_REST_MAXOPEN, user_agent);
	if (err)
		goto out;

	if (clear_cookies) {
		re_printf(" @@@@@ clearing cookies...\n");
		err = clear_cookies_login(mill);
	}
	else 		
		err = start_login(mill);

	mill->readyh = readyh;
	mill->errorh = errorh;
	mill->shuth = shuth;
	mill->arg = arg;

	*millp = mill;

 out:
	if (err)
		mem_deref(mill);
	return err;
}


/*** mill_shutdown
 */

void mill_shutdown(struct mill *mill)
{
	if (!mill)
		return;

	if (mill->nevent) {
		mill->nevent = mem_deref(mill->nevent);
		websock_shutdown(mill->websock);
	}

	/* XXX This may not be a good idea? It will stop it calling
	 *     response callbacks, though, when we think everything
	 *     is done, already.
	 */
	mill->rest = mem_deref(mill->rest);
}


/*** mill_get_rest
 */

struct rest_cli *mill_get_rest(struct mill *mill)
{
	return mill ? mill->rest : NULL;
}


/*** mill_get_nevent
 */

struct nevent *mill_get_nevent(struct mill *mill)
{
	return mill ? mill->nevent : NULL;
}

static void clear_login_handler(int err, const struct http_msg *msg,
				struct mbuf *mb, struct json_object *jobj,
				void *arg)
{
	struct mill *mill = arg;

	err = rest_err(err, msg);
	if (err)
		warning("Failed to clear cookies: %m (%u %r).\n",
			err,
			msg ? msg->scode : 0,
			msg ? &msg->reason : NULL);
	else
		err = start_login(mill);

#if 0
	if (err) {
		if (mill->errorh)
			mill->errorh(err, mill, mill->arg);
		mill_shutdown(mill);
	}
#endif
}


static int clear_cookies_login(struct mill *mill)
{
	struct json_object *jobj, *inner;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;
		
	inner = json_object_new_string(mill->email);
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "email", inner);

	inner = json_object_new_string(mill->password);
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "password", inner);

	inner = json_object_new_array();
	if (!inner) {
		err = ENOMEM;
		goto out;
	}
	json_object_object_add(jobj, "labels", inner);

	err = rest_request(NULL, mill->rest, 0, "POST",
			   clear_login_handler, mill,
			   "/cookies/remove",
			   "%H", jzon_print, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);
	return err;
}

