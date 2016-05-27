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
		log_set_min_level(LOG_LEVEL_INFO);
		log_enable_stderr(false);
#endif

		err = sa_set_str(&laddr, "127.0.0.1", 0);
		ASSERT_EQ(0, err);

		err = udp_listen(&us_peer, &laddr, peer_udp_recv, this);
		ASSERT_EQ(0, err);

		err |= udp_local_get(us_peer, &addr_peer);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		mem_deref(turnc);
		mem_deref(tc);
		mem_deref(tlsc);
		mem_deref(tls);
		mem_deref(us_cli);
		mem_deref(us_peer);
		mem_deref(mb);
	}

	void start(int _proto, bool _secure)
	{
		struct sa laddr;
		int err;

		proto = _proto;
		secure = _secure;

		err = sa_set_str(&laddr, "127.0.0.1", 0);
		ASSERT_EQ(0, err);

		if (secure) {
			err = tls_alloc(&tls, TLS_METHOD_SSLV23,
					NULL, NULL);
			ASSERT_EQ(0, err);
		}

		switch (proto) {

		case IPPROTO_UDP:
			turn_addr = srv.addr;
			err = udp_listen(&us_cli, &laddr,
					 client_udp_recv, this);
			ASSERT_EQ(0, err);
			err = udp_local_get(us_cli, &addr_cli);
			ASSERT_EQ(0, err);

			err = turnc_alloc(&turnc, NULL, proto, us_cli, 0,
					  &srv.addr, "user", "pass", 600,
					  turnc_handler, this);
			ASSERT_EQ(0, err);
			break;

		case IPPROTO_TCP:
			if (secure)
				turn_addr = srv.addr_tls;
			else
				turn_addr = srv.addr_tcp;

			err = tcp_connect(&tc, &turn_addr, tcp_estab,
					  tcp_recv, tcp_close, this);
			ASSERT_EQ(0, err);

			if (secure) {
				err = tls_start_tcp(&tlsc, tls,
						    tc, 0);
				ASSERT_EQ(0, err);
			}
			break;

		default:
			re_printf("invalid proto %d\n", proto);
			ASSERT_TRUE(false);
			break;
		}
	}

	static void turnc_handler(int err,
				  uint16_t scode, const char *reason,
				  const struct sa *relay_addr,
				  const struct sa *mapped_addr,
				  const struct stun_msg *msg,
				  void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		++tt->n_alloch;

		ASSERT_EQ(0, err);
		ASSERT_EQ(0, scode);
		ASSERT_TRUE(sa_isset(relay_addr, SA_ALL));
		ASSERT_TRUE(sa_isset(mapped_addr, SA_ALL));

		if (tt->proto == IPPROTO_UDP) {
			EXPECT_TRUE(sa_cmp(mapped_addr, &tt->addr_cli, SA_ALL));
		}

		tt->addr_relay = *relay_addr;

		err = turnc_add_perm(tt->turnc, &tt->addr_peer,
				     turnc_perm_handler, tt);
		ASSERT_EQ(0, err);
	}

	static void turnc_perm_handler(void *arg)
	{
		TestTurn *tt = static_cast<TestTurn *>(arg);

		++tt->n_permh;

		tt->send_data(payload);
		tt->send_data(payload);
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
			err = turnc_send(turnc, &addr_peer, mb);
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

	static void tcp_recv(struct mbuf *mb, void *arg)
	{
		TestTurn *tl = static_cast<TestTurn *>(arg);
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
			tl->mb = (struct mbuf *)mem_ref(mb);
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
				tl->data_handler(&src, tl->mb);

			/* 4 byte alignment */
			while (len & 0x03)
				++len;

			tl->mb->pos = pos + len;
			tl->mb->end = end;

			if (tl->mb->pos >= tl->mb->end) {
				tl->mb = (struct mbuf *)mem_deref(tl->mb);
				break;
			}
		}

	out:
		if (err) {
			ASSERT_EQ(0, err);
		}
	}

	static void tcp_estab(void *arg)
	{
		TestTurn *tl = static_cast<TestTurn *>(arg);
		int err;

		tl->mb = (struct mbuf *)mem_deref(tl->mb);

		err = turnc_alloc(&tl->turnc, NULL, IPPROTO_TCP, tl->tc, 0,
				  &tl->turn_addr, "user", "pass",
				  TURN_DEFAULT_LIFETIME, turnc_handler, tl);
		if (err) {
			ASSERT_EQ(0, err);
		}
	}

	static void tcp_close(int err, void *arg)
	{
		TestTurn *tl = static_cast<TestTurn *>(arg);

		re_cancel();
	}

protected:
	TurnServer srv;
	struct turnc *turnc = nullptr;
	struct udp_sock *us_cli = nullptr;
	struct udp_sock *us_peer = nullptr;
	struct tcp_conn *tc = nullptr;
	struct sa addr_cli;
	struct sa addr_peer;
	struct sa addr_relay;
	struct sa turn_addr;

	struct tls_conn *tlsc = nullptr;
	struct tls *tls = nullptr;
	struct mbuf *mb = nullptr;
	int proto;
	bool secure;

	unsigned n_alloch = 0;
	unsigned n_permh = 0;
	unsigned n_udp_cli = 0;
	unsigned n_tcp_cli = 0;
	unsigned n_udp_peer = 0;
};


TEST_F(TestTurn, allocation_permission_send)
{
	int err;

	start(IPPROTO_UDP, false);

	/* start mainloop, wait for traffic */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify TURN client */
	ASSERT_EQ(1, n_alloch);
	ASSERT_EQ(1, n_permh);
	ASSERT_TRUE(n_udp_peer > 0);
	ASSERT_TRUE(n_udp_cli > 0);

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
	ASSERT_EQ(1, n_permh);
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
	ASSERT_EQ(1, n_permh);
	ASSERT_TRUE(n_udp_peer > 0);
	ASSERT_EQ(0, n_udp_cli);
	ASSERT_TRUE(n_tcp_cli > 0);

	ASSERT_TRUE(tls != NULL);
	ASSERT_TRUE(tlsc != NULL);

	/* verify TURN server */
	ASSERT_EQ(0, srv.nrecv);
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
