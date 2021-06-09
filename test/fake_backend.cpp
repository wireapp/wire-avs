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
#include <avs.h>
#include <gtest/gtest.h>
#include "fakes.hpp"


void FakeBackend::init()
{
	int err;

	tmr_init(&tmr_send);

	err = sa_set_str(&laddr, "127.0.0.1", 0);
	ASSERT_EQ(0, err);

	err = http_listen(&httpsock, &laddr, http_req_handler, this);
	if (err) {
		re_fprintf(stderr, "http_listen: %m\n", err);
		ASSERT_EQ(0, err);
	}

	err = tcp_sock_local_get(http_sock_tcp(httpsock), &laddr);
	ASSERT_EQ(0, err);

	re_snprintf(uri, sizeof(uri),
		    "http://127.0.0.1:%u", sa_port(&laddr));

	err = websock_alloc(&ws, NULL, NULL);
	ASSERT_EQ(0, err);
}


FakeBackend::FakeBackend()
{
	uri[0] = '\0';

	init();
}


FakeBackend::~FakeBackend()
{
	mem_deref(mbq);
	mem_deref(tcq);
	tmr_cancel(&tmr_send);

	mem_deref(httpsock);
	mem_deref(ws_conn);
	mem_deref(ws);

	mem_deref(clients);
}


static int resp_body(char **bodyp, const Token *tok)
{
	struct odict *o;
	int err;

	if (!tok)
		return EINVAL;

	err = odict_alloc(&o, 4);
	if (err)
		return err;

	err |= odict_entry_add(o, "expires_in",
			      ODICT_INT, (int64_t)tok->expires_in);
	err |= odict_entry_add(o, "access_token",
			      ODICT_STRING, tok->access_token.c_str());
	err |= odict_entry_add(o, "token_type",
			      ODICT_STRING, tok->token_type.c_str());
	if (err)
		return err;

	err = re_sdprintf(bodyp, "%H", json_encode_odict, o);

	mem_deref(o);

	return err;
}


static const char *jzon_get_str(const struct odict *o, const char *key)
{
	const struct odict_entry *e;

	e = odict_lookup(o, key);
	if (!e)
		return NULL;

	if (e->type != ODICT_STRING)
		return NULL;

	return e->u.str;
}


void FakeBackend::handle_login(struct http_conn *conn, const struct odict *o)
{
	const char *email, *passw;
	User *user;
	int err;

	email = jzon_get_str(o, "email");
	passw = jzon_get_str(o, "password");
	if (!email || !passw) {
		warning("fake: missing email/password\n");
		return;
	}

	user = findUser(email);
	if (user) {

		struct mbuf *mb_body = mbuf_alloc(1024);
		Token *tok;
		char *body = 0;

		if (user->password != passw) {
			re_fprintf(stderr, "backend: BAD PASSWORD (%s)\n",
				   passw);

			http_ereply(conn, 401, "Unauthorized");
		}

		tok = addToken(3600, "abc-2");

		err = resp_body(&body, tok);
		ASSERT_EQ(0, err);

		if (chunked) {
			err  = chunk_encode(mb_body, (uint8_t *)body,
					    str_len(body));
			err |= chunk_encode(mb_body, NULL, 0);
			ASSERT_EQ(0, err);
		}
		else {
			mbuf_write_str(mb_body, body);
		}

		err = http_reply(conn, 200, "OK",
				 "%s"
				 "Content-Type: application/json\r\n"
				 "Content-Length: %zu\r\n"
				 "\r\n"
				 "%b"
				 ,
				 chunked
				     ? "Transfer-Encoding: chunked\r\n"
				     : "",
				 mb_body->end,
				 mb_body->buf, mb_body->end);
		ASSERT_EQ(0, err);

		mem_deref(mb_body);
		mem_deref(body);
	}
	else {
		http_ereply(conn, 404, "Not Found");
	}
}


/* this function should not be called */
static void websock_recv_handler(const struct websock_hdr *hdr,
				 struct mbuf *mb, void *arg)
{
	FakeBackend *backend = static_cast<FakeBackend *>(arg);
	(void)backend;

	re_printf("backend: websock receive [%zu bytes]\n",
		  mbuf_get_left(mb));

	ASSERT_TRUE(false);
}


static void websock_close_handler(int err, void *arg)
{
	FakeBackend *backend = static_cast<FakeBackend *>(arg);
	(void)backend;
}


