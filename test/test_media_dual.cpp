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
 * Test-cases that involve 2 instances of "struct mediaflow" running
 * in a back-to-back (B2B) setup.
 */


#define IS_TRICKLE(nat) ((nat)==MEDIAFLOW_TRICKLEICE_DUALSTACK)

#define NAT_IS_ACTIVE(nat) (nat) != MEDIAFLOW_ICELITE

#define HAS_TURN(nat)					\
	(nat) == MEDIAFLOW_TRICKLEICE_DUALSTACK ||	\
		(nat) == MEDIAFLOW_TURN

struct test {
	struct list aucodecl;
};


struct agent {
	TurnServer *turn_srv;
	struct test *test;
	struct tls *dtls;
	struct mediaflow *mf;
	struct agent *other;
	char name[64];
	bool offerer;
	enum mediaflow_nat nat;
	struct tmr tmr;
	int turn_proto;
	bool turn_secure;
	int err;

	unsigned n_lcand_expect;  /* all local candidates, incl. HOST */

	unsigned n_lcand;
	unsigned n_estab;
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

	if (ag->n_lcand_expect && ag->n_lcand < ag->n_lcand_expect)
		return false;

	return ag->n_estab;
}


static bool agents_are_established(const struct agent *ag)
{
	return ag &&
		agent_is_established(ag) &&
		agent_is_established(ag->other);
}


static bool is_conncheck_complete(const struct agent *ag)
{
	return mediaflow_is_ready(ag->mf);
}


static bool are_connchecks_complete(const struct agent *ag)
{
	return ag &&
		is_conncheck_complete(ag) &&
		is_conncheck_complete(ag->other);
}


/*
 * criteria for a test to be complete:
 *
 * - mediaflow must be established
 * - all connectivity checks must be complete
 * - RTP traffic must be done
 */
static bool are_we_complete(const struct agent *ag)
{
	return agents_are_established(ag) &&
		are_connchecks_complete(ag);
}


static void handle_one_candidate(struct agent *ag,
				 const struct zapi_candidate *zcand)
{
	struct ice_cand_attr icand;
	int err;

	if (0 == str_casecmp(zcand->sdp, "a=end-of-candidates")) {
		;
	}
	else {
		err = ice_cand_attr_decode(&icand, zcand->sdp);
		ASSERT_EQ(0, err);

		++ag->n_lcand;
	}

	/* expected ICE mode:
	 *
	 * - Trickle: this handler should be called for each SRFLX/RELAY cand
	 * - Lite:    this handler should NOT be called
	 */
	if (IS_TRICKLE(ag->nat)) {

		/* exchange ICE candidates */
		err = mediaflow_add_rcand(ag->other->mf, zcand->sdp,
					  zcand->mid, zcand->mline_index);
		ASSERT_EQ(0, err);
	}
}


static void mediaflow_localcand_handler(const struct zapi_candidate *candv,
					size_t candc, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	for (size_t i=0; i<candc; i++) {
		handle_one_candidate(ag, &candv[i]);
	}
}


static void mediaflow_estab_handler(const char *crypto, const char *codec,
				    const char *rtype, const struct sa *sa,
				    void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	++ag->n_estab;

#if 0
	re_printf(" *** Agent %s -- established [rtype=%s]\n",
		  ag->name, rtype);
#endif

	ASSERT_TRUE(mediaflow_is_ready(ag->mf));

	if (agents_are_established(ag)) {

		re_cancel();
	}
}


static void mediaflow_close_handler(int err, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);
	(void)ag;

	/* if this one is called, there was an error */
	ASSERT_TRUE(false);

	ag->err = EPROTO;
	re_cancel();
}


static void tmr_complete_handler(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	if (are_we_complete(ag)) {

		re_cancel();
		return;
	}

	tmr_start(&ag->tmr, 10, tmr_complete_handler, ag);
}


