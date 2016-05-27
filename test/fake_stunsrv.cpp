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

#include <gtest/gtest.h>
#include <re.h>
#include "fakes.hpp"


static void stunserver_udp_recv(const struct sa *src, struct mbuf *mb,
				void *arg)
{
	StunServer *stun = static_cast<StunServer *>(arg);
	struct stun_msg *msg;
	int err;

	stun->nrecv++;

	err = stun_msg_decode(&msg, mb, NULL);
	if (err) {
		re_printf("stunsrv: could not decode %zu bytes (%m)\n",
			  mb->end, err);
		return;
	}

#if 0
	stun_msg_dump(msg);
#endif

	if (stun->force_error) {
		(void)stun_ereply(IPPROTO_UDP, stun->us, src, 0, msg, 500,
				  "Forced error", NULL, 0, false, 0);
		goto out;
	}

	ASSERT_EQ(0x0001, stun_msg_type(msg));
	ASSERT_EQ(STUN_CLASS_REQUEST, stun_msg_class(msg));
	ASSERT_EQ(STUN_METHOD_BINDING, stun_msg_method(msg));

	err = stun_reply(IPPROTO_UDP, stun->us, src,
			 0, msg, NULL, 0, false, 1,
			 STUN_ATTR_XOR_MAPPED_ADDR, src);

	if (err) {
		(void)stun_ereply(IPPROTO_UDP, stun->us, src, 0, msg, 400,
				  "Bad Request", NULL, 0, false, 0);
	}

 out:
	mem_deref(msg);
}


void StunServer::init()
{
	int err;

	err = sa_set_str(&addr, "127.0.0.1", 0);
	ASSERT_EQ(0, err);

	err = udp_listen(&us, &addr, stunserver_udp_recv, this);
	ASSERT_EQ(0, err);

	err = udp_local_get(us, &addr);
	ASSERT_EQ(0, err);
}


StunServer::StunServer()
	: us(NULL)
	, nrecv(0)
{
	init();
}


StunServer::~StunServer()
{
	mem_deref(us);
}
