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
#include "../fakes.hpp"
#include "turn.h"


static void turnserver_udp_recv(const struct sa *src, struct mbuf *mb,
				void *arg)
{
	TurnServer *turn = static_cast<TurnServer *>(arg);

	turn->nrecv++;

	process_msg(turn->turnd, IPPROTO_UDP, turn->us, src, &turn->addr, mb);
}


static void tcp_handler(bool secure, void *arg)
{
	TurnServer *turn = static_cast<TurnServer *>(arg);

	if (secure)
		++turn->nrecv_tls;
	else
		++turn->nrecv_tcp;
}


static void destructor(void *arg)
{
	struct turnd *turnd = (struct turnd *)arg;

	hash_flush(turnd->ht_alloc);
	mem_deref(turnd->ht_alloc);
}


void TurnServer::init()
{
	int err;

	err = sa_set_str(&addr, "127.0.0.1", 0);
	if (err)
		goto out;

	err = udp_listen(&us, &addr, turnserver_udp_recv, this);
	if (err)
		goto out;

	err = udp_local_get(us, &addr);
	if (err)
		goto out;

	turnd = (struct turnd *)mem_zalloc(sizeof(*turnd), destructor);

	/* turn_external_addr */
	err = sa_set_str(&turnd->rel_addr, "127.0.0.1", 0);
	if (err) {
		goto out;
	}

	/* turn_max_lifetime, turn_max_allocations, udp_sockbuf_size */
	turnd->lifetime_max = TURN_DEFAULT_LIFETIME;

	err = hash_alloc(&turnd->ht_alloc, 32);
	if (err) {
		error("turnd hash alloc error: %m\n", err);
		goto out;
	}

	err = restund_tcp_init(turnd, fake_certificate_ecdsa);
	ASSERT_EQ(0, err);

	addr_tcp = *restund_tcp_laddr(turnd, false);
	addr_tls = *restund_tcp_laddr(turnd, true);

	turnd->recvh = tcp_handler;
	turnd->arg = this;

	info("turn: listen=%J, lifetime=%u ext=%j\n", &addr,
	      turnd->lifetime_max, &turnd->rel_addr);

 out:
	ASSERT_EQ(0, err);
}


TurnServer::TurnServer()
	: turnd(0)
	, us(NULL)
	, nrecv(0)
{
	init();
}


TurnServer::~TurnServer()
{
	restund_tcp_close(turnd);

	mem_deref(turnd);
	mem_deref(us);
}


void TurnServer::set_sim_error(uint16_t sim_error)
{
	turnd->sim_error = sim_error;
}
