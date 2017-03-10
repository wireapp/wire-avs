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


#define IS_TRICKLE(nat) (nat)==MEDIAFLOW_TRICKLEICE_DUALSTACK

#define HAS_TURN(nat)					\
	(nat) == MEDIAFLOW_TRICKLEICE_DUALSTACK


struct test {
	struct list aucodecl;
};


struct agent {
	TurnServer *turn_srvv[2];
	size_t turn_srvc;
	struct test *test;
	struct tls *dtls;
	struct mediaflow *mf;
	struct dce *dce;
	struct dce_channel *dce_ch;
	struct agent *other;
	char name[64];
	bool offerer;
	enum mediaflow_nat nat;
	struct tmr tmr;
	int turn_proto;
	bool turn_secure;
	bool datachan;
	int err;

	unsigned n_lcand_expect;  /* all local candidates, incl. HOST */

	unsigned n_lcand;
	unsigned n_estab;
	unsigned n_datachan_estab;
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

	ASSERT_TRUE(mediaflow_dtls_peer_isset(ag->mf));

	if (agents_are_established(ag) && !ag->datachan) {

		re_cancel();
	}
}


static void mediaflow_close_handler(int err, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);
	(void)ag;

	/* if this one is called, there was an error */
	ASSERT_TRUE(false);

	abort_test(ag, err ? err : EPROTO);
}


static void tmr_complete_handler(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	if (are_we_complete(ag) && !ag->datachan) {

		re_cancel();
		return;
	}

	tmr_start(&ag->tmr, 5, tmr_complete_handler, ag);
}


static void data_estab_handler(void *arg)
{
	struct agent *ag = (struct agent *)arg;

	++ag->n_datachan_estab;

	if (ag->other->n_datachan_estab) {
		info("both datachannels established -- stop.\n");
		re_cancel();
	}

}


static void data_channel_handler(int chid,
				 uint8_t *data, size_t len, void *arg)
{
	struct agent *ag = (struct agent *)arg;
	(void)data;
	info("datachan recv %zu bytes on channel %d\n", len, chid);
}


static void destructor(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);
	size_t i;

	tmr_cancel(&ag->tmr);

	mem_deref(ag->mf);
	mem_deref(ag->dtls);

	for (i=0; i<ag->turn_srvc; i++) {
		if (ag->turn_srvv[i])
			delete ag->turn_srvv[i];
	}
}