void FakeBackend::handle_await(struct http_conn *conn,
			       const struct http_msg *msg)
{
	struct pl pl_access_token;
	char access_token[256];
	int err;

	if (re_regex(msg->prm.p, msg->prm.l,
		     "?access_token=[^]+", &pl_access_token)) {

		re_fprintf(stderr,
			   "backend: could not parse access_token (%r)\n",
			   &msg->prm);

		http_ereply(conn, 400, "Bad Request");
		return;
	}

	pl_strcpy(&pl_access_token, access_token, sizeof(access_token));

	Token *token = findToken(access_token);
	if (token) {

		/* start Websock connection */
		err = websock_accept(&ws_conn, ws, conn, msg,
				     60000, websock_recv_handler,
				     websock_close_handler, this);
		if (err) {
			re_fprintf(stderr, "backend: websock_accept: %m\n",
				   err);
			http_ereply(conn, 500, "Server Error");
		}
	}
	else {
		re_fprintf(stderr, "backend: await: Access-Token '%r'"
			   " does not exist\n", &pl_access_token);

		http_ereply(conn, 401, "Unauthorized");
	}
}


void FakeBackend::handle_self(struct http_conn *conn,
			      const struct http_msg *msg)
{
	// NOTE: incomplete JSON
	static const char fake_self_json[] =
		"{\"email\":\"blender@wearezeta.com\","
		"\"phone\":null,"
		"\"accent\":"
		"[0.9959999918937683,0.36800000071525574,"
		"0.7409999966621399,1.0],"
		"\"accent_id\":0,"
		"\"name\":\"Test-Blender\","
		"\"id\":\"9cba9672-834b-45ac-924d-1af7a6425e0d\"}";

	int err = http_reply(conn, 200, "OK",
			     "Content-Type: application/json\r\n"
			     "Content-Length: %zu\r\n"
			     "\r\n"
			     "%s"
			     ,
			     str_len(fake_self_json),
			     fake_self_json);
	ASSERT_EQ(0, err);
}


void FakeBackend::handle_users(struct http_conn *conn,
			       const struct http_msg *msg,
			       const struct pl *userid)
{
	http_ereply(conn, 404, "Not Found");
}


static const char *fragment_body =
	"{"
	"  \"transport\"           : \"tcp\",\n"
	"  \"fragmented\"          : \"yes\",\n"
	"  \"is_this_a_cool_test\" : \"no\"\n"
	"}"
	"";

/*
 * Timer for sending outgoing fragmented packets.
 *
 * - a timer is needed so that this re_main() handler can "yield"
 *   and let other sockets receive the data
 */
static void tmr_send_handler(void *arg)
{
	FakeBackend *be = static_cast<FakeBackend *>(arg);
	int err;

	if (mbuf_get_left(be->mbq)) {

		size_t n = MIN(be->frag_size, mbuf_get_left(be->mbq));
		struct mbuf mb;

		tmr_start(&be->tmr_send, 2, tmr_send_handler, be);

		mb.pos = 0;
		mb.end = n;
		mb.size = n;
		mb.buf = mbuf_buf(be->mbq);

		err = tcp_send(be->tcq, &mb);
		if (err)
			goto out;

		mbuf_advance(be->mbq, n);
	}
	else {
		/* queue is empty */
		be->mbq = (struct mbuf *)mem_deref(be->mbq);
		be->tcq = (struct tcp_conn *)mem_deref(be->tcq);
	}

 out:
	return;
}


int FakeBackend::handle_fragment_test(struct http_conn *conn,
				       const struct http_msg *msg)
{
	struct mbuf *mb;
	struct tcp_conn *tc = http_conn_tcp(conn);
	int err;

	mb = mbuf_alloc(8192);
	if (!mb)
		return ENOMEM;

	err  = mbuf_printf(mb, "HTTP/1.1 %u %s\r\n", 200, "OK");
	err |= mbuf_printf(mb, "Content-Type: application/json\r\n");
	err |= mbuf_printf(mb, "Content-Length: %zu\r\n\r\n",
			   str_len(fragment_body));
	if (err)
		goto out;

	mbuf_write_str(mb, fragment_body);

	mb->pos = 0;

	/* save the buffer and TCP-connection, then start
	   the async timer for sending fragments */
	mbq = (struct mbuf *)mem_ref(mb);
	tcq = (struct tcp_conn *)mem_ref(tc);
	tmr_start(&tmr_send, 2, tmr_send_handler, this);

 out:
	mem_deref(mb);

	return err;
}


