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
#include <sys/time.h>

#include <re.h>

#include "avs_log.h"
#include "avs_turn.h"
#include "avs_netprobe.h"
#include "netprobe.h"


struct result {
	bool ok;
	uint32_t rtt_us;
};


struct netprobe {
	struct turn_conn *turnc;
	struct sa relay_addr;

	struct udp_sock *us_tx;
	struct udp_sock *us_rx;  /* receive socket, shared with TURN */

	uint32_t secret;
	uint32_t seq_ctr;
	uint32_t pkt_interval;

	struct tmr tmr_tx;

	netprobe_h *h;
	void *arg;

	struct result *resultv;
	size_t resultc;
};


static int send_one(struct netprobe *np, uint32_t seq);


static uint64_t tmr_microseconds(void)
{
	struct timeval now;
	uint64_t usec;

	if (0 != gettimeofday(&now, NULL))
		return 0;

	usec  = (uint64_t)now.tv_sec * (uint64_t)1000000;
	usec += now.tv_usec;

	return usec;
}


static void receive_packet(struct netprobe *np,
			   const struct sa *src, struct mbuf *mb)
{
	struct packet pkt;
	uint64_t ts_now = tmr_microseconds();
	uint32_t rtt;
	struct result *result;
	int err;

	err = packet_decode(&pkt, mb);
	if (err) {
		warning("netprobe: failed to decode packet (%m)\n", err);
		return;
	}

	if (np->secret != pkt.secret) {
		warning("netprobe: invalid secret\n");
		return;
	}

	rtt = (uint32_t)(ts_now - pkt.timestamp_tx);

	if (pkt.seq >= np->resultc) {
		warning("netprobe: seq %u out of range\n", pkt.seq);
		return;
	}

	result = &np->resultv[pkt.seq];

	result->ok = true;
	result->rtt_us = rtt;
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct netprobe *np = arg;

	receive_packet(np, src, mb);
}


static int send_one(struct netprobe *np, uint32_t seq)
{
	struct mbuf *mb;
	uint64_t ts_now;
	int err;

	mb = mbuf_alloc(1024);

	ts_now = tmr_microseconds();

	err = packet_encode(mb, ts_now, np->secret, seq, 160);
	if (err)
		goto out;

	mb->pos = 0;
	err = udp_send(np->us_tx, &np->relay_addr, mb);

 out:
	mem_deref(mb);

	return err;
}


static void tmr_completed_handler(void *arg)
{
	struct netprobe_result result;
	struct netprobe *np = arg;
	uint64_t rtt_acc = 0;
	size_t i;

	memset(&result, 0, sizeof(result));

	result.n_pkt_sent = np->resultc;

	for (i=0; i<np->resultc; i++) {

		struct result *res = &np->resultv[i];

		if (res->ok) {
			rtt_acc += res->rtt_us;
			result.n_pkt_recv++;
		}
	}

	if (result.n_pkt_recv)
		result.rtt_avg = (uint32_t)(rtt_acc / result.n_pkt_recv);

	np->h(0, &result, np->arg);
}


static void tmr_handler(void *arg)
{
	struct netprobe *np = arg;

	if (np->seq_ctr < np->resultc) {

		tmr_start(&np->tmr_tx, np->pkt_interval, tmr_handler, np);
		send_one(np, np->seq_ctr++);
	}
	else {
		tmr_start(&np->tmr_tx, 50, tmr_completed_handler, np);
	}
}


static void turnc_perm_handler(void *arg)
{
	struct netprobe *np = arg;

	/* Permission was added, we can start the tests */
	tmr_start(&np->tmr_tx, np->pkt_interval, tmr_handler, np);
}


static void turnconn_estab_handler(struct turn_conn *conn,
				   const struct sa *relay_addr,
				   const struct sa *mapped_addr,
				   const struct stun_msg *msg, void *arg)
{
	struct netprobe *np = arg;
	struct stun_attr *attr;

	np->relay_addr = *relay_addr;

	attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
	if (attr) {
		info("netprobe: turn server is %s\n",
		     attr->v.software);
	}
	attr = stun_msg_attr(msg, STUN_ATTR_LIFETIME);
	if (attr) {
		info("netprobe: turn lifetime is %u seconds\n",
		     attr->v.lifetime);
	}

	/* we must add a permission to our own NAT box */
	turnc_add_perm(np->turnc->turnc, mapped_addr, turnc_perm_handler, np);
}


static void turnconn_data_handler(struct turn_conn *conn, const struct sa *src,
				  struct mbuf *mb, void *arg)
{
	struct netprobe *np = arg;
	(void)np;

	receive_packet(np, src, mb);
}


static void turnconn_error_handler(int err, void *arg)
{
	struct netprobe *np = arg;

	warning("netprobe: turn error (%m)\n", err);

	if (np->h)
		np->h(err ? err : EPROTO, NULL, np->arg);
}


static void destructor(void *arg)
{
	struct netprobe *np = arg;

	tmr_cancel(&np->tmr_tx);
	mem_deref(np->turnc);
	mem_deref(np->us_tx);
	mem_deref(np->us_rx);
	mem_deref(np->resultv);
}


/*
 * @param pkt_interval_ms  Packet interval in [milliseconds]
 */
int netprobe_alloc(struct netprobe **npb, const struct sa *turn_srv,
		   int proto, bool secure,
		   const char *turn_username, const char *turn_password,
		   size_t pkt_count, uint32_t pkt_interval_ms,
		   netprobe_h *h, void *arg)
{
	struct netprobe *np;
	struct sa laddr;
	int err;

	if (!npb || !turn_srv || !pkt_count || !pkt_interval_ms)
		return EINVAL;

	np = mem_zalloc(sizeof(*np), destructor);
	if (!np)
		return ENOMEM;

	np->secret = rand_u32();
	np->pkt_interval = pkt_interval_ms;

	/* XXX: bind to a specific network interface */
	sa_init(&laddr, AF_INET);

	err  = udp_listen(&np->us_tx, &laddr, NULL, NULL);
	if (err)
		goto out;

	if (proto == IPPROTO_UDP) {
		err = udp_listen(&np->us_rx, &laddr, udp_recv, np);
		if (err)
			goto out;
	}

	err = turnconn_alloc(&np->turnc, NULL,
			     turn_srv, proto, secure,
			     turn_username, turn_password,
			     AF_INET, np->us_rx,
			     0, 0,
			     turnconn_estab_handler, turnconn_data_handler,
			     turnconn_error_handler, np);

	if (err)
		goto out;

	np->resultv = mem_zalloc(sizeof(struct result) * pkt_count, NULL);
	np->resultc = pkt_count;

	np->h = h;
	np->arg = arg;

 out:
	if (err)
		mem_deref(np);
	else
		*npb = np;

	return err;
}
