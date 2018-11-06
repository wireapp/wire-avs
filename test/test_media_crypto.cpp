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


/*
 * Test-cases that involve 2 instances of "struct mediaflow" running
 * in a back-to-back (B2B) setup.
 *
 * Testing: Different crypto modes
 * NAT:     None
 */

// step 1 get a back non-crypto case working

struct test {
	struct list aucodecl;
	struct tmr tmr;
};


struct agent {
	struct test *test;
	struct tls *dtls;
	struct mediaflow *mf;
	struct agent *other;
	char name[64];
	bool offerer;
	int err;

	unsigned n_estab;
	unsigned n_rtp_started;
};


static void abort_test(struct agent *ag, int err)
{
	ag->err = err;
	re_cancel();
}


static bool agent_is_established(const struct agent *ag)
{
	if (!ag)
		return false;

	return ag->n_estab;
}


static bool agents_are_established(const struct agent *ag)
{
	return ag &&
		agent_is_established(ag) &&
		agent_is_established(ag->other);
}

static bool agent_has_rtp(const struct agent *ag)
{
	if (!ag)
		return false;

	return ag->n_rtp_started;
}

static bool agents_have_rtp(const struct agent *ag)
{
	return ag &&
		agent_has_rtp(ag) &&
		agent_has_rtp(ag->other);
}

static void hangup(void *arg)
{
	struct test *test = static_cast<struct test *>(arg);
	re_cancel();
}

static void rtp_start_handler(bool started, bool video_started, void *arg)
{
	if(!started){
		return;
	}
    
	struct agent *ag = static_cast<struct agent *>(arg);
    
	++ag->n_rtp_started;
    
	/* wait until both agents have RTP ( rcv + snd ) */
	if (agents_have_rtp(ag)) {
		tmr_start(&ag->test->tmr, 1, hangup, ag->test);
	}
}


static void mediaflow_estab_handler(const char *crypto, const char *codec,
				    void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);
	int err;

	++ag->n_estab;

#if 0
	re_printf(" *** Agent %s -- established [crypto=%s, raddr=%J]\n",
		  ag->name, crypto, raddr);
#endif

	/* this should always be true, when handler is called */
	ASSERT_TRUE(mediaflow_is_ready(ag->mf));

	if (CRYPTO_DTLS_SRTP == mediaflow_crypto(ag->mf)) {
		const struct tls_conn *sc;
		const char *name;

		sc = mediaflow_dtls_connection(ag->mf);
		ASSERT_TRUE(sc != NULL);

		name = tls_cipher_name(sc);
		ASSERT_EQ(0, re_regex(name, strlen(name), "ECDHE"));

		/* TODO: also compare cipher-name with ECDSA etc. */
	}

	/* wait until both agents are established */
	if (agents_are_established(ag)) {

		err = mediaflow_start_media(ag->mf);
		ASSERT_EQ(0, err);
		err = mediaflow_start_media(ag->other->mf);
		ASSERT_EQ(0, err);
	}
}


/* if this one is called, there was an error */
static void mediaflow_close_handler(int err, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	warning("mediaflow closed (%m)\n", err);

	abort_test(ag, err ? err : EPROTO);
}


static void destructor(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	mem_deref(ag->mf);
	mem_deref(ag->dtls);
}

static void sdp_exchange(struct agent *a, struct agent *b)
{
	char offer[4096], answer[4096];
	int err;

	/* verify SDP states */
	ASSERT_FALSE(mediaflow_got_sdp(a->mf));
	ASSERT_FALSE(mediaflow_got_sdp(b->mf));
	ASSERT_FALSE(mediaflow_sdp_is_complete(a->mf));
	ASSERT_FALSE(mediaflow_sdp_is_complete(b->mf));

	/* Create an SDP offer from "A" and then send it to "B" */
	err = mediaflow_generate_offer(a->mf, offer, sizeof(offer));
	ASSERT_EQ(0, err);

#if 0
	printf("--------------- sdp offer ----------------\n");
	printf("%s", offer);
	printf("------------------------------------------\n");
#endif

	err = mediaflow_offeranswer(b->mf, answer, sizeof(answer), offer);
	ASSERT_EQ(0, err);

#if 0
	printf("--------------- sdp answer ---------------\n");
	printf("%s", answer);
	printf("------------------------------------------\n");
#endif

	/* Create an SDP answer from "B" and send it to "A" */
	err = mediaflow_handle_answer(a->mf, answer);
	ASSERT_EQ(0, err);

	/* verify SDP states */
	ASSERT_TRUE(mediaflow_got_sdp(a->mf));
	ASSERT_TRUE(mediaflow_got_sdp(b->mf));
	ASSERT_TRUE(mediaflow_sdp_is_complete(a->mf));
	ASSERT_TRUE(mediaflow_sdp_is_complete(b->mf));

	/* verify a common crypto */
	ASSERT_EQ(mediaflow_crypto(a->mf), mediaflow_crypto(b->mf));
}


