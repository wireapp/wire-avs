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

#include <time.h>
#include <re.h>
#include <avs.h>
#include "turn.h"


enum {
	TCP_IDLE_TIMEOUT   = 600 * 1000,
	TCP_MAX_LENGTH = 2048,
	TCP_MAX_TXQSZ  = 16384,
};


struct tcp_lstnr {
	struct le le;
	struct sa bnd_addr;
	struct tcp_sock *ts;
	struct tls *tls;
	struct turnd *turnd;
};

struct conn {
	struct le le;
	struct tmr tmr;
	struct sa laddr;
	struct sa paddr;
	struct tcp_conn *tc;
	struct tls_conn *tlsc;
	struct mbuf *mb;
	time_t created;
	uint64_t prev_rxc;
	uint64_t rxc;
	struct turnd *turnd;
};


static void conn_destructor(void *arg)
{
	struct conn *conn = arg;

	list_unlink(&conn->le);
	tmr_cancel(&conn->tmr);
	tcp_set_handlers(conn->tc, NULL, NULL, NULL, NULL);
	mem_deref(conn->tlsc);
	mem_deref(conn->tc);
	mem_deref(conn->mb);
}


static void tmr_handler(void *arg)
{
	struct conn *conn = arg;

	if (conn->rxc == conn->prev_rxc) {
		debug("tcp: closing idle connection: %J\n",
			      &conn->paddr);
		mem_deref(conn);
		return;
	}

	conn->prev_rxc = conn->rxc;

	tmr_start(&conn->tmr, TCP_IDLE_TIMEOUT, tmr_handler, conn);
}


static void tcp_estab(void *arg)
{
	struct conn *conn = arg;

	if (conn->tlsc) {
		info("tcp: TLS established with cipher %s\n",
		     tls_cipher_name(conn->tlsc));
	}
}


static void tcp_recv(struct mbuf *mb, void *arg)
{
	struct conn *conn = arg;
	int err = 0;

	if (conn->mb) {
		size_t pos;

		pos = conn->mb->pos;

		conn->mb->pos = conn->mb->end;

		err = mbuf_write_mem(conn->mb, mbuf_buf(mb),mbuf_get_left(mb));
		if (err) {
			warning("tcp: buffer write error: %m\n", err);
			goto out;
		}

		conn->mb->pos = pos;
	}
	else {
		conn->mb = mem_ref(mb);
	}

	for (;;) {

		size_t len, pos, end;
		uint16_t typ;

		if (mbuf_get_left(conn->mb) < 4)
			break;

		typ = ntohs(mbuf_read_u16(conn->mb));
		len = ntohs(mbuf_read_u16(conn->mb));

		if (len > TCP_MAX_LENGTH) {
			debug("tcp: bad length: %zu\n", len);
			err = EBADMSG;
			goto out;
		}

		if (typ < 0x4000)
			len += STUN_HEADER_SIZE;
		else if (typ < 0x8000)
			len += 4;
		else {
			debug("tcp: bad type: 0x%04x\n", typ);
			err = EBADMSG;
			goto out;
		}

		conn->mb->pos -= 4;

		if (mbuf_get_left(conn->mb) < len)
			break;

		pos = conn->mb->pos;
		end = conn->mb->end;

		conn->mb->end = pos + len;

		if (conn->turnd->recvh) {
			conn->turnd->recvh(conn->tlsc != NULL,
					   conn->turnd->arg);
		}

		process_msg(conn->turnd, IPPROTO_TCP, conn->tc, &conn->paddr,
				    &conn->laddr, conn->mb);

		++conn->rxc;

		/* 4 byte alignment */
		while (len & 0x03)
			++len;

		conn->mb->pos = pos + len;
		conn->mb->end = end;

		if (conn->mb->pos >= conn->mb->end) {
			conn->mb = mem_deref(conn->mb);
			break;
		}
	}

 out:
	if (err) {
		conn->mb = mem_deref(conn->mb);
	}
}


