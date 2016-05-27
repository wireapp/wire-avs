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
#include <rew.h>
#include "avs_log.h"
#include "avs_turn.h"


enum {
	TURNPING_INTERVAL = 15,  /* seconds, must be less than 29 */
};


/* NOTE: incoming data is bridged from TURN/TCP --> UDP-socket */
static void turntcp_recv_data(struct turn_conn *tc,
			      const struct sa *src, struct mbuf *mb)
{
	if (tc->datah)
		tc->datah(tc, src, mb, tc->arg);
}


static void stun_mapped_addr_handler(int err, const struct sa *map, void *arg)
{
	struct turn_conn *conn = arg;
	(void)conn;

	if (err) {
		warning("turnconn: [alloced=%u, turnc=%p, sock=%p]"
			" stun keepalive handler failed (%m)\n",
			conn->turn_allocated, conn->turnc, conn->us_turn,
			err);
		return;
	}

	info("turnconn: STUN mapped address %J\n", map);
}


static void turnc_handler(int err, uint16_t scode, const char *reason,
			      const struct sa *relay_addr,
			      const struct sa *mapped_addr,
			      const struct stun_msg *msg,
			      void *arg)
{
	struct turn_conn *tc = arg;
	struct stun_attr *attr;
	(void)mapped_addr;  /* NOTE: TCP-mapped address unused */

	if (err) {
		warning("turnconn: [allocated=%d] TURN-%s"
			" client error (%m)\n",
			tc->turn_allocated, turnconn_proto_name(tc),
			err);
		goto error;
	}
	if (scode) {
		warning("turnconn: [allocated=%d] TURN client error"
			" on method '%s' (%u %s)\n",
			tc->turn_allocated,
			stun_method_name(stun_msg_method(msg)),
			scode, reason);

#if 1
		/* XXX: attempt to find reason for some 441 responses */
		if (scode == 441) {
			warning("turnconn: got 441 on username='%s'\n",
				tc->username);
		}
#endif
		goto error;
	}

	tc->turn_allocated = true;
	tc->ts_turn_resp = tmr_jiffies();

	attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);

	info("turnconn: TURN-%s allocation OK in %dms"
	     " (relay=%J, mapped=%J) [%s]\n",
	     turnconn_proto_name(tc),
	     (int)(tc->ts_turn_resp - tc->ts_turn_req),
	     relay_addr, mapped_addr,
	     attr ? attr->v.software : "");


	/* Start a STUN keepalive timer from the client to
	   the TURN-server, to make sure that the NAT bindings
	   are kept open.

	   the Request/Response scheme is important, to increase
	   the NAT timeout from around 30 to 180 seconds
	*/
	if (tc->proto == IPPROTO_UDP && !tc->ska) {

		debug("turnconn: enable STUN keepalive to TURN-server "
		      " (sock=%p, layer=%d, srv=%J)\n",
		      tc->us_turn, tc->layer_stun, &tc->turn_srv);

		err = stun_keepalive_alloc(&tc->ska, IPPROTO_UDP,
					   tc->us_turn,
					   tc->layer_stun, &tc->turn_srv, NULL,
					   stun_mapped_addr_handler, tc);
		if (err) {
			warning("turnconn: stun_keepalive_alloc error (%m)\n",
				err);
			goto error;
		}
		stun_keepalive_enable(tc->ska, TURNPING_INTERVAL);
	}

	tc->estabh(tc, relay_addr, mapped_addr, msg, tc->arg);

	return;

 error:
	tc->errorh(err ? err : EPROTO, tc->arg);
}


static void tcp_estab(void *arg)
{
	struct turn_conn *tl = arg;
	int err;

	if (tl->tlsc) {
		info("turnconn: TLS established with cipher %s\n",
		     tls_cipher_name(tl->tlsc));
	}

	tl->mb = (struct mbuf *)mem_deref(tl->mb);

	err = turnc_alloc(&tl->turnc, NULL, IPPROTO_TCP, tl->tc, tl->layer_turn,
			  &tl->turn_srv, tl->username, tl->password,
			  TURN_DEFAULT_LIFETIME, turnc_handler, tl);
	if (err) {
		warning("turnconn: turnc_alloc failed (%m)\n", err);
	}
}


static void tcp_recv(struct mbuf *mb, void *arg)
{
	struct turn_conn *tl = arg;
	int err = 0;

	if (tl->mb) {
		size_t pos;

		pos = tl->mb->pos;

		tl->mb->pos = tl->mb->end;

		err = mbuf_write_mem(tl->mb, mbuf_buf(mb),
				     mbuf_get_left(mb));
		if (err)
			goto out;

		tl->mb->pos = pos;
	}
	else {
		tl->mb = mem_ref(mb);
	}

	for (;;) {

		size_t len, pos, end;
		struct sa src;
		uint16_t typ;

		if (mbuf_get_left(tl->mb) < 4)
			break;

		typ = ntohs(mbuf_read_u16(tl->mb));
		len = ntohs(mbuf_read_u16(tl->mb));

		if (typ < 0x4000)
			len += STUN_HEADER_SIZE;
		else if (typ < 0x8000)
			len += 4;
		else {
			err = EBADMSG;
			goto out;
		}

		tl->mb->pos -= 4;

		if (mbuf_get_left(tl->mb) < len)
			break;

		pos = tl->mb->pos;
		end = tl->mb->end;

		tl->mb->end = pos + len;

		err = turnc_recv(tl->turnc, &src, tl->mb);
		if (err)
			goto out;

		if (mbuf_get_left(tl->mb))
			turntcp_recv_data(tl, &src, tl->mb);

		/* 4 byte alignment */
		while (len & 0x03)
			++len;

		tl->mb->pos = pos + len;
		tl->mb->end = end;

		if (tl->mb->pos >= tl->mb->end) {
			tl->mb = mem_deref(tl->mb);
			break;
		}
	}

 out:
	if (err) {
		warning("turnconn: turn tcp_recv error (%m)\n");
		mem_deref(tl);
	}
}