/** A dummy PCMU codec to test a fixed codec */
static struct aucodec dummy_pcmu = {
	.pt        = "0",
	.name      = "PCMU",
	.srate     = 8000,
	.ch        = 1,
	.enc_alloc = NULL,
	.ench      = NULL,
	.dec_alloc = NULL,
	.dech      = NULL,
};


static void destructor(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	tmr_cancel(&ag->tmr);

	mem_deref(ag->mf);
	mem_deref(ag->dtls);

	if (ag->turn_srv)
		delete ag->turn_srv;
}


static void agent_alloc(struct agent **agp, struct test *test, bool offerer,
			enum mediaflow_nat nat, const char *name,
			int turn_proto, bool turn_secure)
{
	struct sa laddr;
	struct agent *ag;
	int err;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->test = test;

	ag->offerer = offerer;
	ag->nat = nat;
	str_ncpy(ag->name, name, sizeof(ag->name));
	ag->turn_proto = turn_proto;
	ag->turn_secure = turn_secure;

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = create_dtls_srtp_context(&ag->dtls, CERT_TYPE_RSA);
	ASSERT_EQ(0, err);

	err = mediaflow_alloc(&ag->mf, ag->dtls, &test->aucodecl, &laddr,
			      nat, CRYPTO_DTLS_SRTP, false,
			      mediaflow_localcand_handler,
			      mediaflow_estab_handler,
			      mediaflow_close_handler,
			      ag);
	ASSERT_EQ(0, err);

	ASSERT_FALSE(mediaflow_is_ready(ag->mf));

	if (nat == MEDIAFLOW_ICELITE) {

		info("[ %s ] adding local host candidate (%J)\n",
			  name, &laddr);

		/* NOTE: at least one HOST candidate is needed */
		err = mediaflow_add_local_host_candidate(ag->mf,
							 "en0", &laddr);
		ASSERT_EQ(0, err);

		ag->n_lcand_expect += 1;  /* host */
	}

	mediaflow_set_tag(ag->mf, ag->name);

	if (HAS_TURN(nat)) {

		ag->turn_srv = new TurnServer;

		if (turn_proto == IPPROTO_UDP)
			ag->n_lcand_expect += 2;  /* SRFLX and RELAY */
		else
			ag->n_lcand_expect += 1;  /* RELAY */
	}

	tmr_start(&ag->tmr, 10, tmr_complete_handler, ag);

#if 0
	re_printf("[ %s ] agent allocated (%s, %s)\n",
		  ag->name,
		  offerer ? "Offerer" : "Answerer",
		  mediaflow_nat_name(nat));
#endif

	*agp = ag;
}


static bool find_host_cand(const char *sdp)
{
	return 0 == re_regex(sdp, str_len(sdp),
			     "a=candidate:[^ ]+ 1 UDP"
			     " [0-9]+ 127.0.0.1 [0-9]+ typ host",
			     NULL, NULL, NULL);
}


static void sdp(struct agent *a, struct agent *b)
{
	char offer[4096], answer[4096];
	int err;

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

	/* check for any candidates in SDP */
	if (find_host_cand(offer))
		++a->n_lcand;
	if (find_host_cand(answer))
		++b->n_lcand;
}


static void start_ice(struct agent *ag)
{
	const struct sa *srv;
	int err;

	err = mediaflow_start_ice(ag->mf);
	ASSERT_EQ(0, err);

	switch (ag->turn_proto) {

	case IPPROTO_UDP:
		err = mediaflow_gather_turn(ag->mf, &ag->turn_srv->addr,
					    "user", "pass");
		ASSERT_EQ(0, err);
		break;

	case IPPROTO_TCP:
		if (ag->turn_secure)
			srv = &ag->turn_srv->addr_tls;
		else
			srv = &ag->turn_srv->addr_tcp;

		err = mediaflow_gather_turn_tcp(ag->mf, srv,
						"user", "pass",
						ag->turn_secure);
		ASSERT_EQ(0, err);
		break;

	default:
		ASSERT_EQ(0, EPROTONOSUPPORT);
		break;
	}
}