static void agent_alloc(struct agent **agp, struct test *test, bool offerer,
			enum media_crypto cryptos, const char *name,
			enum tls_keytype cert_type)
{
	struct sa laddr;
	struct agent *ag;
	int err;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->test = test;

	ag->offerer = offerer;
	str_ncpy(ag->name, name, sizeof(ag->name));

	sa_set_str(&laddr, "127.0.0.1", 0);

	if (cryptos & CRYPTO_DTLS_SRTP) {
		err = create_dtls_srtp_context(&ag->dtls, cert_type);
		ASSERT_EQ(0, err);
	}

	err = mediaflow_alloc(&ag->mf, name,
			      ag->dtls, &test->aucodecl, &laddr,
			      cryptos,
			      mediaflow_estab_handler,
			      NULL,
			      mediaflow_close_handler,
			      NULL,
			      ag);
	ASSERT_EQ(0, err);

	mediaflow_set_tag(ag->mf, ag->name);

	mediaflow_set_rtpstate_handler(ag->mf, rtp_start_handler);
    
	err = mediaflow_add_local_host_candidate(ag->mf, "en0", &laddr);
	ASSERT_EQ(0, err);

#if 0
	re_printf("[ %s ] agent allocated (%s, cryptos=%H)\n",
		  ag->name,
		  offerer ? "Offerer" : "Answerer",
		  mediaflow_cryptos_print, cryptos);
#endif

	*agp = ag;
}


static void test_b2b_base(enum tls_keytype a_cert,
			  enum tls_keytype b_cert,
			  enum media_crypto a_cryptos,
			  enum media_crypto b_cryptos,
			  enum media_crypto mode_expect,
			  enum media_setup a_setup,
			  enum media_setup b_setup,
			  enum media_setup a_setup_expect,
			  enum media_setup b_setup_expect)
{
	struct test test;
	struct agent *a = NULL, *b = NULL;
	int err;

#if 1
	log_set_min_level(LOG_LEVEL_WARN);
	log_enable_stderr(true);
#endif

	memset(&test, 0, sizeof(test));

	err = audummy_init(&test.aucodecl);
	ASSERT_EQ(0, err);

	/* initialization */
	agent_alloc(&a, &test, true, a_cryptos, "A", a_cert);
	agent_alloc(&b, &test, false, b_cryptos, "B", b_cert);
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);
	a->other = b;
	b->other = a;

	err = mediaflow_set_remote_userclientid(a->mf, b->name, b->name);
	ASSERT_EQ(0, err);
	err = mediaflow_set_remote_userclientid(b->mf, a->name, a->name);
	ASSERT_EQ(0, err);

	err = mediaflow_set_setup(a->mf, a_setup);
	ASSERT_EQ(0, err);
	err = mediaflow_set_setup(b->mf, b_setup);
	ASSERT_EQ(0, err);

	/* no async stuff now, can exchange SDPs ASAP */
	sdp_exchange(a, b);

	ASSERT_EQ(mode_expect, mediaflow_crypto(a->mf));
	ASSERT_EQ(mode_expect, mediaflow_crypto(b->mf));

	if (mode_expect == CRYPTO_DTLS_SRTP) {
		ASSERT_EQ(a_setup_expect, mediaflow_local_setup(a->mf));
		ASSERT_EQ(b_setup_expect, mediaflow_local_setup(b->mf));
	}

	/* Start ICE after SDP has been exchanged */
	err = mediaflow_start_ice(a->mf);
	ASSERT_EQ(0, err);
	err = mediaflow_start_ice(b->mf);
	ASSERT_EQ(0, err);

	/* start the main loop -- wait for network traffic */
	err = re_main_wait(5000);