static void tcp_close(int err, void *arg)
{
	struct conn *conn = arg;

	debug("tcp: connection closed: %m\n", err);

	mem_deref(conn);
}


static void tcp_conn_handler(const struct sa *peer, void *arg)
{
	const time_t now = time(NULL);
	struct tcp_lstnr *tl = arg;
	struct conn *conn;
	int err;

	debug("tcp: connect from: %J\n", peer);

	conn = mem_zalloc(sizeof(*conn), conn_destructor);
	if (!conn) {
		err = ENOMEM;
		goto out;
	}

	conn->turnd = tl->turnd;

	list_append(&tl->turnd->tcl, &conn->le, conn);
	conn->created = now;
	conn->paddr = *peer;

	err = tcp_accept(&conn->tc, tl->ts, tcp_estab, tcp_recv, tcp_close, conn);
	if (err)
		goto out;

	tcp_conn_txqsz_set(conn->tc, TCP_MAX_TXQSZ);

	err = tcp_conn_local_get(conn->tc, &conn->laddr);
	if (err)
		goto out;

	if (tl->tls) {
		err = tls_start_tcp(&conn->tlsc, tl->tls, conn->tc, 0);
		if (err)
			goto out;
	}

	tmr_start(&conn->tmr, TCP_IDLE_TIMEOUT, tmr_handler, conn);

 out:
	if (err) {
		warning("tcp: unable to accept: %m\n", err);
		tcp_reject(tl->ts);
		mem_deref(conn);
	}
}


static void lstnr_destructor(void *arg)
{
	struct tcp_lstnr *tl = arg;

	list_unlink(&tl->le);
	mem_deref(tl->ts);
	mem_deref(tl->tls);
}


static int listen_handler(struct turnd *turnd, bool tls, const char *cert)
{
	struct tcp_lstnr *tl = NULL;
	int err = ENOMEM;

	tl = mem_zalloc(sizeof(*tl), lstnr_destructor);
	if (!tl) {
		warning("tcp listen error: %m\n", err);
		goto out;
	}

	tl->turnd = turnd;
	list_append(&turnd->lstnrl, &tl->le, tl);

	if (tls) {
		err = tls_alloc(&tl->tls, TLS_METHOD_SSLV23, NULL, NULL);
		if (err) {
			warning("tls error: %m\n", err);
			goto out;
		}

		err = tls_set_certificate(tl->tls, cert, strlen(cert));
		if (err) {
			warning("set certificate error: %m\n", err);
			goto out;
		}
	}

	sa_set_str(&tl->bnd_addr, "127.0.0.1", 0);

	err = tcp_listen(&tl->ts, &tl->bnd_addr, tcp_conn_handler, tl);
	if (err) {
		warning("tcp error: %m\n", err);
		goto out;
	}

	err = tcp_local_get(tl->ts, &tl->bnd_addr);
	if (err)
		goto out;

	debug("%s listen: %J\n", tl->tls ? "tls" : "tcp",
	      &tl->bnd_addr);

 out:
	if (err)
		mem_deref(tl);

	return err;
}


int restund_tcp_init(struct turnd *turnd, const char *cert)
{
	int err;

	list_init(&turnd->lstnrl);
	list_init(&turnd->tcl);

	err = listen_handler(turnd, false, 0);
	if (err)
		goto out;

	err = listen_handler(turnd, true, cert);
	if (err)
		goto out;

 out:
	if (err)
		restund_tcp_close(turnd);

	return err;
}


void restund_tcp_close(struct turnd *turnd)
{
	if (!turnd)
		return;

	list_flush(&turnd->lstnrl);
	list_flush(&turnd->tcl);
}


struct sa *restund_tcp_laddr(struct turnd *turnd, bool secure)
{
	struct le *le;

	if (!turnd)
		return NULL;

	for (le = turnd->lstnrl.head; le; le = le->next) {
		struct tcp_lstnr *tl = le->data;

		if (secure && tl->tls)
			return &tl->bnd_addr;
		else if (!secure)
			return &tl->bnd_addr;
	}

	return NULL;
}