void FakeBackend::handle_get_clients(struct http_conn *conn,
				     const struct http_msg *msg)
{
	char reqid[22];
	char *body = NULL;
	int err;

	rand_str(reqid, sizeof(reqid));

	if (clients) {
		err = re_sdprintf(&body, "%H", json_encode_odict, clients);
	}
	else {
		err = re_sdprintf(&body, "{}");
	}
	if (err) {
		warning("get_clients: failed to encode body (%m)\n", err);
		goto out;
	}

	err = http_reply(conn, 200, "OK",
		     "Access-Control-Expose-Headers: Request-Id, Location\r\n"
		     "Content-Type: application/json\r\n"
		     "Date: %H\r\n"
		     "Request-Id: %s\r\n"
		     "Server: nginx\r\n"
		     "Strict-Transport-Security: max-age=31536000; preload\r\n"
		     "Vary: Accept-Encoding\r\n"
		     "Content-Length: %zu\r\n"
		     "Connection: keep-alive\r\n"
		     "\r\n"
		     "%s"
			     ,
			     fmt_gmtime, NULL,
			     reqid,
			     str_len(body),
			     body
			     );
	ASSERT_EQ(0, err);

 out:
	mem_deref(body);
}


void FakeBackend::handle_post_clients(struct http_conn *conn,
				     const struct http_msg *msg)
{
	struct json_object *jobj = NULL;
	struct odict *cli = NULL, *location = NULL;
	char ix[32], addr[128], id[17], reqid[22];
	char *body = 0;
	int err = 0;

	rand_str(reqid, sizeof(reqid));

	err = jzon_decode(&jobj, (char *)mbuf_buf(msg->mb),
			  mbuf_get_left(msg->mb));
	if (err) {
		warning("jzon_decode error (%m)\n", err);
		goto out;
	}

	err  = odict_alloc(&cli, 4);
	err |= odict_alloc(&location, 4);
	if (err) {
		warning("odict_alloc error (%m)\n", err);
		goto out;
	}

	/* this location is fixed */
	err = odict_entry_add(location, "lat", ODICT_DOUBLE, 52.528500);
	err = odict_entry_add(location, "lon", ODICT_DOUBLE, 13.410899);
	if (err)
		goto out;

	re_snprintf(addr, sizeof(addr), "%j", http_conn_peer(conn));
	rand_str(id, sizeof(id));

	/* build the Client object: */
	err =  odict_entry_add(cli, "cookie",
			       ODICT_STRING, jzon_str(jobj, "cookie"));
	err |= odict_entry_add(cli, "time", ODICT_INT, (int64_t)time(0));
	err |= odict_entry_add(cli, "location", ODICT_OBJECT, location);
	err |= odict_entry_add(cli, "address", ODICT_STRING, addr);
	err |= odict_entry_add(cli, "model",
			       ODICT_STRING, jzon_str(jobj, "model"));
	err |= odict_entry_add(cli, "id", ODICT_STRING, id);
	err |= odict_entry_add(cli, "type",
			       ODICT_STRING, jzon_str(jobj, "type"));
	err |= odict_entry_add(cli, "class",
			       ODICT_STRING, jzon_str(jobj, "class"));

	if (err) {
		warning("odict_entry_add error (%m)\n", err);
		goto out;
	}

	/* Append the new client object to the Clients dictionary */
	if (!clients) {
		err = odict_alloc(&clients, 8);
		if (err) {
			warning("odict_alloc error (%m)\n", err);
			goto out;
		}
	}

	re_snprintf(ix, sizeof(ix), "%u", odict_count(clients, false));
	err = odict_entry_add(clients, ix, ODICT_OBJECT, cli);
	if (err) {
		warning("odict_entry_add(OBJECT) error (%m)\n", err);
		goto out;
	}

#if 0
	re_printf("Backend clients:\n");
	re_printf("%H\n", odict_debug, clients);
#endif

	err = re_sdprintf(&body, "%H", json_encode_odict, clients);
	if (err) {
		warning("post_clients: failed to encode body (%m)\n", err);
		goto out;
	}

	err = http_reply(conn, 201, "Created",
		     "Access-Control-Expose-Headers: Request-Id, Location\r\n"
		     "Content-Type: application/json\r\n"
		     "Date: %H\r\n"
		     "Request-Id: %s\r\n"
		     "Server: nginx\r\n"
		     "Strict-Transport-Security: max-age=31536000; preload\r\n"
		     "Vary: Accept-Encoding\r\n"
		     "Content-Length: %zu\r\n"
		     "Connection: keep-alive\r\n"
		     "\r\n"
		     "%s"
			     ,
			     fmt_gmtime, NULL,
			     reqid,
			     str_len(body),
			     body
			     );
	ASSERT_EQ(0, err);


 out:
	if (err)
		http_ereply(conn, 500, "Internal Server Error");

	mem_deref(location);
	mem_deref(cli);
	mem_deref(jobj);
	mem_deref(body);
}