static void test_b2b(enum mediaflow_nat a_nat, int a_turn_proto,
		     bool a_turn_secure, enum mediaflow_nat b_nat)
{
	struct test test;
	struct agent *a = NULL, *b = NULL;
	int err;

#if 1
	log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(false);
#endif

	memset(&test, 0, sizeof(test));

	aucodec_register(&test.aucodecl, &dummy_pcmu);

	/* initialization */
	agent_alloc(&a, &test, true, a_nat, "A", a_turn_proto, a_turn_secure);
	agent_alloc(&b, &test, false, b_nat, "B", IPPROTO_UDP, false);
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);
	a->other = b;
	b->other = a;

	sdp(a, b);

	/* start ICE connectivity check for the Trickle agents */
	if (NAT_IS_ACTIVE(a->nat))
		start_ice(a);
	if (NAT_IS_ACTIVE(b->nat))
		start_ice(b);

	/* start the main loop -- wait for network traffic */
	err = re_main_wait(5000);
	if (err) {
		warning("main timeout!\n");
		a = (struct agent *)mem_deref(a);
		b = (struct agent *)mem_deref(b);
	}
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a->err);
	ASSERT_EQ(0, b->err);

	/* verify results after traffic is complete */
	ASSERT_EQ(a->n_lcand_expect, a->n_lcand);
	ASSERT_EQ(b->n_lcand_expect, b->n_lcand);

	ASSERT_EQ(1, a->n_estab);
	ASSERT_EQ(1, b->n_estab);

	if (a->turn_srv) {

		switch (a_turn_proto) {

		case IPPROTO_UDP:
			ASSERT_TRUE(a->turn_srv->nrecv > 0);
			ASSERT_EQ(0, a->turn_srv->nrecv_tcp);
			ASSERT_EQ(0, a->turn_srv->nrecv_tls);
			break;

		case IPPROTO_TCP:
			ASSERT_EQ(0, a->turn_srv->nrecv);
			if (a_turn_secure) {
				ASSERT_EQ(0, a->turn_srv->nrecv_tcp);
				ASSERT_TRUE(a->turn_srv->nrecv_tls > 0);
			}
			else {
				ASSERT_TRUE(a->turn_srv->nrecv_tcp > 0);
				ASSERT_EQ(0, a->turn_srv->nrecv_tls);
			}
			break;
		}
	}

	mem_deref(a);
	mem_deref(b);

	aucodec_unregister(&dummy_pcmu);
}


TEST(media_dual, trickledual_and_trickle)
{
	test_b2b(MEDIAFLOW_TRICKLEICE_DUALSTACK, IPPROTO_UDP, false,
		 MEDIAFLOW_TRICKLEICE_DUALSTACK);
}


TEST(media_dual, trickledual_and_trickledual)
{
	test_b2b(MEDIAFLOW_TRICKLEICE_DUALSTACK, IPPROTO_UDP, false,
		 MEDIAFLOW_TRICKLEICE_DUALSTACK);
}


TEST(media_dual, trickledual_and_lite)
{
	test_b2b(MEDIAFLOW_TRICKLEICE_DUALSTACK, IPPROTO_UDP, false,
		 MEDIAFLOW_ICELITE);
}


TEST(media_dual, trickledual_turntcp_and_lite)
{
	test_b2b(MEDIAFLOW_TRICKLEICE_DUALSTACK, IPPROTO_TCP, false,
		 MEDIAFLOW_ICELITE);
}


TEST(media_dual, trickledual_turntls_and_lite)
{
	test_b2b(MEDIAFLOW_TRICKLEICE_DUALSTACK, IPPROTO_TCP, true,
		 MEDIAFLOW_ICELITE);
}


TEST(media_dual, turn_and_turn)
{
	test_b2b(MEDIAFLOW_TURN, IPPROTO_UDP, false,
		 MEDIAFLOW_TURN);
}
