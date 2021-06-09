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
#include "ztest.h"
#include "fakes.hpp"


/*
 * The purpose of this test is to verify that DTLS works robustly
 * also with packet loss.
 */

enum {
	LAYER_DTLS   =  0,
	LAYER_FILTER = -1,  /* below DTLS */
};


enum filter_type {
	FILTER_NONE = 0,
	FILTER_PACKET_LOSS,
	FILTER_DUPLICATE
};


struct agent {
	struct tls *tls;
	struct dtls_sock *dtls_sock;
	struct tls_conn *dtls_conn;
	struct udp_sock *us;
	struct sa addr;
	char name[64];
	bool active;
	unsigned estab;
	unsigned n_recv;
	struct agent *peer;
	struct udp_helper *uh;
	unsigned seq;
	unsigned seq_lost;
	enum filter_type filter_type;
};


static const char *test_payload = "Mr. Potato is coming to town";


static void duplicate_mbuf(struct mbuf *mb)
{
	size_t pos;
	size_t len;
	int err;
	uint8_t *buf;

	pos = mb->pos;
	len = mbuf_get_left(mb);

	buf = (uint8_t *)mem_alloc(len, NULL);
	memcpy(buf, mb->buf + pos, len);

	mb->pos = mb->end;

	err = mbuf_write_mem(mb, buf, len);
	ASSERT_EQ(0, err);

	mb->pos = pos;

	mem_deref(buf);
}


static bool handle_filter(struct agent *ag, struct mbuf *mb, bool tx)
{
	bool drop = false, dup=false;

	++ag->seq;

	switch (ag->filter_type) {

	case FILTER_NONE:
		break;

	case FILTER_PACKET_LOSS:
		if (ag->seq == ag->seq_lost) {
			drop = true;
		}
		break;

	case FILTER_DUPLICATE:
		duplicate_mbuf(mb);
		dup = true;
		break;

	default:
		break;
	}

#if 0
	re_printf("[%s]    seq %u    %s %zu bytes    %s    %s\n",
		  ag->name, ag->seq,
		  tx ? "TX" : "RX", mbuf_get_left(mb),
		  drop ? "DROP" : "    ",
		  dup  ? "DUP!" : "    ");
#endif

	return drop;
}


static bool udp_helper_send_handler(int *err, struct sa *dst,
				    struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	return handle_filter(ag, mb, true);
}


static bool udp_helper_recv_handler(struct sa *src,
				    struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	return handle_filter(ag, mb, false);
}


static void dtls_estab_handler(void *arg)
{
	struct agent *ag = (struct agent *)arg;
	const char *name;
	int err;

	++ag->estab;
	name = tls_cipher_name(ag->dtls_conn);

#if 0
	re_printf("[%s] ~~~ DTLS Established (cipher=%s) ~~~\n",
		  ag->name, name);
#endif

	ASSERT_EQ(0, re_regex(name, strlen(name), "ECDHE"));

	if (ag->peer->estab) {

		struct mbuf *mb = mbuf_alloc(512);

		mbuf_write_str(mb, test_payload);

		mb->pos = 0;
		err = dtls_send(ag->dtls_conn, mb);
		ASSERT_EQ(0, err);

		mb->pos = 0;
		err = dtls_send(ag->peer->dtls_conn, mb);
		ASSERT_EQ(0, err);

		mem_deref(mb);
	}
}


static void dtls_recv_handler(struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	++ag->n_recv;

	ASSERT_EQ(str_len(test_payload), mbuf_get_left(mb));
	ASSERT_EQ(0, memcmp(test_payload, mbuf_buf(mb), mbuf_get_left(mb)));

	if (ag->peer->n_recv > 0) {
		re_cancel();
	}
}


static void dtls_close_handler(int err, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	re_printf("[%s] *** DTLS closed (%m) ***\n", ag->name, err);

	re_cancel();
}


static void dtls_conn_handler(const struct sa *peer, void *arg)
{
	struct agent *ag = (struct agent *)arg;
	int err;

	ASSERT_FALSE(ag->active);

	err = dtls_accept(&ag->dtls_conn, ag->tls, ag->dtls_sock,
			  dtls_estab_handler, dtls_recv_handler,
			  dtls_close_handler, ag);
	ASSERT_EQ(0, err);
}