void FakeBackend::handleRequest(struct http_conn *conn,
				const struct http_msg *msg)
{
	struct odict *o = NULL;
	size_t body_len = mbuf_get_left(msg->mb);
	struct pl userid;
	int err = 0;

#if 0
	re_printf("backend: handle request (%r %r%r) [%zu bytes]\n",
		  &msg->met, &msg->path, &msg->prm, body_len);
#endif

	if (body_len) {

#if 0
		re_printf("%H%b\n", http_msg_print, msg,
			  mbuf_buf(msg->mb), body_len);
#endif

		if (0 != pl_strcasecmp(&msg->ctyp.type, "application") ||
		    0 != pl_strcasecmp(&msg->ctyp.subtype, "json")) {

			re_fprintf(stderr, "backend: ctype = %r\n",
				   &msg->ctyp.type);

			err = http_ereply(conn, 415, "Unsupported Media Type");
			goto out;
		}

		//
		// if the Content-Type is application/json and there
		// is a body, parse the JSON ..
		//
		err = json_decode_odict(&o, 8,
					(char *)mbuf_buf(msg->mb), body_len,
					8);
		if (err) {
			re_fprintf(stderr,
				   "backend: failed to parse JSON"
				   " (%zu bytes)\n", body_len);
			goto out;
		}
	}

	if (0 == pl_strcasecmp(&msg->path, "/login")) {
		handle_login(conn, o);
	}
	else if (0 == pl_strcasecmp(&msg->path, "/await")) {
		handle_await(conn, msg);
	}
	else if (0 == pl_strcasecmp(&msg->path, "/self")) {
		handle_self(conn, msg);
	}
	else if (0 == re_regex(msg->path.p, msg->path.l,
			       "/users/[0-9a-f\\-]+", &userid)) {
		handle_users(conn, msg, &userid);
	}
	else if (0 == pl_strcasecmp(&msg->path, "/fragment_test")) {
		handle_fragment_test(conn, msg);
	}
	else if (0 == pl_strcasecmp(&msg->met, "GET") &&
		 0 == pl_strcasecmp(&msg->path, "/clients")) {

		handle_get_clients(conn, msg);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/clients")) {

		handle_post_clients(conn, msg);
	}
	else {
		http_ereply(conn, 401, "Unauthorized");
	}

 out:
	ASSERT_EQ(0, err);

	mem_deref(o);
}


struct json_object *create_event(struct json_object *payload)
{
	struct json_object *jobj, *array;

	jobj = json_object_new_object();

	array = json_object_new_array();

	// note: only 1 element in payload array for now
	json_object_array_add(array, payload);

	json_object_object_add(jobj, "payload", array);

	json_object_object_add(jobj, "id",
			       json_object_new_string("3727-fb"));

	return jobj;
}


struct json_object *create_payload(const char *convid, const char *time,
				   const char *content, const char *from,
				   const char *id, const char *type)
{
	struct json_object *jobj, *data;
	char nonce[] = "123";

	jobj = json_object_new_object();

	json_object_object_add(jobj, "conversation",
			       json_object_new_string(convid));
	json_object_object_add(jobj, "time",
			       json_object_new_string(time));

	data = json_object_new_object();
	json_object_object_add(data, "content",
			       json_object_new_string(content));
	json_object_object_add(data, "nonce",
			       json_object_new_string(nonce));

	json_object_object_add(jobj, "data", data);


	json_object_object_add(jobj, "from",
			       json_object_new_string(from));
	json_object_object_add(jobj, "id",
			       json_object_new_string(id));
	json_object_object_add(jobj, "type",
			       json_object_new_string(type));

	return jobj;
}


int FakeBackend::simulate_message(const char *content)
{
	struct json_object *payload, *jobj;
	int err = 0;

	if (!ws_conn) {
		re_fprintf(stderr,
			   "backend: simulateEvent"
			   " - no active Websock connection\n");
		return EINVAL;
	}

	payload = create_payload("9a088c8f-1731-4794-b76e-42ba57d917e2",
				 "2014-04-11T11:56:04.118Z",
				 content,
				 "fd4df61d-93e6-41e8-a521-27c3b196b9d5",
				 "206.80011231430856bc",
				 "conversation.message-add");

	jobj = create_event(payload);

	err = websock_send(ws_conn, WEBSOCK_BIN,
			   "%H", jzon_print, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);

	return err;
}
