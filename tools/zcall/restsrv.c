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
#include "cli.h"


static struct http_sock *sock;


static void output_handler(const char *str, void *arg)
{
	struct mbuf *mb = arg;

	mbuf_write_str(mb, str);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct mbuf *mb = mbuf_alloc(1024);
	char *str = NULL;
	int err = 0;
	(void)arg;

	debug("restsrv: request %r %r %r from %J\n",
	      &msg->met, &msg->path, &msg->prm,
	      http_conn_peer(conn));

	/* install output-handler to slurp the command output */
	register_output_handler(output_handler, mb);

	if (0 == pl_strcasecmp(&msg->path, "/stroke")) {

		struct pl pl;

		err = re_regex(msg->prm.p, msg->prm.l, "?[^&]1", &pl);
		if (err || pl.l != 1) {
			warning("invalid input\n");
			goto out;
		}

		if (pl.p[0] == 'q') {
			engine_shutdown(zcall_engine);
			input_shutdown();
		}
		else {
			io_stroke_input(pl.p[0]);
		}

		err = http_creply(conn, 200, "OK",
			    "text/html;charset=UTF-8",
			    "%b", mb->buf, mb->end);
	}
	else if (0 == pl_strcasecmp(&msg->path, "/command")) {

		struct pl pl;

		err = re_regex(msg->prm.p, msg->prm.l, "?[^&]+", &pl);
		if (err)
			goto out;

		err = re_sdprintf(&str, "%H", uri_param_unescape, &pl);
		if (err)
			goto out;

		io_command_input(str);

		err = http_creply(conn, 200, "OK",
			    "text/plain;charset=UTF-8",
			    "%b", mb->buf, mb->end);

	}
	else {
		http_ereply(conn, 404, "Not Found");
	}

 out:
	/* uninstall output-handler */
	register_output_handler(NULL, NULL);

	if (err) {
		http_ereply(conn, 400, "Bad Request");
		warning("restsrv: response 400 Bad Request (%m)\n", err);
	}
	else {
		debug("restsrv: response 200 OK (%zu bytes)\n",
		      mb ? mb->end : 0);
	}

	http_conn_close(conn);

	mem_deref(mb);
	mem_deref(str);
}


int restsrv_init(uint16_t lport)
{
	struct sa laddr;
	struct tcp_sock *tsock;
	int err;

	err = sa_set_str(&laddr, "0.0.0.0", lport);
	if (err)
		return err;

	err = http_listen(&sock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	tsock = http_sock_tcp(sock);
	if (tsock) {
		tcp_sock_local_get(tsock, &laddr);
	}

	info("restsrv: listening on %J\n", &laddr);

	return 0;
}


void restsrv_close(void)
{
	sock = mem_deref(sock);
}
