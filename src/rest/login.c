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
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_store.h"
#include "avs_rest.h"


struct login {
	struct rest_cli *rest_cli;
	struct rest_req *req;
	struct login_token token;  /* cached token from server */
	struct tmr tmr;
	char *email;
	char *password;
	login_h *loginh;
	void *arg;
};


static int send_refresh_request(struct login *login);


static void refresh_timeout(void *arg)
{
	struct login *login = arg;
	int err;

	info("login: refreshing access token..\n");

	err = send_refresh_request(login);
	if (err) {
		warning("login: refresh: send_request failed (%m)\n", err);
	}
}


static void destructor(void *arg)
{
	struct login *login = arg;

	tmr_cancel(&login->tmr);

	mem_deref(login->req);
	mem_deref(login->rest_cli);
	mem_deref(login->email);
	mem_deref(login->password);
}


static void rest_resp_handler(int err, const struct http_msg *msg,
			      struct mbuf *mb, struct json_object *jobj,
			      void *arg)
{
	struct login *login = arg;
	struct login_token *token = &login->token;
	struct json_object *je;
	const char *ja, *jt;

	(void)mb;

	if (err) {
		warning("login: request failed: %m\n", err);
		goto out;
	}

	err = EPROTO;

	if (msg && msg->scode >= 400) {
		warning("login: request failed: %u %r\n",
			msg->scode, &msg->reason);
		goto out;
	}

	if (!json_object_object_get_ex(jobj, "expires_in", &je)) {
		warning("login: missing json objects in response\n");
		goto out;
	}
	ja = jzon_str(jobj, "access_token");
	jt = jzon_str(jobj, "token_type");
	if (!je || !ja || !jt) {
		warning("login: missing json objects in response\n");
		goto out;
	}

	/* save the access token */
	token->expires_in = json_object_get_int(je);
	str_ncpy(token->access_token, ja, sizeof(token->access_token));
	str_ncpy(token->token_type, jt, sizeof(token->token_type));

	tmr_start(&login->tmr, token->expires_in * 3/4 * 1000,
		  refresh_timeout, login);

	err = 0;

 out:
	/* close HTTP connection now */
	login->req = mem_deref(login->req);

	if (login->loginh)
		login->loginh(err, token, login->arg);
}


static int send_refresh_request(struct login *login)
{
	int err;

	err = rest_request_json(&login->req, login->rest_cli, 0, "POST",
				rest_resp_handler, login,
				"/access", 0);
	if (err) {
		warning("login: refresh request failed (%m)\n", err);
		return err;
	}

	return 0;
}

static int send_request(struct login *login)
{
	int err;

	err = rest_request_json(&login->req, login->rest_cli, 0, "POST",
				rest_resp_handler, login,
				"/login?persist=true",
				2,
				"email", login->email,
				"password", login->password);
	if (err) {
		warning("login: rest_request failed: %m\n", err);
		return err;
	}

	return 0;
}


int login_request(struct login **loginp, struct rest_cli *rest_cli,
		  const char *server_uri,
		  const char *email, const char *password,
		  login_h *loginh, void *arg)
{
	struct login *login;
	int err = 0;

	if (!loginp || !rest_cli || !server_uri || !email || !password)
		return EINVAL;

	login = mem_zalloc(sizeof(*login), destructor);
	if (!login)
		return ENOMEM;

	login->rest_cli = mem_ref(rest_cli);
	err |= str_dup(&login->email, email);
	err |= str_dup(&login->password, password);
	if (err)
		goto out;

	login->loginh = loginh;
	login->arg = arg;

	err = send_request(login);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(login);
	else
		*loginp = login;

	return err;
}


struct login_token *login_get_token(const struct login *login)
{
	return login ? (struct login_token *)&login->token : NULL;
}
