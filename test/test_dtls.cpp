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


struct agent {
	struct tls *tls;
	struct dtls_sock *dtls_sock;
	struct tls_conn *dtls_conn;
	struct udp_sock *us;
	struct sa addr;
	char name[64];
	bool active;
	bool estab;
	struct agent *peer;
	struct udp_helper *uh;
	unsigned seq;
	unsigned seq_lost;
};


static bool udp_helper_send_handler(int *err, struct sa *dst,
				    struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	++ag->seq;

	//re_printf("[%s] %u tx %zu bytes\n",
	//		  ag->name, ag->seq, mbuf_get_left(mb));

	if (ag->seq_lost && ag->seq == ag->seq_lost) {
		//re_printf("tx: !! packet number %u DROPPED\n", ag->seq);
		return true;
	}

	return false;
}


static bool udp_helper_recv_handler(struct sa *src,
				    struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;

	++ag->seq;

	//	re_printf("[%s] %u rx %zu bytes\n",
	//		  ag->name, ag->seq, mbuf_get_left(mb));

	if (ag->seq_lost && ag->seq == ag->seq_lost) {
		//re_printf("rx: !! packet number %u DROPPED\n", ag->seq);
		return true;
	}

	return false;
}


static void dtls_estab_handler(void *arg)
{
	struct agent *ag = (struct agent *)arg;
	const char *name;

	ag->estab = true;
	name = tls_cipher_name(ag->dtls_conn);

#if 0
	re_printf("[%s] ~~~ DTLS Established (cipher=%s) ~~~\n",
		  ag->name, name);
#endif

	ASSERT_EQ(0, re_regex(name, strlen(name), "ECDHE"));

	if (ag->peer->estab) {

		//int algo_bits;

		//algo_bits = dtls_conn_cipher_algorithm_bits(ag->dtls_conn);
		//ASSERT_TRUE(algo_bits >= 128);

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
			  dtls_estab_handler, 0, dtls_close_handler, ag);
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
			   dtls_estab_handler, 0, dtls_close_handler, ag);
	ASSERT_EQ(0, err);
}


static void agent_filter(struct agent *ag, unsigned seq_lost)
{
	int err;

	ag->seq_lost = seq_lost;

	err = udp_register_helper(&ag->uh, ag->us, LAYER_FILTER,
				  udp_helper_send_handler,
				  udp_helper_recv_handler,
				  ag);
	ASSERT_EQ(0, err);
}


static void test_init(enum cert_type cert_type, unsigned seq_lost)
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

	switch (cert_type) {

	case CERT_TYPE_RSA:
		err = tls_set_certificate(tls, fake_certificate_rsa,
					  strlen(fake_certificate_rsa));
		ASSERT_EQ(0, err);
		break;

	case CERT_TYPE_ECDSA:
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

	agent_filter(ag_a, seq_lost);

	agent_connect(ag_a, &ag_b->addr);

	/* start main-loop and wait ... */
	err = re_main_wait(50000);
	ASSERT_EQ(0, err);

	/* verify after test is completed */
	ASSERT_EQ(1, ag_a->estab);
	ASSERT_EQ(1, ag_b->estab);

	mem_deref(ag_b);
	mem_deref(ag_a);
	mem_deref(tls);
}


TEST(dtls, no_packet_loss)
{
	test_init(CERT_TYPE_RSA, 0);
}


TEST(dtls, packet_loss_hi)
{
	test_init(CERT_TYPE_RSA, 5);
	//test_init(6);
	//test_init(7);
	//test_init(8);
	//test_init(9);
}


TEST(dtls, no_packet_loss_with_ecdsa)
{
	test_init(CERT_TYPE_ECDSA, 0);
}
