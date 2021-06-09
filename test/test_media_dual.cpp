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
#include <rew.h>
#include <avs.h>
#include <gtest/gtest.h>
#include "fakes.hpp"
#include "ztest.h"


/*
 * Test-cases that involve 2 instances of "struct mediaflow" running
 * in a back-to-back (B2B) setup.
 */


#define PRIVACY true
#define DATACHAN true


class MediaDual;


struct agent {
	TurnServer *turn_srvv[2];
	size_t turn_srvc;
	MediaDual *fix;              /* pointer to parent */
	struct tls *dtls;
	struct mediaflow *mf;
	struct dce *dce;
	struct dce_channel *dce_ch;
	struct agent *other;
	char name[64];
	bool offerer;
	struct tmr tmr;
	int turn_proto;
	bool turn_secure;
	bool datachan;
	int err;

	unsigned n_lcand_expect;  /* all local candidates, incl. HOST */

	unsigned n_estab;
	unsigned n_datachan_estab;
	unsigned n_gather;
};


static void sdp_exchange(struct agent *a, struct agent *b);
static void start_ice(struct agent *ag);


static void abort_test(struct agent *ag, int err)
{
	ag->err = err;
	re_cancel();
}


static bool agent_is_established(const struct agent *ag)
{
	if (!ag)
		return false;

#if 0
	if (ag->n_lcand_expect && ag->n_lcand < ag->n_lcand_expect)
		return false;
#endif

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


static void mediaflow_estab_handler(const char *crypto, const char *codec,
				    void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);
	struct ice_candpair *pair;

	++ag->n_estab;

#if 1
	info("[ %s ] -- established\n", ag->name);
#endif

	ASSERT_TRUE(mediaflow_is_ready(ag->mf));

	ASSERT_TRUE(mediaflow_dtls_peer_isset(ag->mf));

#if 0
	re_printf("[ %s ] selected pair: %H\n", ag->name,
		  trice_candpair_debug,
		  mediaflow_selected_pair(ag->mf)
		  );
#endif

	/* verify the selected pair */
	pair = mediaflow_selected_pair(ag->mf);
	ASSERT_TRUE(pair != NULL);
	ASSERT_EQ(ICE_CANDPAIR_SUCCEEDED, pair->state);
	ASSERT_TRUE(pair->pprio > 0);
	ASSERT_TRUE(pair->valid);
	ASSERT_TRUE(pair->estab);
	ASSERT_EQ(0, pair->err);
	ASSERT_EQ(0, pair->scode);

	if (agents_are_established(ag) && !ag->datachan) {

		struct ice_candpair *opair;

		/* wait until both are established */
		opair = mediaflow_selected_pair(ag->other->mf);

		/* cross-check local and remote candidate */

		ASSERT_EQ(pair->lcand->attr.type, opair->rcand->attr.type);
		ASSERT_EQ(pair->rcand->attr.type, opair->lcand->attr.type);

		ASSERT_TRUE(sa_cmp(&pair->lcand->attr.addr,
				   &opair->rcand->attr.addr, SA_ALL));
		ASSERT_TRUE(sa_cmp(&pair->rcand->attr.addr,
				   &opair->lcand->attr.addr, SA_ALL));

		/* stop the test */
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


static void mediaflow_gather_handler(void *arg)
{
	struct agent *ag = (struct agent *)arg;

	info("[ %s ] gathering complete\n", ag->name);

	if (ag->n_gather)
		return;

	++ag->n_gather;

	if (mediaflow_is_gathered(ag->other->mf)) {

		sdp_exchange(ag, ag->other);

#if 0
		/* verify that DataChannels is negotiated correctly */
		if (datachan) {
			ASSERT_TRUE(mediaflow_has_data(a->mf));
			ASSERT_TRUE(mediaflow_has_data(b->mf));
		}
		else {
			ASSERT_FALSE(mediaflow_has_data(a->mf));
			ASSERT_FALSE(mediaflow_has_data(b->mf));
		}
#endif
	}
}


class MediaDual : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		int err;

		tmr_init(&tmr_sdp);

#if 1
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		err = dce_init();
		ASSERT_EQ(0, err);

		err = audummy_init(&aucodecl);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		audummy_close();

		dce_close();

		tmr_cancel(&tmr_sdp);
	}

	void test_b2b(int a_turn_proto,
		      bool a_turn_secure,
		      bool datachan, size_t turn_srvc);

public:
	struct tmr tmr_sdp;
	struct list aucodecl = LIST_INIT;
	enum ice_role role = ICE_ROLE_UNKNOWN;
	uint16_t sim_error = 0;
	bool privacy = false;
	bool delay_sdp = false;
};


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


