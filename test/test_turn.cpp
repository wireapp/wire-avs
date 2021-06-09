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
#include "ztest.h"


/*
 * This test runs a basic TURN client against a Fake TURN-Server.
 * The client creates an allocation, and sets a permission for
 * a Peer. Then some test packets are sent from the client via the
 * TURN-server to the peer, and echoed back to the client.
 *
 *
 *    [TURN Client] <-----> [TURN Server] <-----> [Peer]
 */


static const char *payload = "Ich bin ein payload?";


class TestTurn : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		struct sa laddr;
		int err;

#if 1
		/* you can enable this to see what is going on .. */
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		tmr_init(&tmr_send);

		err = sa_set_str(&laddr, "127.0.0.1", 0);
		ASSERT_EQ(0, err);

		err = udp_listen(&us_peer, &laddr, peer_udp_recv, this);
		ASSERT_EQ(0, err);

		err |= udp_local_get(us_peer, &addr_peer);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		tmr_cancel(&tmr_send);

		mem_deref(turnc);
		mem_deref(us_cli);
		mem_deref(us_peer);
		mem_deref(mb);
	}

	void alloc_turn(int proto, bool secure, const struct sa *srv_addr)
	{
		int err;

		ASSERT_TRUE(turnc == NULL);

		err = turnconn_alloc(&turnc, NULL, srv_addr, proto, secure,
				     "user", "pass", AF_INET, us_cli, 0, 0,
				     turnconn_estab_handler,
				     turnconn_data_handler,
				     turnconn_error_handler, this);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(turnc != NULL);
	}

	void start(int _proto, bool _secure)
	{
		struct sa laddr;
		int err;

		proto = _proto;
		secure = _secure;

		err = sa_set_str(&laddr, "127.0.0.1", 0);
		ASSERT_EQ(0, err);

		switch (proto) {

		case IPPROTO_UDP:
			turn_addr = srv.addr;
			err = udp_listen(&us_cli, &laddr,
					 client_udp_recv, this);
			ASSERT_EQ(0, err);
			err = udp_local_get(us_cli, &addr_cli);
			ASSERT_EQ(0, err);

			alloc_turn(proto, secure, &turn_addr);
			break;

		case IPPROTO_TCP:
			if (secure)
				turn_addr = srv.addr_tls;
			else
				turn_addr = srv.addr_tcp;

			alloc_turn(proto, secure, &turn_addr);
			break;

		default:
			re_printf("invalid proto %d\n", proto);
			ASSERT_TRUE(false);
			break;
		}
	}

	static void tmr_send_handler(void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		if (tt->turnc->n_permh > 0) {

			tt->send_data(payload);
			tt->send_data(payload);

		}
		else {
			tmr_start(&tt->tmr_send, 10, tmr_send_handler, tt);
		}
	}

	static void turnconn_estab_handler(struct turn_conn *conn,
					   const struct sa *relay_addr,
					   const struct sa *mapped_addr,
					   const struct stun_msg *msg, void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);
		int err;

		++tt->n_alloch;

		ASSERT_TRUE(sa_isset(relay_addr, SA_ALL));
		ASSERT_TRUE(sa_isset(mapped_addr, SA_ALL));

		if (tt->proto == IPPROTO_UDP) {
			EXPECT_TRUE(sa_cmp(mapped_addr, &tt->addr_cli, SA_ALL));
		}

		tt->addr_relay = *relay_addr;

		err = turnconn_add_permission(tt->turnc, &tt->addr_peer);
		ASSERT_EQ(0, err);

		tmr_start(&tt->tmr_send, 10, tmr_send_handler, tt);
	}

	static void turnconn_data_handler(struct turn_conn *conn,
					  const struct sa *src,
					  struct mbuf *mb, void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		tt->data_handler(src, mb);
	}

	static void turnconn_error_handler(int err, void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		warning("turnconn error (%m)\n", err);

		tt->alloc_error = err ? err : EPROTO;

		re_cancel();
	}

	static void peer_udp_recv(const struct sa *src, struct mbuf *mb,
				  void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		++tt->n_udp_peer;

		ASSERT_TRUE(sa_cmp(src, &tt->addr_relay, SA_ALL));
		ASSERT_EQ(strlen(payload), mbuf_get_left(mb));
		ASSERT_TRUE(0 == memcmp(payload, mbuf_buf(mb),
					mbuf_get_left(mb)));

		/* echo data back to client */
		udp_send(tt->us_peer, src, mb);
	}

	static void client_udp_recv(const struct sa *src, struct mbuf *mb,
				    void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		++tt->n_udp_cli;

		ASSERT_TRUE(sa_cmp(src, &tt->addr_peer, SA_ALL));
		ASSERT_EQ(strlen(payload), mbuf_get_left(mb));
		ASSERT_TRUE(0 == memcmp(payload, mbuf_buf(mb),
					mbuf_get_left(mb)));

#if 1
		re_cancel();
#endif
	}

	void send_data(const char *str)
	{
		struct mbuf *mb = mbuf_alloc(36 + str_len(str));
		int err;

		mb->pos = 36;

		mbuf_write_str(mb, str);

		mb->pos = 36;

		switch (proto) {

		case IPPROTO_UDP:
			err = udp_send(us_cli, &addr_peer, mb);
			ASSERT_EQ(0, err);
			break;

		case IPPROTO_TCP:
			err = turnc_send(turnc->turnc, &addr_peer, mb);
			ASSERT_EQ(0, err);
			break;

		default:
			ASSERT_EQ(0, EPROTONOSUPPORT);
			break;
		}

		mem_deref(mb);
	}

	void data_handler(const struct sa *src, struct mbuf *mb)
	{
		TestTurn *tt = static_cast<TestTurn *>(this);

		++tt->n_tcp_cli;

		ASSERT_TRUE(sa_cmp(src, &tt->addr_peer, SA_ALL));
		ASSERT_EQ(strlen(payload), mbuf_get_left(mb));
		ASSERT_TRUE(0 == memcmp(payload, mbuf_buf(mb),
					mbuf_get_left(mb)));

#if 1
		re_cancel();
#endif
	}