static void agent_alloc(struct agent **agp, struct test *test, bool offerer,
			const char *name,
			int turn_proto, bool turn_secure, bool datachan,
			size_t turn_srvc)
{
	struct sa laddr;
	struct agent *ag;
	enum mediaflow_nat nat = MEDIAFLOW_TRICKLEICE_DUALSTACK;
	int err;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->test = test;

	ag->offerer = offerer;
	ag->nat = nat;
	str_ncpy(ag->name, name, sizeof(ag->name));
	ag->turn_proto = turn_proto;
	ag->turn_secure = turn_secure;
	ag->datachan = datachan;
	ag->turn_srvc = turn_srvc;

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = create_dtls_srtp_context(&ag->dtls, TLS_KEYTYPE_EC);
	ASSERT_EQ(0, err);

	err = mediaflow_alloc(&ag->mf, ag->dtls, &test->aucodecl, &laddr,
			      nat, CRYPTO_DTLS_SRTP,
			      mediaflow_localcand_handler,
			      mediaflow_estab_handler,
			      mediaflow_close_handler,
			      ag);
	ASSERT_EQ(0, err);

	ASSERT_FALSE(mediaflow_is_ready(ag->mf));

	if (1) {

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

		size_t i;

		ASSERT_TRUE(turn_srvc > 0);

		for (i=0; i<turn_srvc; i++) {
			ag->turn_srvv[i] = new TurnServer;
		}

		if (turn_proto == IPPROTO_UDP)
			ag->n_lcand_expect += (2 * turn_srvc);  /* SRFLX and RELAY */
		else
			ag->n_lcand_expect += (1 * turn_srvc);  /* RELAY */
	}

	if (datachan) {
		struct dce *dce;
		ag->dce = mediaflow_get_dce(ag->mf);

		ASSERT_TRUE(ag->dce != NULL);

		err = dce_channel_alloc(&ag->dce_ch,
					ag->dce,
					"calling-3.0",
					"",
					data_estab_handler,
					NULL,
					NULL,
					data_channel_handler,
					ag);
		ASSERT_EQ(0, err);

		err = mediaflow_add_data(ag->mf);
		ASSERT_EQ(0, err);
	}

	tmr_start(&ag->tmr, 5, tmr_complete_handler, ag);

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
		for (size_t i=0; i<ag->turn_srvc; i++) {
			err = mediaflow_gather_turn(ag->mf,
						    &ag->turn_srvv[i]->addr,
						    "user", "pass");
			ASSERT_EQ(0, err);
		}
		break;

	case IPPROTO_TCP:
		if (ag->turn_secure)
			srv = &ag->turn_srvv[0]->addr_tls;
		else
			srv = &ag->turn_srvv[0]->addr_tcp;

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


static void test_b2b(int a_turn_proto,
		     bool a_turn_secure,
		     bool datachan, size_t turn_srvc)
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

	if (datachan) {
		err = dce_init();
		ASSERT_EQ(0, err);
	}

	/* initialization */
	agent_alloc(&a, &test, true, "A", a_turn_proto, a_turn_secure,
		    datachan, turn_srvc);
	agent_alloc(&b, &test, false, "B", IPPROTO_UDP, false,
		    datachan, turn_srvc);
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);
	a->other = b;
	b->other = a;

	sdp(a, b);

	/* verify that DataChannels is negotiated correctly */
	if (datachan) {
		ASSERT_TRUE(mediaflow_has_data(a->mf));
		ASSERT_TRUE(mediaflow_has_data(b->mf));
	}
	else {
		ASSERT_FALSE(mediaflow_has_data(a->mf));
		ASSERT_FALSE(mediaflow_has_data(b->mf));
	}

	/* start ICE connectivity check for the Trickle agents */
	start_ice(a);
	start_ice(b);

	/* start the main loop -- wait for network traffic */
	err = re_main_wait(10000);
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

	/* verify if datachannel was established */
	if (datachan) {
		ASSERT_EQ(1, a->n_datachan_estab);
		ASSERT_EQ(1, b->n_datachan_estab);
	}
	else {
		ASSERT_EQ(0, a->n_datachan_estab);
		ASSERT_EQ(0, b->n_datachan_estab);
	}

#if 0
	re_printf("TURN-Servers summary for A:\n");
	for (size_t i=0; i<turn_srvc; i++) {

		re_printf("  TURN #%zu:  nrecv=%u\n",
			  i, a->turn_srvv[i]->nrecv);
	}
#endif

	if (a->turn_srvc > 0) {

		switch (a_turn_proto) {

		case IPPROTO_UDP:
			for (size_t i=0; i<turn_srvc; i++) {
				ASSERT_TRUE(a->turn_srvv[i]->nrecv > 0);
			}
			ASSERT_EQ(0, a->turn_srvv[0]->nrecv_tcp);
			ASSERT_EQ(0, a->turn_srvv[0]->nrecv_tls);
			break;

		case IPPROTO_TCP:
			ASSERT_EQ(0, a->turn_srvv[0]->nrecv);
			if (a_turn_secure) {
				ASSERT_EQ(0, a->turn_srvv[0]->nrecv_tcp);
				ASSERT_TRUE(a->turn_srvv[0]->nrecv_tls > 0);
			}
			else {
				ASSERT_TRUE(a->turn_srvv[0]->nrecv_tcp > 0);
				ASSERT_EQ(0, a->turn_srvv[0]->nrecv_tls);
			}
			break;
		}
	}

	mem_deref(a);
	mem_deref(b);

	audummy_close();

	dce_close();
}


TEST(media_dual, trickledual_and_trickle)
{
	test_b2b(IPPROTO_UDP, false,
		 false, 1);
}


TEST(media_dual, trickledual_and_trickledual)
{
	test_b2b(IPPROTO_UDP, false,
		 false, 1);
}


TEST(media_dual, trickledual_turntcp_and_lite)
{
	test_b2b(IPPROTO_TCP, false,
		 false, 1);
}


TEST(media_dual, trickledual_turntls_and_lite)
{
	test_b2b(IPPROTO_TCP, true,
		 false, 1);
}


TEST(media_dual, data_channels)
{
	test_b2b(IPPROTO_UDP, false,
		 true, 1);
}


TEST(media_dual, trickle_with_2_turn_servers)
{
	test_b2b(IPPROTO_UDP, false,
		 false, 2);
}