static void agent_alloc(struct agent **agp, MediaDual *fix, bool offerer,
			const char *name,
			int turn_proto, bool turn_secure, bool datachan,
			size_t turn_srvc)
{
	struct sa laddr;
	struct agent *ag;
	int err;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->fix = fix;

	ag->offerer = offerer;
	str_ncpy(ag->name, name, sizeof(ag->name));
	ag->turn_proto = turn_proto;
	ag->turn_secure = turn_secure;
	ag->datachan = datachan;
	ag->turn_srvc = turn_srvc;

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = create_dtls_srtp_context(&ag->dtls, TLS_KEYTYPE_EC);
	ASSERT_EQ(0, err);

	err = mediaflow_alloc(&ag->mf, name,
			      ag->dtls, &fix->aucodecl, &laddr,
			      CRYPTO_DTLS_SRTP,
			      mediaflow_estab_handler,
			      NULL,
			      mediaflow_close_handler,
			      NULL,
			      ag);
	ASSERT_EQ(0, err);

	if (fix->role != ICE_ROLE_UNKNOWN) {
		mediaflow_set_ice_role(ag->mf, fix->role);
	}

	mediaflow_enable_privacy(ag->mf, fix->privacy);

	if (turn_srvc) {
		/* NOTE: gathering is ALWAYS used */
		mediaflow_set_gather_handler(ag->mf, mediaflow_gather_handler);
	}

	ASSERT_FALSE(mediaflow_is_ready(ag->mf));

	info("[ %s ] adding local host candidate (%J)\n",
	     name, &laddr);

	/* NOTE: at least one HOST candidate is needed */
	err = mediaflow_add_local_host_candidate(ag->mf, "en0", &laddr);
	ASSERT_EQ(0, err);

	ag->n_lcand_expect += 1;  /* host */

	mediaflow_set_tag(ag->mf, ag->name);

	if (turn_srvc) {

		size_t i;

		for (i=0; i<turn_srvc; i++) {
			ag->turn_srvv[i] = new TurnServer;
		}

		if (turn_proto == IPPROTO_UDP)
			ag->n_lcand_expect += (2 * turn_srvc);  /* SRFLX and RELAY */
		else
			ag->n_lcand_expect += (1 * turn_srvc);  /* RELAY */
	}
	else {
		ASSERT_TRUE(mediaflow_is_gathered(ag->mf));
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

	*agp = ag;
}


static void sdp_exchange_offer(struct agent *a, struct agent *b)
{
	char offer[4096];
	int err;

	/* Create an SDP offer from "A" and then send it to "B" */
	err = mediaflow_generate_offer(a->mf, offer, sizeof(offer));
	ASSERT_EQ(0, err);

#if 0
	printf("--------------- sdp offer ----------------\n");
	printf("%s", offer);
	printf("------------------------------------------\n");
#endif

	err = mediaflow_handle_offer(b->mf, offer);
	ASSERT_EQ(0, err);

	/* start ICE connectivity check for the Trickle agents */
	start_ice(b);
}


static void sdp_exchange_answer(struct agent *a, struct agent *b)
{
	char answer[4096];
	int err;

	err = mediaflow_generate_answer(b->mf, answer, sizeof(answer));
	ASSERT_EQ(0, err);

#if 0
	printf("--------------- sdp answer ---------------\n");
	printf("%s", answer);
	printf("------------------------------------------\n");
#endif

	/* Create an SDP answer from "B" and send it to "A" */
	err = mediaflow_handle_answer(a->mf, answer);
	ASSERT_EQ(0, err);

	start_ice(a);
}


static void tmr_sdp_handler(void *arg)
{
	struct agent *a = (struct agent *)arg;

	sdp_exchange_answer(a, a->other);
}


static void sdp_exchange(struct agent *a, struct agent *b)
{
	sdp_exchange_offer(a, b);

	if (a->fix->delay_sdp) {
		tmr_start(&a->fix->tmr_sdp, 500, tmr_sdp_handler, a);
	}
	else {
		sdp_exchange_answer(a, b);
	}
}


static void start_gathering(struct agent *ag)
{
	const struct sa *srv;
	int err;

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


static void start_ice(struct agent *ag)
{
	int err;

	err = mediaflow_start_ice(ag->mf);
	ASSERT_EQ(0, err);
}


void MediaDual::test_b2b(int a_turn_proto,
			 bool a_turn_secure,
			 bool datachan, size_t turn_srvc)
{
	struct agent *a = NULL, *b = NULL;
	int err;

	/* initialization */
	agent_alloc(&a, this, true, "A", a_turn_proto, a_turn_secure,
		    datachan, turn_srvc);
	agent_alloc(&b, this, false, "B", IPPROTO_UDP, false,
		    datachan, turn_srvc);
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);
	a->other = b;
	b->other = a;

	/* The first TURN-server should fail */
	if (sim_error) {

		/* silence warnings .. */
		log_set_min_level(LOG_LEVEL_ERROR);

		a->turn_srvv[0]->set_sim_error(441);
	}

	if (turn_srvc) {
		start_gathering(a);
		start_gathering(b);
	}
	else {
		sdp_exchange(a, b);
	}

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

#if 0
	re_printf("%H\n", mediaflow_print_ice, a->mf);
	re_printf("%H\n", mediaflow_print_ice, b->mf);
#endif

	/* verify results after traffic is complete */
	if (turn_srvc) {
		ASSERT_EQ(1, a->n_gather);
		ASSERT_EQ(1, b->n_gather);
	}
	else {
		ASSERT_EQ(0, a->n_gather);
		ASSERT_EQ(0, b->n_gather);
	}

	ASSERT_EQ(1, a->n_estab);
	ASSERT_EQ(1, b->n_estab);

	/* verify that DataChannels is negotiated correctly */
	if (datachan) {
		ASSERT_TRUE(mediaflow_has_data(a->mf));
		ASSERT_TRUE(mediaflow_has_data(b->mf));
	}
	else {
		ASSERT_FALSE(mediaflow_has_data(a->mf));
		ASSERT_FALSE(mediaflow_has_data(b->mf));
	}

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

		size_t nrecv = 0;

		switch (a_turn_proto) {

		case IPPROTO_UDP:
			for (size_t i=0; i<turn_srvc; i++) {
				nrecv += a->turn_srvv[i]->nrecv;
			}
			ASSERT_TRUE(nrecv > 0);
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

	if (privacy) {

		/* verify local candidates */
		ASSERT_EQ(0, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_HOST));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_SRFLX));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_PRFLX));
		ASSERT_EQ(1, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_RELAY));

		/* verify remote candidates */
		ASSERT_EQ(0, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_HOST));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_SRFLX));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_PRFLX));
		ASSERT_EQ(1, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_RELAY));

	}
	else {
		const size_t exp_relay = turn_srvc > 0 ? 1 : 0;

		/* NOTE: if everything works as expected, we should have
		 *       zero PRFLX candidates.
		 */

		/* verify local candidates */
		ASSERT_EQ(1, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_HOST));
		//ASSERT_EQ(0, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_SRFLX));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_PRFLX));
		ASSERT_EQ(exp_relay,
			  mediaflow_candc(a->mf, 1, ICE_CAND_TYPE_RELAY));

		/* verify remote candidates */
		ASSERT_EQ(1, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_HOST));
		//ASSERT_EQ(0, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_SRFLX));
		ASSERT_EQ(0, mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_PRFLX));
		ASSERT_EQ(exp_relay,
			  mediaflow_candc(a->mf, 0, ICE_CAND_TYPE_RELAY));
	}

	/* verify ICE roles after test */
	enum ice_role role_a, role_b;

	role_a = mediaflow_local_role(a->mf);
	role_b = mediaflow_local_role(b->mf);

	switch (role_a) {

	case ICE_ROLE_CONTROLLING:
		ASSERT_EQ(ICE_ROLE_CONTROLLED, role_b);
		break;

	case ICE_ROLE_CONTROLLED:
		ASSERT_EQ(ICE_ROLE_CONTROLLING, role_b);
		break;

	default:
		ASSERT_TRUE(false);
		break;
	}

	/* cleanup */
	mem_deref(a);
	mem_deref(b);
}