protected:
	struct tmr tmr_send;
	TurnServer srv;
	struct turn_conn *turnc = nullptr;
	struct udp_sock *us_cli = nullptr;
	struct udp_sock *us_peer = nullptr;
	struct sa addr_cli;
	struct sa addr_peer;
	struct sa addr_relay;
	struct sa turn_addr;
	struct mbuf *mb = nullptr;
	int proto;
	bool secure;

	unsigned n_alloch = 0;
	unsigned n_udp_cli = 0;
	unsigned n_tcp_cli = 0;
	unsigned n_udp_peer = 0;

	int alloc_error = 0;
};


TEST_F(TestTurn, allocation_permission_send)
{
	int err;

	start(IPPROTO_UDP, false);

	/* start mainloop, wait for traffic */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify TURN client */
	ASSERT_EQ(0, alloc_error);
	ASSERT_EQ(1, n_alloch);
	ASSERT_EQ(1, turnc->n_permh);
	ASSERT_TRUE(n_udp_peer > 0);
	ASSERT_TRUE(n_udp_cli > 0);
	ASSERT_FALSE(turnc->failed);

	/* verify TURN server */
	ASSERT_GE(srv.nrecv, 3);
}


TEST_F(TestTurn, allocation_permission_send_tcp)
{
	int err;

	start(IPPROTO_TCP, false);

	/* start mainloop, wait for traffic */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify TURN client */
	ASSERT_EQ(1, n_alloch);
	ASSERT_EQ(1, turnc->n_permh);
	ASSERT_TRUE(n_udp_peer > 0);
	ASSERT_EQ(0, n_udp_cli);
	ASSERT_TRUE(n_tcp_cli > 0);

	/* verify TURN server */
	ASSERT_EQ(0, srv.nrecv);
}


