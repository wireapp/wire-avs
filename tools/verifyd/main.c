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

#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <getopt.h>
#include <stdio.h>
#include <re.h>
#include <avs.h>


struct ident_entry {
	uint64_t id;
	char *ident;

	struct {
		char *publish;
		char *accept;
	} content;
};


static struct {
	struct http_sock *sock;
	struct tmr tmr;
	struct dict *idents;
} verifyd = {
	.sock = NULL,
};


static void ie_destructor(void *arg)
{
	struct ident_entry *ie = arg;

	mem_deref(ie->content.publish);
	mem_deref(ie->content.accept);
	mem_deref(ie->ident);
}


static void handle_post_create(struct http_conn *conn,
			       struct mbuf *mb, size_t clen)
{
	struct ident_entry *ie = NULL;
	uint64_t ident;
	char key[256];
	int err = 0;

	ident = 1 + rand_u64() & 0xf;  // XXX for testing

	re_snprintf(key, sizeof(key), "%llu", ident);

	info("POST: new pairing id %llu\n", ident);

	ie = mem_zalloc(sizeof(*ie), ie_destructor);

	ie->id = ident;
	str_dup(&ie->ident, key);

	err = dict_add(verifyd.idents, key, ie);
	if (err)
		goto out;

	mem_deref(ie); /* Owned by the dictionary */

	http_creply(conn, 200, "OK",
		    "application/json",
		    "{\"pairid\":\"%llu\"}", ident);

 out:
	if (err) {
		warning("internal error (%m)\n" ,err);
	}
}


static void handle_put_publish(struct http_conn *conn,
			       const struct http_msg *msg,
			       struct mbuf *mb, size_t clen)
{
	struct ident_entry *ie = NULL;
	struct pl pl;
	char *key = NULL;
	int err;

	err = re_regex(msg->prm.p, msg->prm.l, "?pairid=[0-9]+", &pl);
	if (err) {
		warning("invalid input\n");
		goto out;
	}

	pl_strdup(&key, &pl);

	ie = dict_lookup(verifyd.idents, key);
	if (!ie) {
		info("publish: pairing-id %s not found\n", key);
		http_ereply(conn, 404, "Not found");
		goto out;
	}

	debug("publish: saving content(%zu): %b\n",
		  clen, mbuf_buf(mb), mbuf_get_left(mb));

	mbuf_strdup(mb, &ie->content.publish, clen);

	http_reply(conn, 200, "OK", NULL);

	http_conn_close(conn);

 out:
	mem_deref(key);
}


static void handle_put_accept(struct http_conn *conn,
			      const struct http_msg *msg,
			      struct mbuf *mb, size_t clen)
{
	struct ident_entry *ie = NULL;
	struct pl pl;
	char *key = NULL;
	int err;

	err = re_regex(msg->prm.p, msg->prm.l, "?pairid=[0-9]+", &pl);
	if (err) {
		warning("invalid input\n");
		goto out;
	}

	pl_strdup(&key, &pl);

	ie = dict_lookup(verifyd.idents, key);
	if (!ie) {
		info("accept: pairing-id %s not found\n", key);
		http_ereply(conn, 404, "Not found");
		goto out;
	}

	debug("accept: saving content(%zu): %b\n",
		  clen, mbuf_buf(mb), mbuf_get_left(mb));

	mbuf_strdup(mb, &ie->content.accept, clen);

	http_reply(conn, 200, "OK", NULL);

	http_conn_close(conn);

 out:
	mem_deref(key);
}


static void handle_get_publish(struct http_conn *conn,
			       const struct http_msg *msg,
			       struct mbuf *mb, size_t clen)
{
	struct ident_entry *ie = NULL;
	struct pl pl;
	char *key = NULL;
	int err;

	info("handle get publish\n");

	err = re_regex(msg->prm.p, msg->prm.l, "?pairid=[0-9]+", &pl);
	if (err) {
		warning("invalid input\n");
		goto out;
	}

	pl_strdup(&key, &pl);

	ie = dict_lookup(verifyd.idents, key);
	if (!ie) {
		info("publish: pairing-id %s not found\n", key);
		http_ereply(conn, 404, "Not found");
		goto out;
	}

	debug("handle get publish (content=%s)\n", ie->content.publish);

	if (ie->content.publish) {
		http_creply(conn, 200, "OK",
			    "application/json", "%s", ie->content.publish);
	}
	else {
		http_ereply(conn, 404, "Not Yet");
	}

	http_conn_close(conn);

 out:
	mem_deref(key);
}


static void handle_get_accept(struct http_conn *conn,
			      const struct http_msg *msg,
			      struct mbuf *mb, size_t clen)
{
	struct ident_entry *ie = NULL;
	struct pl pl;
	char *key = NULL;
	int err;

	info("handle get accept\n");

	err = re_regex(msg->prm.p, msg->prm.l, "?pairid=[0-9]+", &pl);
	if (err) {
		warning("invalid input\n");
		goto out;
	}