TEST_F(MediaDual, trickledual_and_trickle)
{
	test_b2b(IPPROTO_UDP, false, false, 1);
}


TEST_F(MediaDual, trickledual_and_trickledual)
{
	test_b2b(IPPROTO_UDP, false, false, 1);
}


TEST_F(MediaDual, trickledual_turntcp_and_lite)
{
	test_b2b(IPPROTO_TCP, false, false, 1);
}


TEST_F(MediaDual, trickledual_turntls_and_lite)
{
	test_b2b(IPPROTO_TCP, true, false, 1);
}


TEST_F(MediaDual, data_channels)
{
	test_b2b(IPPROTO_UDP, false, true, 1);
}


TEST_F(MediaDual, trickle_with_2_turn_servers)
{
	test_b2b(IPPROTO_UDP, false, false, 2);
}


TEST_F(MediaDual, ice_and_privacy)
{
	privacy = PRIVACY;

	test_b2b(IPPROTO_UDP, false, false, 1);
}


TEST_F(MediaDual, ice_and_turn_failover)
{
	privacy = PRIVACY;

	sim_error = 441;

	test_b2b(IPPROTO_UDP, false, false, 2);
}


TEST_F(MediaDual, ice_role_conflict)
{
	/* silence warnings .. */
	log_set_min_level(LOG_LEVEL_ERROR);

	role = ICE_ROLE_CONTROLLING;

	test_b2b(IPPROTO_UDP, false, true, 0);
}


TEST_F(MediaDual, ice_upgrade_prflx_to_host)
{
	delay_sdp = true;

	test_b2b(IPPROTO_UDP, false, true, 0);
}
