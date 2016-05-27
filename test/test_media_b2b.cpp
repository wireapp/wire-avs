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


#define NUM_PACKETS 2
#define TS 160
#define SSRC 0x01020304
#define NTP_SEC 1234
#define NTP_FRAC 5678
#define PSENT 1
#define OSENT 160


enum mode {
	TRICKLE_STUN,
	TRICKLE_TURN,
	TRICKLE_TURN_ONLY,
	LITE,
};

#define IS_TRICKLE(mode) ((mode)==TRICKLE_STUN ||	\
			  (mode)==TRICKLE_TURN ||	\
			  (mode)==TRICKLE_TURN_ONLY)
#define HAS_TURN(mode) ((mode)==TRICKLE_TURN || (mode)==TRICKLE_TURN_ONLY)

struct test {
	struct list aucodecl;
	unsigned n_sdp_exch;
};


struct agent {
	StunServer *stun_srv;  /* either STUN ...     */
	TurnServer *turn_srv;  /* ... or TURN is used */
	struct test *test;
	struct tls *dtls;
	struct mediaflow *mf;
	struct agent *other;
	char name[64];
	bool offerer;
	enum mode mode;
	int err;
	struct tmr tmr;

	unsigned n_lcand_expect;  /* all local candidates, incl. HOST */

	uint16_t seq;
	unsigned n_lcand;
	unsigned n_estab;
	unsigned n_rtp_sent;
	unsigned n_rtp_recv;
	unsigned n_rtcp_sent;
	unsigned n_rtcp_recv;
};

static const uint8_t payload[160] = {0};
static void sdp_exchange(struct agent *a, struct agent *b);


static void abort_test(struct agent *ag, int err)
{
	ag->err = err;
	re_cancel();
}


static void send_rtp_packet(struct agent *ag)
{
	struct rtp_header hdr;
	int err;

	memset(&hdr, 0, sizeof(hdr));

	hdr.ver = RTP_VERSION;
	hdr.seq = ++ag->seq;
	hdr.ts  = TS;
	hdr.ssrc = SSRC;

	err = mediaflow_send_rtp(ag->mf, &hdr, payload, sizeof(payload));
	ASSERT_EQ(0, err);

	++ag->n_rtp_sent;
}


static int rr_encode_handler(struct mbuf *mb, void *arg)
{
	struct agent *ag = (struct agent *)arg;
	int err = 0;

	err |= mbuf_write_u32(mb, htonl(SSRC));
	err |= mbuf_write_u32(mb, htonl(0));
	err |= mbuf_write_u32(mb, htonl(ag->seq));
	err |= mbuf_write_u32(mb, htonl(42));
	err |= mbuf_write_u32(mb, htonl(0));
	err |= mbuf_write_u32(mb, htonl(0));

	return err;
}