#if 0
	re_printf("%H\n", mediaflow_summary, a->mf);
#endif

	if (err) {
		re_printf("main timeout!\n");
		a = (struct agent *)mem_deref(a);
		b = (struct agent *)mem_deref(b);
	}
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a->err);
	ASSERT_EQ(0, b->err);

	ASSERT_TRUE(mediaflow_is_rtpstarted(a->mf));
	ASSERT_TRUE(mediaflow_is_rtpstarted(b->mf));

	mem_deref(a);
	mem_deref(b);

	tmr_cancel(&test.tmr);
	audummy_close();
}


static void test_b2b(enum tls_keytype a_cert,
		     enum tls_keytype b_cert,
		     enum media_crypto a_cryptos,
		     enum media_crypto b_cryptos,
		     enum media_crypto mode_expect)
{
	test_b2b_base(a_cert, b_cert, a_cryptos, b_cryptos, mode_expect,
		      SETUP_ACTPASS, SETUP_ACTPASS,
		      SETUP_PASSIVE, SETUP_ACTIVE);
}


static void test_setup(enum media_setup a_setup,
		       enum media_setup b_setup,
		       enum media_setup a_setup_expect,
		       enum media_setup b_setup_expect)
{
	test_b2b_base(TLS_KEYTYPE_EC, TLS_KEYTYPE_EC,
		      CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP,
		      a_setup, b_setup, a_setup_expect, b_setup_expect);
}


TEST(media_crypto, none_and_none)
{
	test_b2b((enum tls_keytype)0, (enum tls_keytype)0,
		 CRYPTO_NONE, CRYPTO_NONE, CRYPTO_NONE);
}


TEST(media_crypto, dtlssrtp_and_dtlssrtp)
{
	test_b2b(TLS_KEYTYPE_EC, TLS_KEYTYPE_EC,
		 CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP);
}


TEST(media_crypto, mix_ecdsa_ecdsa_dtlssrtp_and_dtlssrtp)
{
	test_b2b(TLS_KEYTYPE_EC, TLS_KEYTYPE_EC,
		 CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP, CRYPTO_DTLS_SRTP);
}


TEST(media_crypto, kase_and_kase)
{
	test_b2b(TLS_KEYTYPE_EC, TLS_KEYTYPE_EC,
		 CRYPTO_KASE, CRYPTO_KASE, CRYPTO_KASE);
}


/*
 * Test-cases for DTLS-SRTP setup direction
 */


TEST(media_crypto, setup_actpass_actpass)
{
	test_setup(SETUP_ACTPASS, SETUP_ACTPASS, SETUP_PASSIVE, SETUP_ACTIVE);
}


TEST(media_crypto, setup_actpass_active)
{
	test_setup(SETUP_ACTPASS, SETUP_ACTIVE, SETUP_PASSIVE, SETUP_ACTIVE);
}


TEST(media_crypto, setup_actpass_passive)
{
	test_setup(SETUP_ACTPASS, SETUP_PASSIVE, SETUP_ACTIVE, SETUP_PASSIVE);
}


TEST(media_crypto, setup_passive_actpass)
{
	test_setup(SETUP_PASSIVE, SETUP_ACTPASS, SETUP_PASSIVE, SETUP_ACTIVE);
}


TEST(media_crypto, setup_passive_active)
{
	test_setup(SETUP_PASSIVE, SETUP_ACTIVE, SETUP_PASSIVE, SETUP_ACTIVE);
}


TEST(media_crypto, setup_active_actpass)
{
	test_setup(SETUP_ACTIVE, SETUP_ACTPASS, SETUP_ACTIVE, SETUP_PASSIVE);
}


TEST(media_crypto, setup_active_passive)
{
	test_setup(SETUP_ACTIVE, SETUP_PASSIVE, SETUP_ACTIVE, SETUP_PASSIVE);
}