static void destructor(void *arg)
{
	struct agent *ag = (struct agent *)arg;

	mem_deref(ag->uh);
	mem_deref(ag->dtls_conn);
	mem_deref(ag->dtls_sock);
	mem_deref(ag->us);
	mem_deref(ag->tls);
}


static void agent_alloc(struct agent **agp, struct tls *tls,
		       const char *name, bool active)
{
	struct agent *ag;
	struct sa laddr;
	int err;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->tls = (struct tls *)mem_ref(tls);
	str_ncpy(ag->name, name, sizeof(ag->name));
	ag->active = active;

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = udp_listen(&ag->us, &laddr, NULL, NULL);
	ASSERT_EQ(0, err);

	err = udp_local_get(ag->us, &ag->addr);
	ASSERT_EQ(0, err);

	err = dtls_listen(&ag->dtls_sock, &laddr, ag->us, 32,
			  LAYER_DTLS, dtls_conn_handler, ag);
	ASSERT_EQ(0, err);

	*agp = ag;
}


static void agent_connect(struct agent *ag, const struct sa *peer)
{
	int err;

	err = dtls_connect(&ag->dtls_conn, ag->tls, ag->dtls_sock, peer,
			   dtls_estab_handler, dtls_recv_handler,
			   dtls_close_handler, ag);
	ASSERT_EQ(0, err);
}


static void agent_filter(struct agent *ag, enum filter_type filter_type,
			 unsigned seq_lost)
{
	int err;

	ag->filter_type = filter_type;
	ag->seq_lost = seq_lost;

	err = udp_register_helper(&ag->uh, ag->us, LAYER_FILTER,
				  udp_helper_send_handler,
				  udp_helper_recv_handler,
				  ag);
	ASSERT_EQ(0, err);
}


static void test_init(enum tls_keytype cert_type, enum filter_type filter_type,
		      unsigned seq_lost)
{
	struct tls *tls = NULL;
	struct agent *ag_a=0, *ag_b=0;
	int err;

#if 0
	if (seq_lost)
		re_printf("test: loosing packet number #%u\n", seq_lost);
#endif

	/* shared TLS context */
	err = tls_alloc(&tls, TLS_METHOD_DTLS, 0, 0);
	ASSERT_EQ(0, err);

	err = cert_enable_ecdh(tls);
	ASSERT_EQ(0, err);

	switch (cert_type) {

	case TLS_KEYTYPE_EC:
		err = cert_tls_set_selfsigned_ecdsa(tls, "prime256v1");
		ASSERT_EQ(0, err);
		break;

	default:
		ASSERT_TRUE(false);
		break;
	}

	/* create both agents, then connect from A to B */
	agent_alloc(&ag_a, tls, "A", true);
	agent_alloc(&ag_b, tls, "B", false);
	ASSERT_TRUE(ag_a != NULL);
	ASSERT_TRUE(ag_b != NULL);
	ag_a->peer = ag_b;
	ag_b->peer = ag_a;

	agent_filter(ag_a, filter_type, seq_lost);

	agent_connect(ag_a, &ag_b->addr);

	/* start main-loop and wait ... */
	err = re_main_wait(50000);
	ASSERT_EQ(0, err);

	/* verify after test is completed */
	ASSERT_EQ(1, ag_a->estab);
	ASSERT_EQ(1, ag_b->estab);

	ASSERT_EQ(1, ag_a->n_recv);
	ASSERT_EQ(1, ag_b->n_recv);

	mem_deref(ag_b);
	mem_deref(ag_a);
	mem_deref(tls);
}


TEST(dtls, no_packet_loss_with_ecdsa)
{
	test_init(TLS_KEYTYPE_EC, FILTER_NONE, 0);
}


TEST(dtls, duplicate)
{
	test_init(TLS_KEYTYPE_EC, FILTER_DUPLICATE, 0);
}


TEST(dtls, loss_1)
{
	test_init(TLS_KEYTYPE_EC, FILTER_PACKET_LOSS, 1);
}


TEST(dtls, loss_2)
{
	test_init(TLS_KEYTYPE_EC, FILTER_PACKET_LOSS, 2);
}


TEST(dtls, loss_3)
{
	test_init(TLS_KEYTYPE_EC, FILTER_PACKET_LOSS, 3);
}


TEST(dtls, loss_4)
{
	test_init(TLS_KEYTYPE_EC, FILTER_PACKET_LOSS, 4);
}