static void send_rtcp_packet(struct agent *ag)
{
	uint32_t ssrc = SSRC;
	uint32_t ntp_sec = NTP_SEC;
	uint32_t ntp_frac = NTP_FRAC;
	uint32_t rtp_ts = TS;
	uint32_t psent = PSENT;
	uint32_t osent = OSENT;
	int err;

	struct mbuf *mb = mbuf_alloc(256);

	err = rtcp_encode(mb, RTCP_SR, 1,
			  ssrc, ntp_sec, ntp_frac, rtp_ts, psent, osent,
                          rr_encode_handler, ag);
	ASSERT_EQ(0, err);

	err = mediaflow_send_raw_rtcp(ag->mf, mb->buf, mb->end);
	ASSERT_EQ(0, err);

	++ag->n_rtcp_sent;

	mem_deref(mb);
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


static bool is_traffic_complete(const struct agent *ag)
{
	return (ag->n_rtp_recv > 0 && ag->n_rtcp_recv > 0);
}


static bool are_traffics_complete(const struct agent *ag)
{
	return is_traffic_complete(ag) &&
		is_traffic_complete(ag->other);
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
		are_connchecks_complete(ag) &&
		are_traffics_complete(ag);
}


static void send_traffic(struct agent *ag)
{
	for (int i=0; i<NUM_PACKETS; i++) {
		send_rtp_packet(ag);
		send_rtcp_packet(ag);
	}
}


static void mediaflow_estab_handler(const char *crypto, const char *codec,
				    const char *type, const struct sa *sa,
				    void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	++ag->n_estab;

#if 0
	re_printf(" *** Agent %s -- established [type=%s]\n", ag->name, type);
#endif

	ASSERT_TRUE(mediaflow_is_ready(ag->mf));

	if (agents_are_established(ag)) {

		send_traffic(ag);
		send_traffic(ag->other);
	}
}


static void mediaflow_rtp_handler(const struct sa *src,
				  const struct rtp_header *hdr,
				  struct mbuf *mb, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	++ag->n_rtp_recv;

	ASSERT_EQ(RTP_VERSION, hdr->ver);
	ASSERT_TRUE(hdr->seq < 10);
	ASSERT_EQ(TS, hdr->ts);
	ASSERT_EQ(SSRC, hdr->ssrc);

	ASSERT_EQ(sizeof(payload), mbuf_get_left(mb));
	ASSERT_TRUE(0 == memcmp(mbuf_buf(mb), payload, sizeof(payload)));

#if 1
	if (are_we_complete(ag)) {
		re_cancel();
	}
#endif
}


static void mediaflow_rtcp_handler(struct rtp_sock *rtp,
				   struct rtcp_msg *msg, void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	++ag->n_rtcp_recv;

	/* verify content of the RTCP packet */
	ASSERT_EQ(RTCP_VERSION, msg->hdr.version);
	ASSERT_EQ(1, msg->hdr.count);
	ASSERT_EQ(RTCP_SR, msg->hdr.pt);
	ASSERT_EQ(12, msg->hdr.length);

	ASSERT_EQ(SSRC, msg->r.sr.ssrc);
	ASSERT_EQ(NTP_SEC, msg->r.sr.ntp_sec);
	ASSERT_EQ(NTP_FRAC, msg->r.sr.ntp_frac);
	ASSERT_EQ(TS, msg->r.sr.rtp_ts);
	ASSERT_EQ(PSENT, msg->r.sr.psent);
	ASSERT_EQ(OSENT, msg->r.sr.osent);

	if (are_we_complete(ag)) {
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

	if (ag->stun_srv)
		delete ag->stun_srv;
	if (ag->turn_srv)
		delete ag->turn_srv;
}


static bool are_both_gathered(const struct agent *ag)
{
	return mediaflow_is_gathered(ag->mf) &&
		mediaflow_is_gathered(ag->other->mf);
}


static void start_ice_conncheck(struct agent *ag)
{
	int err;

	err = mediaflow_start_ice(ag->mf);
	ASSERT_EQ(0, err);
}


/* start ICE connectivity check for the Trickle agents */
static void start_both_ice(struct agent *ag)
{
	struct agent *other = ag->other;

	if (IS_TRICKLE(ag->mode))
		start_ice_conncheck(ag);
	if (IS_TRICKLE(other->mode))
		start_ice_conncheck(other);
}


static void gather_handler(void *arg)
{
	struct agent *ag = static_cast<struct agent *>(arg);

	if (are_both_gathered(ag)) {
		sdp_exchange(ag, ag->other);

		/* Start ICE after SDP has been exchanged */
		start_both_ice(ag);
	}
}


static void gather_server(struct agent *ag)
{
	int err;

	if (ag->turn_srv) {
		err = mediaflow_gather_turn(ag->mf, &ag->turn_srv->addr,
					    "user", "pass");
		ASSERT_EQ(0, err);
	}
	else if (ag->stun_srv) {
		err = mediaflow_gather_stun(ag->mf, &ag->stun_srv->addr);
		ASSERT_EQ(0, err);
	}
}


static void agent_alloc(struct agent **agp, struct test *test, bool offerer,
			enum mode mode, const char *name)
{
	struct sa laddr;
	struct agent *ag;
	enum mediaflow_nat nat;
	bool host_cand = (mode != TRICKLE_TURN_ONLY);
	int err;

	nat = IS_TRICKLE(mode) ?
		MEDIAFLOW_TRICKLEICE_DUALSTACK : MEDIAFLOW_ICELITE;

	ag = (struct agent *)mem_zalloc(sizeof(*ag), destructor);
	ASSERT_TRUE(ag != NULL);

	ag->test = test;

	ag->offerer = offerer;
	ag->mode = mode;
	str_ncpy(ag->name, name, sizeof(ag->name));

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = create_dtls_srtp_context(&ag->dtls, CERT_TYPE_RSA);
	ASSERT_EQ(0, err);

	err = mediaflow_alloc(&ag->mf, ag->dtls, &test->aucodecl, &laddr,
			      nat, CRYPTO_DTLS_SRTP, false,
			      NULL, /*mediaflow_localcand_handler,*/
			      mediaflow_estab_handler,
			      mediaflow_close_handler,
			      ag);
	ASSERT_EQ(0, err);

	mediaflow_set_gather_handler(ag->mf, gather_handler);

	mediaflow_set_rtp_handler(ag->mf, 0, 0,
				  NULL,
				  mediaflow_rtp_handler,
				  mediaflow_rtcp_handler);

	if (host_cand) {
		/* NOTE: at least one HOST candidate is needed */
		err = mediaflow_add_local_host_candidate(ag->mf,
							 "en0", &laddr);
		ASSERT_EQ(0, err);

		ag->n_lcand_expect += 1;  /* host */
	}

	mediaflow_set_tag(ag->mf, ag->name);

	if (IS_TRICKLE(mode)) {

		switch (mode) {

		case TRICKLE_STUN:
			ag->stun_srv = new StunServer;
			//ag->n_lcand_expect += 1;
			break;

		case TRICKLE_TURN:
			ag->turn_srv = new TurnServer;
			//ag->n_lcand_expect += 2;
			break;

		case TRICKLE_TURN_ONLY:
			ag->turn_srv = new TurnServer;
			//ag->n_lcand_expect += 1;
			break;

		default:
			break;
		}
	}

	gather_server(ag);

	tmr_start(&ag->tmr, 10, tmr_complete_handler, ag);

#if 0
	re_printf("[ %s ] agent allocated (%s, %s, %s)\n",
		  ag->name,
		  offerer ? "Offerer" : "Answerer",
		  IS_TRICKLE(mode) ? "Trickle-ICE" : "ICE-lite",
		  ag->turn_srv ? "TURN" : "STUN");
#endif

	*agp = ag;
}


// XXX: this finds only ONE candidate
static bool find_host_cand(const char *sdp)
{
	return 0 == re_regex(sdp, str_len(sdp),
			     "a=candidate:[^ ]+ 1 UDP"
			     " [0-9]+ 127.0.0.1 [0-9]+ typ host",
			     NULL, NULL, NULL);
}


static bool find_tcp_cand(const char *sdp)
{
	return 0 == re_regex(sdp, str_len(sdp),
			     "a=candidate:[^ ]+ 1 TCP [0-9]+",
			     NULL, NULL);
}


static void sdp_exchange(struct agent *a, struct agent *b)
{
	char offer[4096], answer[4096];
	int err;

	ASSERT_EQ(0, a->test->n_sdp_exch);

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

	ASSERT_FALSE(find_tcp_cand(offer));
	ASSERT_FALSE(find_tcp_cand(answer));

	/* check for any candidates in SDP */
	if (find_host_cand(offer))
		++a->n_lcand;
	if (find_host_cand(answer))
		++b->n_lcand;

	++a->test->n_sdp_exch;
}


static void test_b2b(enum mode a_mode, enum mode b_mode)
{
	struct test test;
	struct agent *a = NULL, *b = NULL;
	int err;

#if 1
	log_set_min_level(LOG_LEVEL_WARN);
	log_enable_stderr(true);
#endif

	memset(&test, 0, sizeof(test));

	aucodec_register(&test.aucodecl, &dummy_pcmu);

	/* initialization */
	agent_alloc(&a, &test, true, a_mode, "A");
	agent_alloc(&b, &test, false, b_mode, "B");
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);
	a->other = b;
	b->other = a;

	if (are_both_gathered(a)) {
		sdp_exchange(a, b);

		/* Start ICE after SDP has been exchanged */
		start_both_ice(a);
	}

#if 0
	/* start ICE connectivity check for the Trickle agents */
	if (IS_TRICKLE(a->mode))
		start_ice(a);
	if (IS_TRICKLE(b->mode))
		start_ice(b);
#endif

	/* start the main loop -- wait for network traffic */
	err = re_main_wait(5000);
	if (err) {
		re_printf("main timeout!\n");
		a = (struct agent *)mem_deref(a);
		b = (struct agent *)mem_deref(b);
	}
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a->err);
	ASSERT_EQ(0, b->err);

	/* verify results after traffic is complete */
	ASSERT_EQ(a->n_lcand_expect, a->n_lcand);
	ASSERT_EQ(b->n_lcand_expect, b->n_lcand);

#if 0
	// XXX: not sure if we really should verify this?
	if (IS_TRICKLE(a->mode)) {
		ASSERT_EQ(b->n_lcand_expect,
			  mediaflow_remote_cand_count(a->mf));
	}
	if (IS_TRICKLE(b->mode)) {
		ASSERT_EQ(a->n_lcand_expect,
			  mediaflow_remote_cand_count(b->mf));
	}
#endif

#if 1
	ASSERT_TRUE(mediaflow_remote_cand_count(a->mf) > 0);
	ASSERT_TRUE(mediaflow_remote_cand_count(b->mf) > 0);
#endif

	// TODO: temp workaround, estab-handled called 1+ times for lite
	//ASSERT_EQ(1, a->n_estab);
	//ASSERT_EQ(1, b->n_estab);
	ASSERT_GE(a->n_estab, 1);
	ASSERT_GE(b->n_estab, 1);

	ASSERT_TRUE(a->n_rtp_recv > 0);
	ASSERT_TRUE(b->n_rtp_recv > 0);
	ASSERT_TRUE(a->n_rtcp_recv > 0);
	ASSERT_TRUE(b->n_rtcp_recv > 0);

	if (HAS_TURN(a_mode)) {
		ASSERT_TRUE(mediaflow_stats_get(a->mf)->turn_alloc >= 0);
	}
	else {
		ASSERT_EQ(-1, mediaflow_stats_get(a->mf)->turn_alloc);
	}
	ASSERT_TRUE(mediaflow_stats_get(a->mf)->nat_estab >= 0);
	ASSERT_TRUE(mediaflow_stats_get(a->mf)->nat_estab < 5000);
	ASSERT_TRUE(mediaflow_stats_get(a->mf)->dtls_estab >= 0);
	ASSERT_TRUE(mediaflow_stats_get(a->mf)->dtls_estab < 5000);

	if (HAS_TURN(b_mode)) {
		ASSERT_TRUE(mediaflow_stats_get(b->mf)->turn_alloc >= 0);
	}
	else {
		ASSERT_EQ(-1, mediaflow_stats_get(b->mf)->turn_alloc);
	}
	ASSERT_TRUE(mediaflow_stats_get(b->mf)->nat_estab >= 0);
	ASSERT_TRUE(mediaflow_stats_get(b->mf)->nat_estab < 5000);
	ASSERT_TRUE(mediaflow_stats_get(b->mf)->dtls_estab >= 0);
	ASSERT_TRUE(mediaflow_stats_get(b->mf)->dtls_estab < 5000);

	mem_deref(a);
	mem_deref(b);

	aucodec_unregister(&dummy_pcmu);
}


TEST(media, b2b_trickle_stun_and_lite)
{
	test_b2b(TRICKLE_STUN, LITE);
}


TEST(media, b2b_trickle_turn_and_lite)
{
	test_b2b(TRICKLE_TURN, LITE);
}


TEST(media, b2b_trickle_stun_and_trickle_stun)
{
	test_b2b(TRICKLE_STUN, TRICKLE_STUN);
}


TEST(media, b2b_trickle_stun_and_trickle_turn)
{
	test_b2b(TRICKLE_STUN, TRICKLE_TURN);
}


TEST(media, b2b_trickle_turn_and_trickle_turn)
{
	test_b2b(TRICKLE_TURN, TRICKLE_TURN);
}


TEST(media, b2b_turnonly_and_turnonly)
{
	test_b2b(TRICKLE_TURN_ONLY, TRICKLE_TURN_ONLY);
}


TEST(media, b2b_turnonly_and_lite)
{
	test_b2b(TRICKLE_TURN_ONLY, LITE);
}