static void tcp_close(int err, void *arg)
{
	struct turn_conn *tl = arg;

	info("turnconn: turn connection closed (%m)\n", err);

	tl->turn_allocated = false;
	tl->turnc = mem_deref(tl->turnc);

	if (tl->errorh)
		tl->errorh(err ? err : EPROTO, tl->arg);
}


static void turnconn_destructor(void *data)
{
	struct turn_conn *tc = data;

	list_unlink(&tc->le);

	mem_deref(tc->uh_app);   /* note: deref before us_app */
	mem_deref(tc->us_app);
	mem_deref(tc->ska);      /* note: deref before socket */
	mem_deref(tc->turnc);    /* note: deref before socket */
	mem_deref(tc->us_turn);
	mem_deref(tc->tlsc);
	mem_deref(tc->tc);
	mem_deref(tc->tls);
	mem_deref(tc->mb);
	mem_deref(tc->username);
	mem_deref(tc->password);
}


int turnconn_alloc(struct turn_conn **connp, struct list *connl,
		   const struct sa *turn_srv, int proto, bool secure,
		   const char *username, const char *password,
		   struct udp_sock *sock,
		   int layer_stun, int layer_turn,
		   turnconn_estab_h *estabh, turnconn_data_h *datah,
		   turnconn_error_h *errorh, void *arg)
{
	struct turn_conn *tc;
	int err = 0;

	if (!turn_srv || !proto)
		return EINVAL;

	if (layer_stun > layer_turn) {
		warning("turn: stun layer (%d) higher thant turn layer (%d)\n",
			layer_stun, layer_turn);
	}

	tc = mem_zalloc(sizeof(*tc), turnconn_destructor);
	if (!tc)
		return ENOMEM;

	tc->turn_srv = *turn_srv;
	tc->proto = proto;
	tc->secure = secure;
	tc->us_turn = mem_ref(sock);
	tc->layer_stun = layer_stun;
	tc->layer_turn = layer_turn;
	tc->estabh = estabh;
	tc->datah = datah;
	tc->errorh = errorh;
	tc->arg = arg;

	err |= str_dup(&tc->username, username);
	err |= str_dup(&tc->password, password);
	if (err)
		goto out;

	switch (proto) {

	case IPPROTO_UDP:
		err = turnc_alloc(&tc->turnc, NULL, IPPROTO_UDP, sock,
				  tc->layer_turn, turn_srv, username, password,
				  TURN_DEFAULT_LIFETIME, turnc_handler, tc);
		if (err) {
			warning("turnconn: turnc_alloc UDP failed (%m)\n",
				err);
			goto out;
		}
		break;

	case IPPROTO_TCP:
		err = tcp_connect(&tc->tc, turn_srv, tcp_estab,
				  tcp_recv, tcp_close, tc);
		if (err) {
			warning("turnconn: could not connect to %J (%m)\n",
				turn_srv, err);
			goto out;
		}

		if (secure) {
			// XXX: NOTE, this one can be shared globally
			err = tls_alloc(&tc->tls, TLS_METHOD_SSLV23,
					NULL, NULL);
			if (err)
				goto out;

			err = tls_start_tcp(&tc->tlsc, tc->tls, tc->tc, 0);
			if (err) {
				warning("turnconn: failed to start TLS"
					" (%m)\n", err);
				goto out;
			}
		}
		break;

	default:
		err = EPROTONOSUPPORT;
		goto out;
	}

	tc->ts_turn_req = tmr_jiffies();

	debug("turnconn: alloc: username='%s' srv=%J\n",
	      username, turn_srv);

	list_append(connl, &tc->le, tc);

 out:
	if (err)
		mem_deref(tc);
	else if (connp)
		*connp = tc;

	return err;
}


static void turnc_perm_handler(void *arg)
{
	(void)arg;

	info("turnconn: TURN permission added OK\n");
}


int turnconn_add_permission(struct turn_conn *conn, const struct sa *peer)
{
	if (!conn || !peer)
		return EINVAL;

	info("turnconn: adding TURN permission to remote address %j\n",
	     peer);

	if (!conn->turn_allocated) {
		warning("turnconn: not allocated, cannot add permission\n");
		return EINTR;
	}

	return turnc_add_perm(conn->turnc, peer, turnc_perm_handler, NULL);
}


struct turn_conn *turnconn_find_allocated(const struct list *turnconnl,
					  int proto)
{
	struct le *le;

	for (le = list_head(turnconnl); le; le = le->next) {
		struct turn_conn *conn = le->data;

		if (conn->proto == proto && conn->turn_allocated)
			return conn;
	}

	return NULL;
}


const char *turnconn_proto_name(const struct turn_conn *conn)
{
	if (!conn)
		return "???";

	if (conn->secure)
		return "TLS";
	else
		return net_proto2name(conn->proto);
}


int turnconn_debug(struct re_printf *pf, const struct turn_conn *conn)
{
	int err = 0;

	if (!conn)
		return 0;

	err |= re_hprintf(pf,
			  "...[%c] proto=%s srv=%J  turnc=<%p>  (%zu ms)\n",
			  conn->turn_allocated ? 'A' : ' ',
			  turnconn_proto_name(conn), &conn->turn_srv,
			  conn->turnc,
			  conn->ts_turn_resp - conn->ts_turn_req);
	return err;
}