TEST_F(TestTurn, allocation_permission_send_tls)
{
	int err;

	start(IPPROTO_TCP, true);

	/* start mainloop, wait for traffic */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify TURN client */
	ASSERT_EQ(1, n_alloch);
	ASSERT_EQ(1, turnc->n_permh);
	ASSERT_TRUE(n_udp_peer > 0);
	ASSERT_EQ(0, n_udp_cli);
	ASSERT_TRUE(n_tcp_cli > 0);

	ASSERT_TRUE(turnc->tls != NULL);
	ASSERT_TRUE(turnc->tlsc != NULL);

	/* verify TURN server */
	ASSERT_EQ(0, srv.nrecv);
}


TEST_F(TestTurn, allocation_failure_441)
{
	int err;

	/* silence warnings .. */
	log_set_min_level(LOG_LEVEL_ERROR);

	srv.set_sim_error(441);

	start(IPPROTO_UDP, false);

	/* start mainloop, wait for traffic */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify TURN client */
	ASSERT_EQ(EAUTH, alloc_error);
	ASSERT_EQ(0, n_alloch);
	ASSERT_EQ(0, turnc->n_permh);
	ASSERT_EQ(0, n_udp_peer);
	ASSERT_EQ(0, n_udp_cli);
	ASSERT_TRUE(turnc->failed);

	/* verify TURN server */
	ASSERT_GE(srv.nrecv, 1);
}


TEST(turn, uri)
{
	struct test {
		enum stun_scheme scheme;
		const char *addr;
		uint16_t port;
		int proto;
		bool secure;

		const char *str;

	} testv[] = {

		/* STUN uris */
		{
			STUN_SCHEME_STUN, "10.0.0.2", 3478, IPPROTO_UDP, false,
			"stun:10.0.0.2"
		},
		{
			STUN_SCHEME_STUN, "10.0.0.2", 19302, IPPROTO_UDP, false,
			"stun:10.0.0.2:19302"
		},
		{
			STUN_SCHEME_STUN, "10.0.0.2", 3478, IPPROTO_TCP, false,
			"stun:10.0.0.2?transport=tcp"
		},
		{
			STUN_SCHEME_STUN, "10.0.0.2", 19302, IPPROTO_TCP, false,
			"stun:10.0.0.2:19302?transport=tcp"
		},
		{
			STUN_SCHEME_STUN, "10.0.0.2", 5349, IPPROTO_TCP, true,
			"stuns:10.0.0.2?transport=tcp"
		},

		/* TURN uris */
		{
			STUN_SCHEME_TURN, "1.2.3.4", 3478, IPPROTO_UDP, false,
			"turn:1.2.3.4"
		},
		{
			STUN_SCHEME_TURN, "1.2.3.4", 5349, IPPROTO_UDP, true,
			"turns:1.2.3.4"
		},
		{
			STUN_SCHEME_TURN, "1.2.3.4", 8000, IPPROTO_UDP, false,
			"turn:1.2.3.4:8000"
		},
		{
			STUN_SCHEME_TURN, "1.2.3.4", 3478, IPPROTO_TCP, false,
			"turn:1.2.3.4?transport=tcp"
		},
		{
			STUN_SCHEME_TURN, "1.2.3.4", 5349, IPPROTO_TCP, true,
			"turns:1.2.3.4?transport=tcp"
		},
	};
	unsigned i;
	int err;

	for (i=0; i<ARRAY_SIZE(testv); i++) {
		struct test *test = &testv[i];
		struct stun_uri uri;
		struct sa addr;
		char buf[256];

		/* Decode */

		err = stun_uri_decode(&uri, test->str);
		ASSERT_EQ(0, err);

		ASSERT_EQ(test->scheme, uri.scheme);

		err = sa_set_str(&addr, test->addr, test->port);
		ASSERT_EQ(0, err);

		ASSERT_EQ(test->port, sa_port(&uri.addr));
		ASSERT_TRUE(sa_cmp(&addr, &uri.addr, SA_ALL));
		ASSERT_EQ(test->proto, uri.proto);
		ASSERT_EQ(test->secure, uri.secure);

		/* Encode */

		re_snprintf(buf, sizeof(buf), "%H", stun_uri_encode, &uri);

		ASSERT_STREQ(test->str, buf);
	}
}