	pl_strdup(&key, &pl);

	ie = dict_lookup(verifyd.idents, key);
	if (!ie) {
		info("accept: pairing-id %s not found\n", key);
		http_ereply(conn, 404, "Not found");
		goto out;
	}

	debug("handle get accept (content=%s)\n", ie->content.accept);

	if (ie->content.accept) {
		http_creply(conn, 200, "OK",
			    "application/json", "%s", ie->content.accept);
	}
	else {
		http_ereply(conn, 404, "Not Yet");
	}

	http_conn_close(conn);

 out:
	mem_deref(key);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct mbuf *mb = mbuf_alloc(1024);
	int err = 0;
	(void)arg;

	info("restsrv: request %r %r%r from %J\n",
	     &msg->met, &msg->path, &msg->prm,
	     http_conn_peer(conn));

	if (0 == pl_strcasecmp(&msg->met, "POST") &&
	    0 == pl_strcasecmp(&msg->path, "/create")) {

		handle_post_create(conn, msg->mb, (size_t)msg->clen);
	}
	else if (0 == pl_strcasecmp(&msg->met, "PUT") &&
		 0 == pl_strcasecmp(&msg->path, "/publish")) {

		handle_put_publish(conn, msg, msg->mb, (size_t)msg->clen);
	}
	else if (0 == pl_strcasecmp(&msg->met, "PUT") &&
		 0 == pl_strcasecmp(&msg->path, "/accept")) {

		handle_put_accept(conn, msg, msg->mb, (size_t)msg->clen);
	}
	else if (0 == pl_strcasecmp(&msg->met, "GET") &&
		 0 == pl_strcasecmp(&msg->path, "/publish")) {

		handle_get_publish(conn, msg, msg->mb, (size_t)msg->clen);
	}
	else if (0 == pl_strcasecmp(&msg->met, "GET") &&
		 0 == pl_strcasecmp(&msg->path, "/accept")) {

		handle_get_accept(conn, msg, msg->mb, (size_t)msg->clen);
	}
	else {
		warning("no such resource\n");
		err = ENOENT;
		goto out;
	}

 out:
	if (err) {
		http_ereply(conn, 400, "Bad Request");
		warning("restsrv: response 400 Bad Request (%m)\n", err);

		http_conn_close(conn);
	}

	mem_deref(mb);
}


static int srv_init(uint16_t lport)
{
	struct sa laddr;
	struct tcp_sock *tsock;
	int err;

	info("srv_init: lport=%d\n", (int)lport);

	err = sa_set_str(&laddr, "0.0.0.0", lport);
	if (err)
		return err;

	err = http_listen(&verifyd.sock, &laddr, http_req_handler, NULL);
	if (err) {
		re_fprintf(stderr, "http_listen: failed: %m\n", err);
		return err;
	}

	tsock = http_sock_tcp(verifyd.sock);
	if (tsock) {
		tcp_sock_local_get(tsock, &laddr);
	}

	re_fprintf(stderr, "verifyd: listening on %J\n", &laddr);

	return 0;
}


static void srv_close(void)
{
	verifyd.sock = mem_deref(verifyd.sock);
}


static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		warning("Aborted.\n");
		exit(0);
	}

	term = true;

	warning("Terminating ...\n");

	re_cancel();
}


static void usage(const char *cmd)
{
	re_fprintf(stderr, "usage: %s -p <port>\n", cmd);
}


static void init_timeout(void *arg)
{
	int local_port = *(int *)arg;

	info("init_timeout: srv_init\n");
	srv_init((uint16_t)local_port);
}


int main(int argc, char **argv)
{
	int local_port = -1;
	int err = 0;

	for (;;) {
		const int c = getopt(argc, argv, "p:");

		if (c < 0)
			break;

		switch (c) {

		case 'p':
			local_port = atoi(optarg);
			break;

		default:
			break;

		}
	}

	if (local_port == -1) {
		usage(argv[0]);
		err = EINVAL;
		goto out;
	}

	err = libre_init();
	if (err) {
		(void)re_fprintf(stderr, "libre init failed: %m\n", err);
		goto out;
	}

	err = avs_init(0);
	if (err) {
		(void)re_fprintf(stderr, "avs init failed: %m\n", err);
		goto out;
	}

	err = dict_alloc(&verifyd.idents);
	if (err) {
		re_fprintf(stderr,
			   "%s: cannot alloc idetifier dictionary: %m\n",
			   argv[0], err);
		goto out;
	}

	log_set_min_level(LOG_LEVEL_INFO);

	tmr_init(&verifyd.tmr);

	tmr_start(&verifyd.tmr, 1, init_timeout, &local_port);

	re_printf("re_main\n");
	re_main(signal_handler);
	re_printf("re_main DONE!\n");

 out:
	tmr_cancel(&verifyd.tmr);
	mem_deref(verifyd.idents);

	srv_close();

	libre_close();

	/* check for memory leaks */
	mem_debug();
	tmr_debug();

	return err;
}
