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

#include <string.h>
#include <pthread.h>
#include <re.h>
#include <rew.h>
#include "avs_log.h"
#include "avs_aucodec.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_vidcodec.h"
#include "priv_mediaflow.h"

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif


#define MAGIC 0xed1af100
#define MAGIC_CHECK(s) \
        if (MAGIC != s->magic) {                                        \
                warning("%s: wrong magic struct=%p (magic=0x%08x)\n",   \
                        __REFUNC__, s, s->magic);                   \
                BREAKPOINT;                                             \
        }


enum {
	DTLS_MAX_TRIES = 3,
	RTP_TIMEOUT_MS = 20000,
};

enum setup {
	ACTPASS,
	ACTIVE,
	PASSIVE
};


enum sdp_state {
	SDP_IDLE = 0,
	SDP_GOFF,
	SDP_HOFF,
	SDP_DONE
};

enum {
	MQ_ERR = 0,
	MQ_RTP_START = 1,
};

struct interface {
	struct le le;

	const struct mediaflow *mf;  /* pointer to parent */
	struct ice_lcand *lcand;
	struct sa addr;
	char ifname[64];
	bool is_default;
};


struct mediaflow {

	struct mqueue *mq;

	/* common stuff */
	struct sa laddr_default;
	char tag[32];
	bool terminated;
	int err;

	/* RTP/RTCP */
	struct rtp_sock *rtp;
	struct rtcp_stats stats;
	struct tmr tmr_rtp;
	uint32_t pseq;
	bool rtcp_mux;
	bool external_rtp;
	bool enable_rtcp;
	uint32_t lssrcv[MEDIA_NUM];
	char cname[16];             /* common for audio+video */
	char msid[36];
	char *label;

	/* SDP */
	struct sdp_session *sdp;
	struct sdp_media *sdpm;
	bool sdp_offerer;
	bool got_sdp;
	bool sent_sdp;
	enum sdp_state sdp_state;

	/* ice: */
	enum mediaflow_nat nat;

	/* union: { */

	struct tmr tmr_nat;

	struct ice_lite *ice_lite;

	struct trice *trice;
	struct stun *trice_stun;
	struct udp_helper *trice_uh;
	struct ice_lcand *sel_lcand;
	struct udp_sock *us_turn;
	struct list turnconnl;

	/* } */

	struct ice_cand_attr rcand;  /* chosen remote candidate */
	uint64_t ice_tiebrk;
	char ice_ufrag[16];
	char ice_pwd[32];
	bool ice_ready;
	char *peer_software;

	/* ice - gathering */
	struct stun_ctrans *ct_gather;
	bool ice_local_eoc;
	bool ice_remote_eoc;
	bool stun_server;
	bool stun_ok;

	/* crypto: */
	enum media_crypto cryptos_local;
	enum media_crypto cryptos_remote;
	enum media_crypto crypto;          /* negotiated crypto */
	struct udp_helper *uh_srtp;
	struct srtp *srtp_tx;
	struct srtp *srtp_rx;
	struct tls *dtls;
	struct dtls_sock *dtls_sock;
	struct tls_conn *tls_conn;
	enum setup setup;
	bool crypto_ready;
	bool crypto_verified;
	uint64_t ts_dtls;
	uint64_t dtls_estab;
	unsigned dtls_n_tries;

	/* Codec handling */
	struct media_ctx *mctx;
	struct auenc_state *aes;
	struct audec_state *ads;
	pthread_mutex_t mutex_enc;  /* protect the encoder state */
	uint32_t srate;
	uint8_t audio_ch;
	bool started;
	bool hold;

	/* Video */
	struct {
		struct sdp_media *sdpm;
		struct media_ctx *mctx;
		struct videnc_state *ves;
		struct viddec_state *vds;
		
		bool has_media;
		bool started;
		char *label;
		bool has_rtp;

		mediaflow_create_preview_h *cpvh;
		mediaflow_release_preview_h *rpvh;
		mediaflow_create_view_h *cvh;
		mediaflow_release_view_h *rvh;
		void *arg;
	} video;

	/* User callbacks */
	mediaflow_localcand_h *lcandh;
	mediaflow_estab_h *estabh;
	mediaflow_audio_h *audioh;
	mediaflow_rtp_h *rtph;
	mediaflow_rtcp_h  *rtcph;
	mediaflow_close_h *closeh;
	mediaflow_rtp_state_h *rtpstateh;
	mediaflow_gather_h *gatherh;
	void *arg;

	struct {
		size_t total_lost;

		struct {
			uint64_t ts_first;
			uint64_t ts_last;
			size_t bytes;
		} tx, rx;

		size_t n_sdp_recv;
		size_t n_cand_recv;
		size_t n_srtp_dropped;
		size_t n_srtp_error;
	} stat;

	bool sent_rtp;
	bool got_rtp;

	struct list interfacel;

	/* magic number check at the end of the struct */
	uint32_t magic;
};


#undef debug
#undef info
#undef warning
#define debug(...)   mf_log(mf, LOG_LEVEL_DEBUG, __VA_ARGS__);
#define info(...)    mf_log(mf, LOG_LEVEL_INFO,  __VA_ARGS__);
#define warning(...) mf_log(mf, LOG_LEVEL_WARN,  __VA_ARGS__);


#if TARGET_OS_IPHONE
#undef OS
#define OS "ios"
#endif


const char *avs_software = AVS_PROJECT " " AVS_VERSION " (" ARCH "/" OS ")";


/* prototypes */
static int print_cand(struct re_printf *pf, const struct ice_cand_attr *cand);
static void turnc_perm_handler(void *arg);
static void add_turn_permission_ds(struct mediaflow *mf,
				   struct turnc *turnc,
				   const struct ice_cand_attr *rcand);
static void add_permission_to_remotes(struct mediaflow *mf);
static void add_permission_to_remotes_ds(struct mediaflow *mf,
					 struct turnc *turnc);
static void external_rtp_recv(struct mediaflow *mf,
			      const struct sa *src, struct mbuf *mb);


static void mf_log(const struct mediaflow *mf, enum log_level level,
		   const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);

	if (mf && mf->tag[0]) {
		int n;
		n = re_snprintf(buf, sizeof(buf), "[%s] ", mf->tag);
		str_ncpy(&buf[n], fmt, strlen(fmt)+1);
		fmt = buf;
	}

	vloglv(level, fmt, ap);
	va_end(ap);
}


/*
 * See RFC 5764 figure 3:
 *
 *                  +----------------+
 *                  | 127 < B < 192 -+--> forward to RTP
 *                  |                |
 *      packet -->  |  19 < B < 64  -+--> forward to DTLS
 *                  |                |
 *                  |       B < 2   -+--> forward to STUN
 *                  +----------------+
 *
 */
static bool is_rtp_or_rtcp(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mbuf_buf(mb)[0];

	return 127 < b && b < 192;
}


static inline bool is_rtcp_packet(const struct mbuf *mb)
{
	uint8_t pt;

	if (mbuf_get_left(mb) < 2)
		return false;

	pt = mbuf_buf(mb)[1] & 0x7f;

	return 64 <= pt && pt <= 95;
}


static bool is_dtls_packet(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mb->buf[mb->pos];

	if (b >= 20 && b <= 63) {
		return true;
	}

	return false;
}


static const char *classify_packet(const struct mbuf *mb)
{
	uint16_t *p, type;
	uint8_t b;

	b = mb->buf[mb->pos];
	p = (void *)&mb->buf[mb->pos];
	type = ntohs(*p);

	if (is_rtp_or_rtcp(mb)) {
		if (is_rtcp_packet(mb))
			return "RTCP";
		else
			return "RTP";
	}
	else if (b >= 20 && b <= 63) {
		return "DTLS";
	}
	else if ( ! ( type & 0xc000 )) {
		return "STUN";
	}
	else
		return "???";
}


enum packet {
	PACKET_UNKNOWN = 0,
	PACKET_RTP,
	PACKET_RTCP,
	PACKET_DTLS,
	PACKET_STUN,
};


static enum packet classify_packet_type(const struct mbuf *mb)
{
	uint16_t *p, type;
	uint8_t b;

	b = mb->buf[mb->pos];
	p = (void *)&mb->buf[mb->pos];
	type = ntohs(*p);

	if (is_rtp_or_rtcp(mb)) {
		if (is_rtcp_packet(mb))
			return PACKET_RTCP;
		else
			return PACKET_RTP;
	}
	else if (b >= 20 && b <= 63) {
		return PACKET_DTLS;
	}
	else if ( ! ( type & 0xc000 )) {
		return PACKET_STUN;
	}
	else
		return PACKET_UNKNOWN;
}


static const char *nat_name(enum mediaflow_nat nat)
{
	switch (nat) {

	case MEDIAFLOW_NAT_NONE:             return "None";
	case MEDIAFLOW_TRICKLEICE_DUALSTACK: return "Trickle-Dualstack";
	case MEDIAFLOW_ICELITE:              return "ICE-lite";
	default: return "?";
	}
}


static const char *crypto_name(enum media_crypto crypto)
{
	switch (crypto) {

	case CRYPTO_NONE:      return "None";
	case CRYPTO_DTLS_SRTP: return "DTLS-SRTP";
	case CRYPTO_SDESC:     return "SDESC";
	default:               return "???";
	}
}


int mediaflow_cryptos_print(struct re_printf *pf, enum media_crypto cryptos)
{
	int err = 0;

	if (!cryptos)
		return re_hprintf(pf, "%s", crypto_name(CRYPTO_NONE));

	if (cryptos & CRYPTO_DTLS_SRTP) {
		err |= re_hprintf(pf, "%s ", crypto_name(CRYPTO_DTLS_SRTP));
	}
	if (cryptos & CRYPTO_SDESC) {
		err |= re_hprintf(pf, "%s ", crypto_name(CRYPTO_SDESC));
	}

	return err;
}


static const char *setup_name(enum setup setup)
{
	switch (setup) {

	case ACTPASS: return "actpass";
	case ACTIVE:  return "active";
	case PASSIVE: return "passive";
	default: return "?";
	}
}


bool mediaflow_is_rtpstarted(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->sent_rtp && mf->got_rtp;
}


static bool mediaflow_is_video_started(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->video.has_rtp;
}


static void check_rtpstart(struct mediaflow *mf)
{
	if (!mf)
		return;

	if (!mf->rtpstateh)
		return;
	
	mf->rtpstateh(mediaflow_is_rtpstarted(mf),
		      mediaflow_is_video_started(mf),
		      mf->arg);
}


static size_t get_headroom(const struct mediaflow *mf)
{
	size_t headroom = 0;

	if (!mf)
		return 0;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		if (!mf->trice)
			return 0;

		if (!mf->sel_lcand)
			return 0;

		if (mf->sel_lcand->attr.type == ICE_CAND_TYPE_RELAY)
			return 36;
		else
			return 0;
		break;

	default:
		return 0;
	}

	return headroom;
}


static bool lite_candidate_handler(const char *name, const char *val,
				   void *arg)
{
	struct mediaflow *mf = arg;
	struct ice_cand_attr rcand;
	int err;

	err = ice_cand_attr_decode(&rcand, val);
	if (err || rcand.compid != ICE_COMPID_RTP)
		return false;

	err = icelite_cand_add(mf->ice_lite, &rcand);
	if (err) {
		warning("mediaflow: icelite_cand_add error (%m)\n", err);
	}

	return false;
}


static void ice_error(struct mediaflow *mf, int err)
{
	warning("mediaflow: error in ICE-transport (%m)\n", err);

	mf->ice_ready = false;
	mf->err = err;

	list_flush(&mf->turnconnl);

	mf->trice_uh = mem_deref(mf->trice_uh);  /* note: destroy first */
	mf->sel_lcand = mem_deref(mf->sel_lcand);
	mf->trice = mem_deref(mf->trice);

	mf->ice_lite = mem_deref(mf->ice_lite);

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


static void crypto_error(struct mediaflow *mf, int err)
{
	warning("mediaflow: error in DTLS (%m)\n", err);

	mf->crypto_ready = false;
	mf->err = err;
	mf->tls_conn = mem_deref(mf->tls_conn);

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


bool mediaflow_is_ready(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	if (mf->nat != MEDIAFLOW_NAT_NONE && !mf->ice_ready)
		return false;

	if (mf->crypto != CRYPTO_NONE)
		return mf->crypto_ready;

	return true;
}


static inline int lostcalc(struct mediaflow *mf, uint16_t seq)
{
	const uint16_t delta = seq - mf->pseq;
	int lostc;

	if (mf->pseq == (uint32_t)-1)
		lostc = 0;
	else if (delta == 0)
		return -1;
	else if (delta < 3000)
		lostc = delta - 1;
	else if (delta < 0xff9c)
		lostc = 0;
	else
		return -2;

	mf->pseq = seq;

	return lostc;
}


static void rtp_recv_handler(const struct sa *src,
			     const struct rtp_header *hdr,
			     struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	const struct sdp_format *fmt;
	const struct aucodec *ac;
	int lost;
	int err;

	lost = lostcalc(mf, hdr->seq);
	if (lost > 0) {
		info("mediaflow[%u]: %d rtp packets lost\n",
		     sa_port(rtp_local(mf->rtp)), lost);
		mf->stat.total_lost += lost;
	}

	if (mf->rtph)
		mf->rtph(src, hdr, mb, mf->arg);

	fmt = sdp_media_lformat(mf->sdpm, hdr->pt);
	if (!fmt) {
		warning("mediaflow: payload type: %d not found"
			" in sdp (%zu bytes)\n",
			hdr->pt, mbuf_get_left(mb));
		return;
	}

	ac = fmt->data;
	if (!ac) {
		warning("mediaflow: decoder: %s/%d/%d not found\n",
			fmt->name, fmt->srate, fmt->ch);
		return;
	}

	if (ac->dech) {
		err = ac->dech(mf->ads, hdr, mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("mediaflow: decode %zu bytes failed (%m)\n",
				mbuf_get_left(mb), err);
		}
	}
}


static void rtcp_recv_handler(const struct sa *src, struct rtcp_msg *msg,
			      void *arg)
{
	struct mediaflow *mf = arg;
	(void)src;

	switch (msg->hdr.pt) {

	case RTCP_SR:
		rtcp_stats(mf->rtp, msg->r.sr.ssrc, &mf->stats);
		break;
	}

	if (mf->rtcph)
		mf->rtcph(mf->rtp, msg, mf->arg);
}


static void update_tx_stats(struct mediaflow *mf, size_t len)
{
	uint64_t now = tmr_jiffies();

	if (!mf->stat.tx.ts_first)
		mf->stat.tx.ts_first = now;
	mf->stat.tx.ts_last = now;
	mf->stat.tx.bytes += len;
}


static void update_rx_stats(struct mediaflow *mf, size_t len)
{
	uint64_t now = tmr_jiffies();

	if (!mf->stat.rx.ts_first)
		mf->stat.rx.ts_first = now;
	mf->stat.rx.ts_last = now;
	mf->stat.rx.bytes += len;
}


static int auenc_packet_handler(uint8_t pt, uint32_t ts,
				const uint8_t *pld, size_t pld_len,
				void *arg)
{
	struct mediaflow *mf = arg;
	struct mbuf *mb = mbuf_alloc(256);
	int err;

	mb->pos = RTP_HEADER_SIZE;
	mbuf_write_mem(mb, pld, pld_len);
	mb->pos = RTP_HEADER_SIZE;

	update_tx_stats(mf, pld_len);

	/* send RTP packet */
	err = rtp_send(mf->rtp, &mf->rcand.addr, false, pt, ts, mb);
	if (err) {
		warning("rtp_send [%J] failed (%m)\n", &mf->rcand.addr, err);
		goto out;
	}

	/* XXX: workaround */
	mediaflow_rtp_start_send(mf);

 out:
	mem_deref(mb);

	return 0;
}


static void auenc_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	error("auenc_error_handler: %s\n", msg);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}
}


static int audec_recv_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct mediaflow *mf = arg;

	if (mf->audioh) {
		mf->audioh(sampv, sampc, mf->arg);
	}

	return 0;
}


static void audec_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	error("audec_error_handler: %s\n", msg);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}
}


static int voenc_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;

	if (!mf)
		return -1;

	if (!mf->sent_rtp) { // Move inside mediaflow_send_raw ??
		info("mediaflow: first RTP packet sent\n");
		mf->sent_rtp = true;
		mqueue_push(mf->mq, MQ_RTP_START, NULL);
	}

	return mediaflow_send_raw_rtp(mf, pkt, len);
}


static int voenc_rtcp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	// Can use the same as video
	struct mediaflow *mf = arg;

	return mediaflow_send_raw_rtcp(mf, pkt, len);
}


// Move to mediamanager

static int start_codecs(struct mediaflow *mf)
{
	const struct aucodec *ac;
	const struct sdp_format *fmt;
	int err = 0;

	pthread_mutex_lock(&mf->mutex_enc);

	fmt = sdp_media_rformat(mf->sdpm, NULL);
	if (!fmt) {
		warning("mediaflow: no common codec\n");
		err = ENOENT;
		goto out;
	}

	ac = fmt->data;
	if (!ac) {
		warning("mediaflow: no aucodec in sdp data\n");
		err = EINVAL;
		goto out;
	}

	debug("mediaflow: starting audio codecs (%s/%u/%d)\n",
	      fmt->name, fmt->srate, fmt->ch);

	if (ac->enc_alloc && !mf->aes) {
		err = ac->enc_alloc(&mf->aes, &mf->mctx, ac, NULL, fmt->pt,
				    mf->srate ? mf->srate : ac->srate,
				    mf->audio_ch ? mf->audio_ch : ac->ch,
				    voenc_rtp_handler,
				    voenc_rtcp_handler,
				    auenc_packet_handler,
				    auenc_error_handler,
				    mf);
		if (err) {
			warning("mediaflow: encoder failed (%m)\n", err);
			goto out;
		}

		if (mf->started && ac->enc_start) {
			ac->enc_start(mf->aes);
		}
	}

	if (ac->dec_alloc && !mf->ads){
		err = ac->dec_alloc(&mf->ads, &mf->mctx, ac, NULL, fmt->pt,
				    mf->srate ? mf->srate : ac->srate,
				    mf->audio_ch ? mf->audio_ch : ac->ch,
				    audec_recv_handler,
				    audec_error_handler,
				    mf);
		if (err) {
			warning("mediaflow: decoder failed (%m)\n", err);
			goto out;
		}

		if (mf->started && ac->dec_start) {
			ac->dec_start(mf->ads);
		}
	}

	rtcp_set_srate(mf->rtp, ac->srate, ac->srate);

 out:
	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}

static int videnc_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;

	return mediaflow_send_raw_rtp(mf, pkt, len);
}


static int videnc_rtcp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;
#if 0
	debug("mediaflow: videnc: RTCP send (%zu bytes)\n", len);
#endif
	return mediaflow_send_raw_rtcp(mf, pkt, len);
}


static void videnc_create_preview_handler(void *arg)
{
	struct mediaflow *mf = arg;

	if (mf->video.cpvh) {
		mf->video.cpvh(mf->arg);
	}
}


static void videnc_release_preview_handler(void *view, void *arg)
{
	struct mediaflow *mf = arg;

	if (mf->video.rpvh) {
		mf->video.rpvh(view, mf->arg);
	}
}


static void viddec_create_view_handler(void *arg)
{
	struct mediaflow *mf = arg;

	if (mf->video.cvh) {
		mf->video.cvh(mf->arg);
	}
}


static void viddec_release_view_handler(void *view, void *arg)
{
	struct mediaflow *mf = arg;

	if (mf->video.rvh) {
		mf->video.rvh(view, mf->arg);
	}
}


static void vidcodec_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	warning("mediaflow: video-codec error '%s' (%m)\n", msg, err);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}

	// TODO: should we also close video-states and ICE+DTLS ?
}


static int start_video_codecs(struct mediaflow *mf)
{
	const struct vidcodec *vc;
	const struct sdp_format *fmt;
	int err = 0;

	fmt = sdp_media_rformat(mf->video.sdpm, NULL);
	if (!fmt) {
		warning("mediaflow: no common video-codec\n");
		err = ENOENT;
		goto out;
	}

	vc = fmt->data;
	if (!vc) {
		warning("mediaflow: no vidcodec in sdp data\n");
		err = EINVAL;
		goto out;
	}

	debug("mediaflow: starting video codecs (%s/%u/%d)"
	      " [params=%s, rparams=%s]\n",
	      fmt->name, fmt->srate, fmt->ch, fmt->params, fmt->rparams);

	if (vc->enc_alloch && !mf->video.ves) {
		err = vc->enc_alloch(&mf->video.ves, &mf->video.mctx, vc,
				     fmt->rparams, fmt->pt,
				     mf->video.sdpm,
				     videnc_rtp_handler,
				     videnc_rtcp_handler,
				     videnc_create_preview_handler,
				     videnc_release_preview_handler,
				     vidcodec_error_handler,
				     mf);
		if (err) {
			warning("mediaflow: video encoder failed (%m)\n", err);
			goto out;
		}

		if (mf->started && vc->enc_starth) {
			err = vc->enc_starth(mf->video.ves);
			if (err) {
				warning("mediaflow: could not start"
					" video encoder (%m)\n", err);
				goto out;
			}
		}
	}

	if (vc->dec_alloch && !mf->video.vds){
		err = vc->dec_alloch(&mf->video.vds, &mf->video.mctx, vc,
				     fmt->params, fmt->pt,
				     mf->video.sdpm,
				     viddec_create_view_handler,
				     viddec_release_view_handler,
				     vidcodec_error_handler,
				     mf);
		if (err) {
			warning("mediaflow: video decoder failed (%m)\n", err);
			goto out;
		}

		if (mf->started && vc->dec_starth) {
			err = vc->dec_starth(mf->video.vds);
			if (err) {
				warning("mediaflow: could not start"
					" video decoder (%m)\n", err);
				return err;
			}
		}
	}

 out:
	return err;
}


static void timeout_rtp(void *arg)
{
	struct mediaflow *mf = arg;

	tmr_start(&mf->tmr_rtp, 5000, timeout_rtp, mf);

	if (mediaflow_is_rtpstarted(mf)) {

		int diff = tmr_jiffies() - mf->stat.rx.ts_last;

		if (diff > RTP_TIMEOUT_MS) {

			warning("mediaflow: no RTP packets recvd for"
				" %d ms -- stop\n",
				diff);

			mf->terminated = true;
			mf->ice_ready = false;

			if (mf->closeh)
				mf->closeh(ETIMEDOUT, mf->arg);
		}
	}
}


/* this function is only called once */
static void mediaflow_established_handler(struct mediaflow *mf)
{
	if (mf->nat != MEDIAFLOW_NAT_NONE && !mf->ice_ready) {
		return;
	}
	if (mf->crypto != CRYPTO_NONE && !mf->crypto_ready) {
		return;
	}

	info("mediaflow: ICE+DTLS established (remote = %s.%J)\n",
	     ice_cand_type2name(mf->rcand.type), &mf->rcand.addr);

	if (!tmr_isrunning(&mf->tmr_rtp))
		tmr_start(&mf->tmr_rtp, 1000, timeout_rtp, mf);

	if (mf->estabh) {
		const struct sdp_format *fmt;

		fmt = sdp_media_rformat(mf->sdpm, NULL);

		mf->estabh(crypto_name(mf->crypto),
			   fmt ? fmt->name : "?",
			   ice_cand_type2name(mf->rcand.type),
			   &mf->rcand.addr, mf->arg);
	}

	if (mf->enable_rtcp)
		rtcp_start(mf->rtp, mf->cname, &mf->rcand.addr);
}


static bool udp_helper_send_handler_srtp(int *err, struct sa *dst,
					 struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	(void)dst;

	if (is_rtp_or_rtcp(mb) && mf->srtp_tx) {

		if (is_rtcp_packet(mb)) {

			/* drop short RTCP packets */
			if (mbuf_get_left(mb) <= 8)
				return true;

			*err = srtcp_encrypt(mf->srtp_tx, mb);
			if (*err) {
				warning("srtcp_encrypt() failed (%m)\n",
					*err);
			}
		}
		else {
			*err = srtp_encrypt(mf->srtp_tx, mb);
			if (*err) {
				warning("srtp_encrypt() [%zu bytes]"
					" failed (%m)\n",
					mbuf_get_left(mb), *err);
			}
		}
	}

	return false;
}


/* For Dual-stack only */
static bool udp_helper_send_handler_trice(int *err, struct sa *dst,
					 struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	int lerr;
	(void)dst;

	if (mf->ice_ready && mf->sel_lcand) {

		void *sock;

		sock = trice_lcand_sock(mf->trice, mf->sel_lcand);

#if 0
		re_printf("TX: redir %zu bytes from %J to %s.%J\n",
			  mbuf_get_left(mb),
			  dst,
			  ice_cand_type2name(mf->rcand.type), &mf->rcand.addr);
#endif

		if (!sock) {
			warning("send: selected lcand %p has no sock [%H]\n",
				mf->sel_lcand,
				trice_cand_print, mf->sel_lcand);
		}

		lerr = udp_send(sock, &mf->rcand.addr, mb);
		if (lerr) {
			warning("mediaflow: send helper error (%m)\n",
				lerr);
		}
	}
#if 1
	else if (is_dtls_packet(mb) &&
		 sa_af(dst) == sa_af(rtp_local(mf->rtp))) {

		info("mediaflow: DTLS pass-through native socket"
		      " (%zu bytes to %J)\n", mbuf_get_left(mb), dst);
		return false;  /* packet contiue */
	}
#endif
	else {
		warning("mediaflow: helper: cannot send"
			" %zu bytes to %J, ICE not ready! (packet=%s)\n",
			mbuf_get_left(mb), dst,
			classify_packet(mb));
		*err = ENOTCONN;
	}

	return true;
}


/* RFC 4572 */
static int sdp_fingerprint_decode(const char *attr, struct pl *hash,
			   uint8_t *md, size_t *sz)
{
	struct pl f;
	const char *p;
	int err;

	if (!attr || !hash)
		return EINVAL;

	err = re_regex(attr, str_len(attr), "[^ ]+ [0-9A-F:]+", hash, &f);
	if (err)
		return err;

	if (md && sz) {
		if (*sz < (f.l+1)/3)
			return EOVERFLOW;

		for (p = f.p; p < (f.p+f.l); p += 3) {
			*md++ = ch_hex(p[0]) << 4 | ch_hex(p[1]);
		}

		*sz = (f.l+1)/3;
	}

	return 0;
}


static bool verify_fingerprint(struct mediaflow *mf,
			       const struct sdp_session *sess,
			       const struct sdp_media *media,
			       struct tls_conn *tc)
{
	struct pl hash;
	uint8_t md_sdp[32], md_dtls[32];
	size_t sz_sdp = sizeof(md_sdp);
	size_t sz_dtls;
	enum tls_fingerprint type;
	const char *attr;
	int err;

	attr = sdp_media_session_rattr(media, sess, "fingerprint");
	if (sdp_fingerprint_decode(attr, &hash, md_sdp, &sz_sdp))
		return false;

	if (0 == pl_strcasecmp(&hash, "sha-1")) {
		type = TLS_FINGERPRINT_SHA1;
		sz_dtls = 20;
	}
	else if (0 == pl_strcasecmp(&hash, "sha-256")) {
		type = TLS_FINGERPRINT_SHA256;
		sz_dtls = 32;
	}
	else {
		warning("mediaflow: dtls_srtp: unknown fingerprint"
			" '%r'\n", &hash);
		return false;
	}

	err = tls_peer_fingerprint(tc, type, md_dtls, sizeof(md_dtls));
	if (err) {
		warning("mediaflow: dtls_srtp: could not get"
			" DTLS fingerprint (%m)\n", err);
		return false;
	}

	if (sz_sdp != sz_dtls || 0 != memcmp(md_sdp, md_dtls, sz_sdp)) {
		warning("mediaflow: dtls_srtp: %r fingerprint mismatch\n",
			&hash);
		info("  SDP:  %w\n", md_sdp, sz_sdp);
		info("  DTLS: %w\n", md_dtls, sz_dtls);
		return false;
	}

	info("mediaflow: dtls_srtp: verified %r fingerprint OK\n", &hash);

	return true;
}


static void dtls_estab_handler(void *arg)
{
	struct mediaflow *mf = arg;
	enum srtp_suite suite;
	uint8_t cli_key[30], srv_key[30];
	int err;

	mf->dtls_estab = tmr_jiffies() - mf->ts_dtls;

	info("mediaflow: DTLS established (%llu ms)\n", mf->dtls_estab);

	info("           %s %s\n",
	     dtls_conn_proto_version_str(mf->tls_conn),
	     dtls_conn_cipher_name(mf->tls_conn));

	if (mf->got_sdp) {
		if (!verify_fingerprint(mf, mf->sdp, mf->sdpm, mf->tls_conn)) {
			warning("mediaflow: dtls_srtp: could not verify"
				" remote fingerprint\n");
			err = EAUTH;
			goto error;
		}
		mf->crypto_verified = true;
	}

	err = tls_srtp_keyinfo(mf->tls_conn, &suite,
			       cli_key, sizeof(cli_key),
			       srv_key, sizeof(srv_key));
	if (err) {
		warning("mediaflow: could not get SRTP keyinfo (%m)\n", err);
		goto error;
	}

	info("mediaflow: DTLS established (%s)\n", srtp_suite_name(suite));

	mf->srtp_tx = mem_deref(mf->srtp_tx);
	err = srtp_alloc(&mf->srtp_tx, suite,
			 mf->setup == ACTIVE ? cli_key : srv_key,
			 sizeof(cli_key), 0);
	if (err) {
		warning("mediaflow: failed to allocate SRTP for TX (%m)\n",
			err);
		goto error;
	}

	err = srtp_alloc(&mf->srtp_rx, suite,
			 mf->setup == ACTIVE ? srv_key : cli_key,
			 sizeof(srv_key), 0);
	if (err) {
		warning("mediaflow: failed to allocate SRTP for RX (%m)\n",
			err);
		goto error;
	}

	mf->crypto_ready = true;

	mediaflow_established_handler(mf);

	return;

 error:
	warning("mediaflow: DTLS-SRTP error (%m)\n", err);

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


static void dtls_close_handler(int err, void *arg)
{
	struct mediaflow *mf = arg;

	info("mediaflow: dtls-connection closed (%m)\n", err);

	mf->tls_conn = mem_deref(mf->tls_conn);
	mf->err = err;

	if (!mf->crypto_ready) {

		++mf->dtls_n_tries;

		if (mf->dtls_n_tries < DTLS_MAX_TRIES) {

			if (mf->setup == ACTIVE) {

				err = dtls_connect(&mf->tls_conn, mf->dtls,
						   mf->dtls_sock,
						   &mf->rcand.addr,
						   dtls_estab_handler, NULL,
						   dtls_close_handler, mf);
				if (err) {
					warning("mediaflow: dtls_connect()"
						" failed (%m)\n", err);
					return;
				}
			}

			return;
		}

		if (mf->closeh)
			mf->closeh(err, mf->arg);
	}
}


/*
 * called ONCE when we receive DTLS Client Hello from the peer
 *
 * this function is only called when the ICE-layer is established
 */
static void dtls_conn_handler(const struct sa *peer, void *arg)
{
	struct mediaflow *mf = arg;
	bool okay;
	int err;

	info("mediaflow: incoming DTLS connect from %J\n", peer);

	if (mf->ice_ready || sa_af(peer) == sa_af(rtp_local(mf->rtp))) {
		okay = 1;
	}
	else {
		okay = 0;
	}

	if (!okay) {
		info("mediaflow: ICE is not ready, cannot accept DTLS\n");
		return;
	}

	mf->ts_dtls = tmr_jiffies();

	err = dtls_accept(&mf->tls_conn, mf->dtls, mf->dtls_sock,
			  dtls_estab_handler, NULL, dtls_close_handler, mf);
	if (err) {
		warning("mediaflow: dtls_accept failed (%m)\n", err);
		info("%H\n", dtls_sock_debug, mf->dtls_sock);
		goto error;
	}

	info("mediaflow: dtls accepted\n");

#if 0
	/* must be done after dtls_accept() */
	if (sa_isset(&mf->dtls_peer, SA_ALL) &&
	    !sa_cmp(&mf->dtls_peer, peer, SA_ALL)) {

		info("mediaflow: update DTLS peer: %J -> %J\n",
		     &mf->dtls_peer, peer );

		mf->dtls_peer = *peer;
		dtls_update_peer(mf->dtls_sock, peer);
	}
#endif

	return;

 error:
	crypto_error(mf, err);
}


/* this function is only called once */
static void ice_established_handler(struct mediaflow *mf,
				    const struct sa *peer)
{
	int err;

	info("mediaflow: ICE-transport established [got_sdp=%d]"
	     " (peer = %s.%J)\n",
	     mf->got_sdp, ice_cand_type2name(mf->rcand.type), peer);

	if (mf->crypto_ready) {
		info("mediaflow: ice-estab: crypto already ready\n");
		goto out;
	}

	switch (mf->crypto) {

	case CRYPTO_NONE:
		/* Do nothing */
		break;

	case CRYPTO_DTLS_SRTP:

		if (mf->setup == ACTIVE) {

			if (mf->tls_conn) {
				info("mediaflow: dtls_connect,"
				     " already connecting ..\n");
				goto out;
			}

			info("mediaflow: dtls connect to peer %J\n", peer);

			mf->ts_dtls = tmr_jiffies();

			/* NOTE: must be done before dtls_connect() */
			if (mf->trice) {
				size_t headroom;

				headroom = get_headroom(mf);

				debug("mediaflow: connect: "
				      "DTLS headroom %zu\n",
				      headroom);

				dtls_set_headroom(mf->dtls_sock, headroom);
			}

			err = dtls_connect(&mf->tls_conn, mf->dtls,
					   mf->dtls_sock, peer,
					   dtls_estab_handler, NULL,
					   dtls_close_handler, mf);
			if (err) {
				warning("mediaflow: dtls_connect()"
					" failed (%m)\n", err);
				return;
			}
		}
		break;

	case CRYPTO_SDESC:
		mf->crypto_ready = true;
		break;

	default:
		warning("mediaflow: established: unknown crypto '%s' (%d)\n",
			crypto_name(mf->crypto), mf->crypto);
		break;
	}

 out:
	mediaflow_established_handler(mf);
}


static int handle_sdes_srtp_tx(struct mediaflow *mf)
{
       uint8_t key[30];
       char buf[256];
       size_t buf_len = sizeof(buf);
       int err;

       rand_bytes(key, sizeof(key));

       err = srtp_alloc(&mf->srtp_tx, SRTP_AES_CM_128_HMAC_SHA1_80,
                        key, sizeof(key), 0);
       if (err) {
               warning("mediaflow: failed to allocate SRTP for TX (%m)\n",
                       err);
               return err;
       }

       err = base64_encode(key, sizeof(key), buf, &buf_len);
       if (err)
               return err;

       err = sdp_media_set_lattr(mf->sdpm, true,
                                 "crypto",
                                 "1 AES_CM_128_HMAC_SHA1_80 inline:%b",
                                 buf, buf_len);
       if (err)
               return err;

       return 0;
}


static bool attrib_handler(const char *name, const char *val, void *arg)
{
       struct pl idx, suite, keyprm, sessprm, key, lifemki, keyprm2;
       (void)name;
       (void)arg;

       if (!val)
               return false;

       if (re_regex(val, strlen(val),
                    "[0-9]+[ \t]+[0-9a-z_]+[ \t]+inline:[^ \t]+[^]*",
                    &idx, NULL, &suite, NULL, &keyprm, &sessprm))
               return false;

       if (re_regex(keyprm.p, keyprm.l, "[^|;]+[^;]*[;]*[^]*",
                    &key, &lifemki, NULL, &keyprm2))
               return false;

       /* MKI or multi-key not supported */
       if (pl_strchr(&lifemki, ':') || keyprm2.l > 0)
               return false;

       /* found */
       if (0 == pl_strcasecmp(&suite, "AES_CM_128_HMAC_SHA1_80"))
               return true;

       return false;
}


static int handle_sdes_srtp_rx(struct mediaflow *mf)
{
       const char *crypto;
       struct pl b;
       uint8_t key[30];
       size_t key_len = sizeof(key);
       int err;

       crypto = sdp_media_rattr_apply(mf->sdpm, "crypto", attrib_handler, 0);
       if (!crypto) {
               warning("mediaflow: crypto parameter not found\n");
               return ENOENT;
       }

       err = re_regex(crypto, str_len(crypto), "inline:[^]+", &b);
       if (err) {
               warning("mediaflow: could not get crypto key (%s)\n", crypto);
               return err;
       }

       err = base64_decode(b.p, b.l, key, &key_len);
       if (err)
               return err;

       err = srtp_alloc(&mf->srtp_rx, SRTP_AES_CM_128_HMAC_SHA1_80,
                        key, key_len, 0);
       if (err) {
               warning("mediaflow: failed to allocate SRTP for RX (%m)\n",
                       err);
               return err;
       }

       return err;
}


static bool udp_helper_recv_handler_srtp(struct sa *src, struct mbuf *mb,
					 void *arg)
{
	struct mediaflow *mf = arg;
	size_t len = mbuf_get_left(mb);
	int err;

	if (is_rtp_or_rtcp(mb)) {

		/* the SRTP is not ready yet .. */
		if (!mf->srtp_rx) {
			mf->stat.n_srtp_dropped++;
			goto next;
		}

		if (is_rtcp_packet(mb)) {

			err = srtcp_decrypt(mf->srtp_rx, mb);
			if (err) {
				mf->stat.n_srtp_error++;
				warning("mediaflow: srtcp_decrypt failed"
					" [%zu bytes] (%m)\n", len, err);
				return true;
			}
		}
		else {
			err = srtp_decrypt(mf->srtp_rx, mb);
			if (err) {
				mf->stat.n_srtp_error++;
				if (err != EALREADY) {
					warning("mediaflow: srtp_decrypt"
						" failed"
						" [%zu bytes] (%m)\n",
						len, err);
				}
				return true;
			}
		}

	}

 next:
	if (is_rtp_or_rtcp(mb)) {

		/* If external RTP is enabled, forward RTP/RTCP packets
		 * to the relevant au/vid-codec.
		 *
		 * otherwise just pass it up to internal RTP-stack
		 */
		if (mf->external_rtp) {
			external_rtp_recv(mf, src, mb);
			return true; /* handled */
		}
		else {
			update_rx_stats(mf, mbuf_get_left(mb));
			return false;  /* continue processing */
		}
	}

	return false;
}


/*
 * UDP helper to intercept incoming RTP/RTCP packets:
 *
 * -- send to decoder if supported by it
 */
static void external_rtp_recv(struct mediaflow *mf,
			      const struct sa *src, struct mbuf *mb)
{
	const struct aucodec *ac;
	const struct vidcodec *vc;
	struct rtp_header hdr;
	const struct sdp_format *fmt;
	size_t start = mb->pos;
	int err;

	if (!mf->started) {
		return;
	}
	
	ac = audec_get(mf->ads);
	vc = viddec_get(mf->video.vds);

	if (!is_rtcp_packet(mb)) {
		update_rx_stats(mf, mbuf_get_left(mb));
	}
	else {
		/* RTCP is sent to both audio+video */
#if 0
		debug("mediaflow(%p): recv RTCP packet (%zu bytes)\n",
		      mf, mbuf_get_left(mb));
#endif
		if (ac && ac->dec_rtcph) {
			mb->pos = start;
			ac->dec_rtcph(mf->ads,
				      mbuf_buf(mb), mbuf_get_left(mb));
		}
		if (vc && vc->dec_rtcph) {
			mb->pos = start;
			vc->dec_rtcph(mf->video.vds,
				      mbuf_buf(mb), mbuf_get_left(mb));
		}
		goto out;
	}

	if (!mf->got_rtp) {
		info("mediaflow: first RTP packet received (%zu bytes)\n",
		     mbuf_get_left(mb));
		mf->got_rtp = true;
		check_rtpstart(mf);
	}

	err = rtp_hdr_decode(&hdr, mb);
	mb->pos = start;
	if (err) {
		warning("mediaflow: rtp header decode (%m)\n", err);
		goto out;
	}

#if 0
	debug("mediaflow(%p): RTP packet received (%zu bytes)\n",
	      mf, mbuf_get_left(mb));
#endif

	fmt = sdp_media_lformat(mf->sdpm, hdr.pt);
	if (fmt) {

		/* now, pass on the raw RTP/RTCP packet to the decoder */

		if (ac && ac->dec_rtph) {
			ac->dec_rtph(mf->ads,
				     mbuf_buf(mb), mbuf_get_left(mb));
		}

		goto out;
	}

	fmt = sdp_media_lformat(mf->video.sdpm, hdr.pt);
	if (fmt) {
		if (!mf->video.has_rtp) {			
			mf->video.has_rtp = true;
			check_rtpstart(mf);
		}
		if (vc && vc->dec_rtph) {
			vc->dec_rtph(mf->video.vds,
				     mbuf_buf(mb), mbuf_get_left(mb));
		}

		goto out;
	}

	info("mediaflow: recv: no SDP format found"
	     " for payload type %d\n", hdr.pt);

 out:
	return;  /* stop packet here */
}


static int print_cand(struct re_printf *pf, const struct ice_cand_attr *cand)
{
	if (!cand)
		return 0;

	return re_hprintf(pf, "%s.%J",
			  ice_cand_type2name(cand->type), &cand->addr);
}


static int print_errno(struct re_printf *pf, int err)
{
	if (err == -1)
		return re_hprintf(pf, "Progress..");
	else if (err)
		return re_hprintf(pf, "%m", err);
	else
		return re_hprintf(pf, "Success");
}


#if 1
static int print_candidates(struct re_printf *pf, const struct mediaflow *mf)
{
	int err = 0;

	if (!mf)
		return 0;

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {
		err = trice_debug(pf, mf->trice);
	}
	else if (mf->nat == MEDIAFLOW_ICELITE) {
		err = icelite_debug(pf, mf->ice_lite);
	}

	return err;
}
#endif


int mediaflow_summary(struct re_printf *pf, const struct mediaflow *mf)
{
	const struct rtcp_stats *s;
	struct le *le;
	double dur_tx;
	double dur_rx;
	int err = 0;

	if (!mf)
		return 0;

	s = &mf->stats;

	dur_tx = (double)(mf->stat.tx.ts_last - mf->stat.tx.ts_first) / 1000.0;
	dur_rx = (double)(mf->stat.rx.ts_last - mf->stat.rx.ts_first) / 1000.0;

	err |= re_hprintf(pf,
			  "------------- mediaflow summary -------------\n");
	err |= re_hprintf(pf, "tag:  %s\n", mf->tag);
	err |= re_hprintf(pf, "\n");
	err |= re_hprintf(pf, "sdp: state=%d, got_sdp=%d, sent_sdp=%d\n",
			  mf->sdp_state, mf->got_sdp, mf->sent_sdp);

	err |= re_hprintf(pf, "nat: %s (ready=%d)\n",
			  nat_name(mf->nat), mf->ice_ready);
	err |= re_hprintf(pf, "remote candidates:\n");

#if 1
	err |= print_candidates(pf, mf);
#endif

	if (mf->sel_lcand) {
		err |= re_hprintf(pf, "selected local candidate:   %H\n",
				  trice_cand_print, mf->sel_lcand);
	}
	err |= re_hprintf(pf, "remote IP-address:   %H\n",
			  print_cand, &mf->rcand);
	err |= re_hprintf(pf, "peer_software:       %s\n", mf->peer_software);
	err |= re_hprintf(pf, "eoc:                 local=%d, remote=%d\n",
			  mf->ice_local_eoc, mf->ice_remote_eoc);
	err |= re_hprintf(pf, "\n");

	/* Crypto summary */
	err |= re_hprintf(pf,
			  "crypto: local  = %H\n"
			  "        remote = %H\n"
			  "        common = %s\n",
			  mediaflow_cryptos_print, mf->cryptos_local,
			  mediaflow_cryptos_print, mf->cryptos_remote,
			  crypto_name(mf->crypto));
	err |= re_hprintf(pf,
			  "        ready=%d\n", mf->crypto_ready);

	if (mf->crypto == CRYPTO_DTLS_SRTP) {
		err |= re_hprintf(pf,
				  "        setup=%s, verified=%d\n",
				  setup_name(mf->setup),
				  mf->crypto_verified);
		err |= re_hprintf(pf, "        setup_time=%llu ms\n",
				  mf->dtls_estab);
		err |= re_hprintf(pf, "%H\n", dtls_sock_debug, mf->dtls_sock);
	}
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, "RTP packets lost:    %zu\n",
			  mf->stat.total_lost);
	err |= re_hprintf(pf, "bytes sent:  %zu (%.1f bit/s)"
			  " for %.2f sec\n",
		  mf->stat.tx.bytes,
		  dur_tx ? 8.0 * (double)mf->stat.tx.bytes / dur_tx : 0,
		  dur_tx);
	err |= re_hprintf(pf, "bytes recv:  %zu (%.1f bit/s)"
			  " for %.2f sec\n",
		  mf->stat.rx.bytes,
		  dur_rx ? 8.0 * (double)mf->stat.rx.bytes / dur_rx : 0,
		  dur_rx);

	if (s->tx.sent) {
		err |= re_hprintf(pf, "RTCP Tx: sent=%-6u lost=%-3d"
				  " jitter=%.1fms\n",
				  s->tx.sent, s->tx.lost, 1.0*s->tx.jit/1000);
	}
	if (s->rx.sent) {
		err |= re_hprintf(pf, "RTCP Rx: sent=%-6u lost=%-3d"
				  " jitter=%.1fms\n",
				  s->rx.sent, s->rx.lost, 1.0*s->rx.jit/1000);
	}

	err |= re_hprintf(pf, "\n");
	err |= re_hprintf(pf, "SDP recvd:       %zu\n", mf->stat.n_sdp_recv);
	err |= re_hprintf(pf, "ICE cand recvd:  %zu\n", mf->stat.n_cand_recv);
	err |= re_hprintf(pf, "SRTP dropped:    %zu\n",
			  mf->stat.n_srtp_dropped);
	err |= re_hprintf(pf, "SRTP errors:     %zu\n",
			  mf->stat.n_srtp_error);

	err |= re_hprintf(pf, "\nvideo_media: %d\n", mf->video.has_media);

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {

		err |= re_hprintf(pf, "TURN Clients: (%u)\n",
				  list_count(&mf->turnconnl));

		for (le = mf->turnconnl.head; le; le = le->next) {
			struct turn_conn *tc = le->data;

			err |= turnconn_debug(pf, tc);
		}
	}

	err |= re_hprintf(pf, "Interfaces: (%u)\n",
			  list_count(&mf->interfacel));
	for (le = mf->interfacel.head; le; le = le->next) {
		struct interface *ifc = le->data;

		err |= re_hprintf(pf, "...%s..%s|%j\n",
				  ifc->is_default ? "*" : ".",
				  ifc->ifname, &ifc->addr);
	}

	err |= re_hprintf(pf,
			  "-----------------------------------------------\n");
	err |= re_hprintf(pf, "\n");

	return err;
}


/* NOTE: all udp-helpers must be free'd before RTP-socket */
static void destructor(void *arg)
{
	struct mediaflow *mf = arg;

	mf->terminated = true;

	mf->estabh = NULL;
	mf->closeh = NULL;

	if (mf->started)
		mediaflow_stop_media(mf);

	info("mediaflow: mediaflow %p destroyed (%H) got_sdp=%d\n",
	     mf, print_errno, mf->err, mf->got_sdp);

	/* print a nice summary */
	if (mf->got_sdp) {
		info("%H\n", mediaflow_summary, mf);
	}

	tmr_cancel(&mf->tmr_rtp);
	tmr_cancel(&mf->tmr_nat);

	/* XXX: voe is calling to mediaflow_xxx here */
	/* deref the encoders/decodrs first, as they may be multithreaded,
	 * and callback in here...
	 * Remove decoder first as webrtc might still send RTCP packets
	 */
	mf->ads = mem_deref(mf->ads);
	mf->aes = mem_deref(mf->aes);

	mf->video.ves = mem_deref(mf->video.ves);
	mf->video.vds = mem_deref(mf->video.vds);

	list_flush(&mf->interfacel);
	mf->ice_lite = mem_deref(mf->ice_lite);

	mf->trice_uh = mem_deref(mf->trice_uh);  /* note: destroy first */
	mf->sel_lcand = mem_deref(mf->sel_lcand);
	mf->trice = mem_deref(mf->trice);
	mf->trice_stun = mem_deref(mf->trice_stun);
	mem_deref(mf->us_turn);
	list_flush(&mf->turnconnl);

	mem_deref(mf->tls_conn);
	mem_deref(mf->dtls_sock);

	mem_deref(mf->uh_srtp);

	mem_deref(mf->rtp); /* must be free'd after ICE and DTLS */
	mem_deref(mf->sdp);

	mem_deref(mf->srtp_tx);
	mem_deref(mf->srtp_rx);
	mem_deref(mf->dtls);
	mem_deref(mf->ct_gather);

	mem_deref(mf->label);
	mem_deref(mf->video.label);

	mem_deref(mf->peer_software);

	mem_deref(mf->mq);
}


/* called once or more, for each successful ICE candidate-pair */
static void icelite_estab_handler(struct ice_cand_attr *rcand, void *arg)
{
	struct mediaflow *mf = arg;

	mf->rcand = *rcand;
	mf->ice_ready = true;

	ice_established_handler(mf, &rcand->addr);
}


static void icelite_close_handler(int err, void *arg)
{
	struct mediaflow *mf = arg;

	ice_error(mf, err);
}


// TODO: check if we need this, or it can be moved ?
static void stun_udp_recv_handler(const struct sa *src,
				  struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	struct stun_unknown_attr ua;
	struct stun_msg *msg = NULL;

	debug("mediaflow: stun: receive %zu bytes from %J\n",
	      mbuf_get_left(mb), src);

	if (0 == stun_msg_decode(&msg, mb, &ua) &&
	    stun_msg_method(msg) == STUN_METHOD_BINDING) {

		switch (stun_msg_class(msg)) {

		case STUN_CLASS_SUCCESS_RESP:
		case STUN_CLASS_ERROR_RESP:
			(void)stun_ctrans_recv(mf->trice_stun, msg, &ua);
			break;

		default:
			re_printf("STUN message from %J dropped\n", src);
			break;
		}
	}

	mem_deref(msg);
}


static void mq_callback(int id, void *data, void *arg)
{
	struct mediaflow *mf = arg;

	switch (id) {

	case MQ_RTP_START:
		check_rtpstart(mf);
		break;
	}
}


/**
 * Create a new mediaflow.
 *
 * No ICE candidates are added here, you need to do that explicitly.
 *
 * @param aucodecl     Optional list of audio-codecs (struct aucodec)
 * @param audio_srate  Force a specific sample-rate (optional)
 * @param audio_ch     Force a specific number of channels (optional)
 */
int mediaflow_alloc(struct mediaflow **mfp,
		    struct tls *dtls,
		    const struct list *aucodecl,
		    const struct sa *laddr_sdp,
		    enum mediaflow_nat nat,
		    enum media_crypto cryptos,
		    bool external_rtp,
		    mediaflow_localcand_h *lcandh,
		    mediaflow_estab_h *estabh,
		    mediaflow_close_h *closeh,
		    void *arg)
{
	struct mediaflow *mf;
	struct le *le;
	struct sa laddr_rtp;
	uint16_t lport;
	int err;

	if (!mfp || !laddr_sdp)
		return EINVAL;

	if (!sa_isset(laddr_sdp, SA_ADDR))
		return EINVAL;

	mf = mem_zalloc(sizeof(*mf), destructor);
	if (!mf)
		return ENOMEM;

	mf->magic = MAGIC;

	err = mqueue_alloc(&mf->mq, mq_callback, mf);
	if (err)
		goto out;

	mf->dtls   = mem_ref(dtls);
	mf->pseq   = -1;
	mf->nat    = nat;
	mf->rtcp_mux = true;  /* MANDATORY in Zeta-world */
	mf->setup    = ACTPASS;
	mf->external_rtp = external_rtp;
	mf->cryptos_local = cryptos;

	mf->lcandh = lcandh;
	mf->estabh = estabh;
	mf->closeh = closeh;
	mf->arg    = arg;

	mf->rcand.type = (enum ice_cand_type)-1;
	mf->ice_tiebrk = rand_u64();

	err = pthread_mutex_init(&mf->mutex_enc, NULL);
	if (err)
		goto out;

	rand_str(mf->ice_ufrag, sizeof(mf->ice_ufrag));
	rand_str(mf->ice_pwd, sizeof(mf->ice_pwd));

	/* RTP must listen on 0.0.0.0 so that we can send/recv
	   packets on all interfaces */
	sa_init(&laddr_rtp, AF_INET);

	mf->enable_rtcp = !external_rtp;

	err = rtp_listen(&mf->rtp, IPPROTO_UDP, &laddr_rtp, 32768, 61000,
			 mf->enable_rtcp,
			 rtp_recv_handler, rtcp_recv_handler, mf);
	if (err) {
		warning("mediaflow: rtp_listen failed (%m)\n", err);
		goto out;
	}

	rtcp_enable_mux(mf->rtp, mf->rtcp_mux);

	lport = sa_port(rtp_local(mf->rtp));

	err = sdp_session_alloc(&mf->sdp, laddr_sdp);
	if (err)
		goto out;

	(void)sdp_session_set_lattr(mf->sdp, true, "tool", avs_software);

	err = sdp_media_add(&mf->sdpm, mf->sdp, "audio",
			    sa_port(rtp_local(mf->rtp)), "RTP/SAVPF");
	if (err)
		goto out;

	/* needed for new versions of WebRTC */
	err = sdp_media_set_alt_protos(mf->sdpm, 2,
				       "UDP/TLS/RTP/SAVPF", "RTP/SAVPF");
	if (err)
		goto out;

	sdp_media_set_lattr(mf->sdpm, false, "mid", "audio");

	rand_str(mf->cname, sizeof(mf->cname));
	rand_str(mf->msid, sizeof(mf->msid));
	err = uuid_v4(&mf->label);
	err |= uuid_v4(&mf->video.label);
	if (err)
		goto out;

	mf->lssrcv[MEDIA_AUDIO] = rtp_sess_ssrc(mf->rtp);

	debug("mediaflow: local SSRC is %u\n", mf->lssrcv[MEDIA_AUDIO]);

	err = sdp_media_set_lattr(mf->sdpm, false, "ssrc", "%u cname:%s",
				  mf->lssrcv[MEDIA_AUDIO], mf->cname);
	
#if 0 /* Following bundles the media streams */
	err |= sdp_media_set_lattr(mf->sdpm, false,
				   "ssrc", "%u msid:%s %s",
				   mf->lssrcv[MEDIA_AUDIO],
				   mf->msid, mf->label);
	err |= sdp_media_set_lattr(mf->sdpm, false,
				   "ssrc", "%u mslabel:%s",
				   mf->lssrcv[MEDIA_AUDIO],
				   mf->msid);
	err |= sdp_media_set_lattr(mf->sdpm, false,
				   "ssrc", "%u label:%s",
				   mf->lssrcv[MEDIA_AUDIO],
				   mf->label);
#endif
	if (err)
		goto out;

	/* ICE */
	if (nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {

		struct trice_conf conf = {
			.debug = false,
			.trace = false,
#if TARGET_OS_IPHONE
			.ansi = false
#elif defined (__ANDROID__)
			.ansi = false
#else
			.ansi = true
#endif
		};
		struct sa laddr_turn;
		bool controlling = false;  /* NOTE: this is set later */

		err = trice_alloc(&mf->trice, &conf, controlling,
				  mf->ice_ufrag, mf->ice_pwd);
		if (err) {
			warning("mediaflow: DUALSTACK trice error (%m)\n",
				err);
			goto out;
		}

		err = trice_set_software(mf->trice, avs_software);
		if (err)
			goto out;

		err = stun_alloc(&mf->trice_stun, NULL, NULL, NULL);
		if (err)
			goto out;

		/*
		 * tuning the STUN transaction values
		 *
		 * RTO=160 and RC=8 gives around 22 seconds timeout
		 */
		stun_conf(mf->trice_stun)->rto = 150;  /* milliseconds */
		stun_conf(mf->trice_stun)->rc =    6;  /* retransmits */

		switch (sa_af(laddr_sdp)) {

		case AF_INET:
			/* Need a common UDP-socket for STUN/TURN traffic */
			sa_set_str(&laddr_turn, "0.0.0.0", 0);
			break;

		case AF_INET6:
			err = net_default_source_addr_get(AF_INET6,
							  &laddr_turn);
			if (err) {
				warning("mediaflow: no local AF_INET6"
					" address (%m)\n", err);
				goto out;
			}
			info("mediaflow: laddr turn is v6 (%j)\n",
			     &laddr_turn);
			break;

		default:
			warning("mediaflow: invalid af in laddr sdp\n");
			break;
		}

		err = udp_listen(&mf->us_turn, &laddr_turn,
				 stun_udp_recv_handler, mf);
		if (err)
			goto out;

		err = udp_local_get(mf->us_turn, &laddr_turn);
		if (err)
			goto out;

		/* Virtual socket for directing outgoing Packets */
		err = udp_register_helper(&mf->trice_uh, rtp_sock(mf->rtp),
					  LAYER_ICE,
					  udp_helper_send_handler_trice,
					  NULL, mf);
		if (err)
			goto out;

	}
	else if (nat == MEDIAFLOW_ICELITE) {

		err = icelite_alloc(&mf->ice_lite, rtp_sock(mf->rtp),
				    mf->ice_ufrag, mf->ice_pwd,
				    icelite_estab_handler,
				    icelite_close_handler, mf);
		if (err)
			goto out;
	}

	/* populate SDP with all known audio-codecs */
	LIST_FOREACH(aucodecl, le) {
		struct aucodec *ac = list_ledata(le);

		if (external_rtp != ac->has_rtp) {
			warning("mediaflow: external_rtp=%d but "
				" aucodec '%s' has rtp=%d\n",
				external_rtp, ac->name, ac->has_rtp);
			err = EINVAL;
			goto out;
		}

		err = sdp_format_add(NULL, mf->sdpm, false,
				     ac->pt, ac->name, ac->srate, ac->ch,
				     ac->fmtp_ench, ac->fmtp_cmph, ac, false,
				     "%s", ac->fmtp);
		if (err)
			goto out;
	}

	/* Set ICE-options */
	switch (nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		sdp_session_set_lattr(mf->sdp, false, "ice-options",
				      "trickle");
		break;

	case MEDIAFLOW_ICELITE:
		sdp_session_set_lattr(mf->sdp, false, "ice-lite", NULL);
		break;

	default:
		break;
	}

	/* Mandatory */
	if (mf->rtcp_mux) {
		sdp_media_set_lattr(mf->sdpm, false, "rtcp-mux", NULL);
	}

	lport = sa_port(rtp_local(mf->rtp));
	sdp_media_set_lport_rtcp(mf->sdpm, lport+1);

	sdp_media_set_lattr(mf->sdpm, false, "ice-ufrag", "%s", mf->ice_ufrag);
	sdp_media_set_lattr(mf->sdpm, false, "ice-pwd", "%s", mf->ice_pwd);

	/* we enable support for DTLS-SRTP by default, so that the
	   SDP attributes are sent in the offer. the attributes
	   might change later though, depending on the SDP answer */

	if (cryptos & CRYPTO_DTLS_SRTP) {
		if (!mf->dtls) {
			warning("mediaflow: dtls context is missing\n");
		}
		err = dtls_listen(&mf->dtls_sock, NULL,
				  rtp_sock(mf->rtp), 2, LAYER_DTLS,
				  dtls_conn_handler, mf);
		if (err) {
			warning("mediaflow: dtls_listen failed (%m)\n", err);
			goto out;
		}

		/* Enable support for max 1 peer on the DTLS-session.
		 * this is needed to support incoming datagrams coming
		 * from different IP-addresses.
		 */
		dtls_set_one_peer(mf->dtls_sock);

		err = sdp_media_set_lattr(mf->sdpm, true,
					  "fingerprint", "sha-256 %H",
					  dtls_print_sha256_fingerprint,
					  mf->dtls);
		if (err)
			goto out;

		err = sdp_media_set_lattr(mf->sdpm, true, "setup", "actpass");
		if (err)
			goto out;
	}
	if (cryptos & CRYPTO_SDESC) {

		err  = handle_sdes_srtp_tx(mf);
		if (err)
			goto out;
	}

	/* install UDP socket helpers */
	err |= udp_register_helper(&mf->uh_srtp, rtp_sock(mf->rtp), LAYER_SRTP,
				   udp_helper_send_handler_srtp,
				   udp_helper_recv_handler_srtp,
				   mf);
	if (err)
		goto out;

	mf->laddr_default = *laddr_sdp;
	sa_set_port(&mf->laddr_default, lport);

	info("mediaflow: created new mediaflow with"
	     " local port %u and %u audio-codecs"
	     " and %s (external_rtp=%d)\n",
	     lport, list_count(aucodecl),
	     nat_name(mf->nat),
	     external_rtp);

 out:
	if (err)
		mem_deref(mf);
	else if (mfp)
		*mfp = mf;

	return err;
}


void mediaflow_set_rtp_handler(struct mediaflow *mf,
			       uint32_t audio_srate, uint8_t audio_ch,
			       mediaflow_audio_h *audioh,
			       mediaflow_rtp_h *rtph,
			       mediaflow_rtcp_h *rtcph)
{
	if (!mf)
		return;

	mf->srate    = audio_srate;
	mf->audio_ch = audio_ch;
	mf->audioh   = audioh;
	mf->rtph     = rtph;
	mf->rtcph    = rtcph;
}


int mediaflow_add_video(struct mediaflow *mf, struct list *vidcodecl)
{
	struct le *le;
	int err;

	if (!mf || !vidcodecl)
		return EINVAL;

	/* already added */
	if (mf->video.sdpm)
		return 0;

	info("mediaflow: adding video-codecs (%u)\n", list_count(vidcodecl));

	err = sdp_media_add(&mf->video.sdpm, mf->sdp, "video",
			    sa_port(rtp_local(mf->rtp)), "RTP/SAVPF");
	if (err)
		goto out;

	/* needed for new versions of WebRTC */
	err = sdp_media_set_alt_protos(mf->video.sdpm, 2,
				       "UDP/TLS/RTP/SAVPF", "RTP/SAVPF");
	if (err)
		goto out;

	sdp_session_set_lattr(mf->sdp, true, "group", "BUNDLE audio video");

	/* SDP media attributes */

	sdp_media_set_lattr(mf->video.sdpm, false, "mid", "video");
	sdp_media_set_lattr(mf->video.sdpm, false, "rtcp-mux", NULL);

	sdp_media_set_lport_rtcp(mf->video.sdpm,
				 sa_port(rtp_local(mf->rtp))+1);

	sdp_media_set_lattr(mf->video.sdpm, false,
			    "ice-ufrag", "%s", mf->ice_ufrag);
	sdp_media_set_lattr(mf->video.sdpm, false,
			    "ice-pwd", "%s", mf->ice_pwd);

	if (mf->dtls) {
		err = sdp_media_set_lattr(mf->video.sdpm, true,
					  "fingerprint", "sha-256 %H",
					  dtls_print_sha256_fingerprint,
					  mf->dtls);
		if (err)
			goto out;

		err = sdp_media_set_lattr(mf->video.sdpm, true,
					  "setup", "actpass");
		if (err)
			goto out;
	}
	

	{
		size_t ssrcc = list_count(vidcodecl);
		uint32_t ssrcv[ssrcc];
		char ssrc_group[16];
		char ssrc_fid[sizeof(ssrc_group)*ssrcc + 5];
		int i = 0;
		int k = 0;

		*ssrc_fid = '\0';
		
		LIST_FOREACH(vidcodecl, le) {
			struct vidcodec *vc = list_ledata(le);
			
			err = sdp_format_add(NULL, mf->video.sdpm, false,
					     vc->pt, vc->name, 90000, 1,
					     vc->fmtp_ench, vc->fmtp_cmph,
					     vc, false,
					     "%s", vc->fmtp);
			if (err)
				goto out;

			ssrcv[i] = rand_u32();
			re_snprintf(ssrc_group, sizeof(ssrc_group),
				    "%u ", ssrcv[i]);
			strcat(ssrc_fid, ssrc_group);
			++i;
		}
		ssrc_fid[strlen(ssrc_fid) - 1] = '\0';
		
		err = sdp_media_set_lattr(mf->video.sdpm, false, "ssrc-group",
					  "FID %s", ssrc_fid);
		
		if (err)
			goto out;

		if (ssrcc > 0)
			mf->lssrcv[MEDIA_VIDEO] = ssrcv[0];
		
		for(k = 0; k < i; ++k) {
			err = sdp_media_set_lattr(mf->video.sdpm, false,
						  "ssrc", "%u cname:%s",
						  ssrcv[k], mf->cname);
			err |= sdp_media_set_lattr(mf->video.sdpm, false,
						  "ssrc", "%u msid:%s %s",
						   ssrcv[k],
						   mf->msid, mf->video.label);
			err |= sdp_media_set_lattr(mf->video.sdpm, false,
						  "ssrc", "%u mslabel:%s",
						   ssrcv[k], mf->msid);
			err |= sdp_media_set_lattr(mf->video.sdpm, false,
						  "ssrc", "%u label:%s",
						   ssrcv[k], mf->video.label);
			if (err)
				goto out;
		}

		
	}


#if 0
	err |= sdp_media_set_lattr(mf->video.sdpm, true,
				   "rtcp-fb", "* nack pli");

	err |= sdp_media_set_lattr(mf->video.sdpm, false,
				   "ssrc", "%u cname:%s",
				   mf->lssrcv[MEDIA_VIDEO], mf->cname);
#endif


 out:
	return err;
}


void mediaflow_set_tag(struct mediaflow *mf, const char *tag)
{
	if (!mf)
		return;

	str_ncpy(mf->tag, tag, sizeof(mf->tag));
}


static int handle_dtls_srtp(struct mediaflow *mf)
{
	const char *fingerprint;
	const char *rsetup;
	struct pl fp_name;
	int err;

	fingerprint = sdp_media_session_rattr(mf->sdpm, mf->sdp,
					      "fingerprint");

	err = re_regex(fingerprint, str_len(fingerprint),
		       "[^ ]+ [0-9A-F:]*", &fp_name, NULL);
	if (err) {
		warning("mediaflow: could not parse fingerprint attr\n");
		return err;
	}

	debug("mediaflow: DTLS-SRTP fingerprint selected (%r)\n", &fp_name);

	re_printf_h *fp_printh;

	if (0 == pl_strcasecmp(&fp_name, "sha-1")) {
		fp_printh = (re_printf_h *)dtls_print_sha1_fingerprint;
	}
	else if (0 == pl_strcasecmp(&fp_name, "sha-256")) {
		fp_printh = (re_printf_h *)dtls_print_sha256_fingerprint;
	}
	else {
		warning("mediaflow: unsupported fingerprint (%r)\n", &fp_name);
		return EPROTO;
	}

	err = sdp_media_set_lattr(mf->sdpm, true, "fingerprint", "%r %H",
				  &fp_name, fp_printh, mf->dtls);
	if (err)
		return err;

	err = sdp_media_set_lattr(mf->sdpm, true, "setup", "active");
	if (err)
		return err;

	if (mf->video.sdpm) {
		err = sdp_media_set_lattr(mf->video.sdpm, true,
					  "setup", "active");
		if (err)
			return err;
	}

	rsetup = sdp_media_session_rattr(mf->sdpm, mf->sdp, "setup");

	if (0 == str_cmp(rsetup, "active"))
		mf->setup = PASSIVE;
	else
		mf->setup = ACTIVE;

	debug("mediaflow: incoming SDP offer has DTLS fingerprint = '%s'\n",
	      fingerprint);
	debug("mediaflow: remote_setup=%s, local_setup=%s\n",
	      rsetup, setup_name(mf->setup));

	/* DTLS has already been established, before SDP o/a */
	if (mf->crypto_ready && mf->tls_conn && !mf->crypto_verified) {

		info("mediaflow: sdp: verifying DTLS fp\n");

		if (!verify_fingerprint(mf, mf->sdp, mf->sdpm, mf->tls_conn)) {
			warning("mediaflow: dtls_srtp: could not verify"
				" remote fingerprint\n");
			return EAUTH;
		}

		mf->crypto_verified = true;
	}

	return 0;
}


static void trice_udp_recv_handler(const struct sa *src, struct mbuf *mb,
				   void *arg)
{
	struct mediaflow *mf = arg;
	enum packet pkt;

	pkt = classify_packet_type(mb);

	switch (pkt) {

	case PACKET_RTP:
	case PACKET_RTCP:
		/* inject data into RTP-socket! */
		udp_recv_helpers(rtp_sock(mf->rtp), src, mb, NULL);
		break;

	case PACKET_DTLS:
		debug("mediaflow: dtls: %zu bytes from %J\n",
		      mbuf_get_left(mb), src);
		dtls_recv_packet(mf->dtls_sock, src, mb);
		break;

	case PACKET_STUN:
		/* inject data into RTP-socket! */
		udp_recv_helpers(rtp_sock(mf->rtp), src, mb, NULL);
		break;

	default:
		re_printf("   @@@ udp: dropping %zu bytes from %J\n",
			  mbuf_get_left(mb), src);
		break;
	}
}


static void interface_destructor(void *data)
{
	struct interface *ifc = data;

	list_unlink(&ifc->le);
	mem_deref(ifc->lcand);
}


/* NOTE: only ADDRESS portion of 'addr' is used */
int mediaflow_add_local_host_candidate(struct mediaflow *mf,
				       const char *ifname,
				       const struct sa *addr)
{
	// XXX: adjust local-preference here for v4/v6
	uint32_t prio = ice_cand_calc_prio(ICE_CAND_TYPE_HOST, 0, 1);
	uint16_t lport;
	int err = 0;

	if (!mf || !addr)
		return EINVAL;

	if (!sa_isset(addr, SA_ADDR)) {
		warning("mediaflow: add_cand: address not set\n");
		return EINVAL;
	}
	if (sa_port(addr)) {
		warning("mediaflow: add_local_host: Port should not be set\n");
		return EINVAL;
	}

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {
		struct ice_lcand *lcand;
		struct interface *ifc;

		ifc = mem_zalloc(sizeof(*ifc), interface_destructor);
		if (!ifc)
			return ENOMEM;

		err = trice_lcand_add(&lcand, mf->trice, ICE_COMPID_RTP,
				      IPPROTO_UDP, prio, addr, NULL,
				      ICE_CAND_TYPE_HOST, NULL,
				      0,     /* tcptype */
				      NULL,  /* sock */
				      0);
		if (err) {
			warning("mediaflow: add_local_host[%j] failed (%m)\n",
				addr, err);
			return err;
		}

		/* hijack the UDP-socket of the local candidate
		 *
		 * NOTE: this must be done for all local candidates
		 */
		udp_handler_set(lcand->us, trice_udp_recv_handler, mf);

		err = sdp_media_set_lattr(mf->sdpm, false,
					  "candidate",
					  "%H", ice_cand_attr_encode, lcand);
		if (err)
			return err;

		if (ifname)
			str_ncpy(lcand->ifname, ifname, sizeof(lcand->ifname));

		list_append(&mf->interfacel, &ifc->le, ifc);
		ifc->lcand = lcand;
		ifc->addr = *addr;
		if (ifname)
			str_ncpy(ifc->ifname, ifname, sizeof(ifc->ifname));
		ifc->is_default = sa_cmp(addr, &mf->laddr_default, SA_ADDR);
		ifc->mf = mf;
	}
#if 1
	else if (mf->nat == MEDIAFLOW_ICELITE) {
		struct ice_cand_attr cand = {
			.foundation = "1",
			.compid     = ICE_COMPID_RTP,
			.proto      = IPPROTO_UDP,
			.prio       = prio,
			.type       = ICE_CAND_TYPE_HOST,
		};

		cand.addr = *addr;
		lport = sa_port(rtp_local(mf->rtp));
		sa_set_port(&cand.addr, lport);

		err = sdp_media_set_lattr(mf->sdpm, false,
					  "candidate",
					  "%H", ice_cand_attr_encode, &cand);
		if (err)
			return err;
	}
	else {
		warning("mediaflow: add_local_host: invalid nat %d\n",
			mf->nat);
		return ENOTSUP;
	}
#endif

	return err;
}


static void set_ice_role(struct mediaflow *mf, bool controlling)
{
	if (!mf)
		return;

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {
		if (sdp_media_session_rattr(mf->sdpm, mf->sdp, "ice-lite")) {
			info("mediaflow: remote side is ice-lite"
			     " -- force controlling\n");
			controlling = true;
		}
	}

	if (mf->trice)
		trice_set_controlling(mf->trice, controlling);
}


int mediaflow_generate_offer(struct mediaflow *mf, char *sdp, size_t sz)
{
	bool offer = true;
	struct mbuf *mb = NULL;
	int err = 0;

	if (!mf || !sdp)
		return EINVAL;

	if (mf->sdp_state != SDP_IDLE) {
		warning("mediaflow: invalid sdp state %d (%s)\n",
			mf->sdp_state, __func__);
	}
	mf->sdp_state = SDP_GOFF;

	mf->sdp_offerer = true;

	set_ice_role(mf, true);

	/* for debugging */
	sdp_session_set_lattr(mf->sdp, true,
			      offer ? "x-OFFER" : "x-ANSWER", NULL);

	err = sdp_encode(&mb, mf->sdp, offer);
	if (err) {
		warning("mediaflow: sdp encode(offer) failed (%m)\n", err);
		goto out;
	}

	if (re_snprintf(sdp, sz, "%b", mb->buf, mb->end) < 0) {
		err = ENOMEM;
		goto out;
	}

#if 1
	debug("---------- generate SDP offer ---------\n");
	debug("%s", sdp);
	debug("---------------------------------------\n");
#endif

	mf->sent_sdp = true;

 out:
	mem_deref(mb);

	return err;
}


int mediaflow_generate_answer(struct mediaflow *mf, char *sdp, size_t sz)
{
	bool offer = false;
	struct mbuf *mb = NULL;
	int err = 0;

	if (!mf || !sdp)
		return EINVAL;

	if (mf->sdp_state != SDP_HOFF) {
		warning("mediaflow: invalid sdp state (%s)\n", __func__);
	}
	mf->sdp_state = SDP_DONE;

	mf->sdp_offerer = false;

	set_ice_role(mf, false);

	/* for debugging */
	sdp_session_set_lattr(mf->sdp, true,
			      offer ? "x-OFFER" : "x-ANSWER", NULL);

	err = sdp_encode(&mb, mf->sdp, offer);
	if (err)
		goto out;

	if (re_snprintf(sdp, sz, "%b", mb->buf, mb->end) < 0) {
		err = ENOMEM;
		goto out;
	}

#if 1
	debug("---------- generate SDP answer ---------\n");
	debug("%s", sdp);
	debug("----------------------------------------\n");
#endif

	mf->sent_sdp = true;

 out:
	mem_deref(mb);

	return err;
}


static void get_sdp_candidates(struct mediaflow *mf)
{
	switch (mf->nat) {

	case MEDIAFLOW_ICELITE:
		sdp_media_rattr_apply(mf->sdpm, "candidate",
				      lite_candidate_handler, mf);
		break;

	default:
		break;
	}
}


/* after the SDP has been parsed,
   we can start to analyze it
   (this must be done _after_ sdp_decode() )
*/
static int post_sdp_decode(struct mediaflow *mf)
{
	const char *mid;
	int err = 0;

	if (0 == sdp_media_rport(mf->sdpm)) {
		warning("mediaflow: sdp medialine port is 0 - disabled\n");
		return EPROTO;
	}

	if (mf->trice) {

		const char *rufrag, *rpwd;

		rufrag = sdp_media_session_rattr(mf->sdpm, mf->sdp,
						 "ice-ufrag");
		rpwd   = sdp_media_session_rattr(mf->sdpm, mf->sdp,
						 "ice-pwd");
		if (!rufrag || !rpwd) {
			warning("mediaflow: post_sdp_decode: missing remote"
				" ice-ufrag/ice-pwd\n");
			warning("%H\n", sdp_session_debug, mf->sdp);
		}

		err |= trice_set_remote_ufrag(mf->trice, rufrag);
		err |= trice_set_remote_pwd(mf->trice, rpwd);
		if (err)
			goto out;
	}

	mid = sdp_media_rattr(mf->sdpm, "mid");
	if (mid) {
		debug("mediaflow: updating mid-value to '%s'\n", mid);
		sdp_media_set_lattr(mf->sdpm, true, "mid", mid);
	}

	if (!sdp_media_rattr(mf->sdpm, "rtcp-mux")) {
		warning("mediaflow: no 'rtcp-mux' attribute in SDP"
			" -- rejecting\n");
		err = EPROTO;
		goto out;
	}

	if (mf->video.sdpm) {
		const char *group;

		mid = sdp_media_rattr(mf->video.sdpm, "mid");
		if (mid) {
			debug("mediaflow: updating video mid-value "
			      "to '%s'\n", mid);
			sdp_media_set_lattr(mf->video.sdpm,
					    true, "mid", mid);
		}

		group = sdp_session_rattr(mf->sdp, "group");
		if (group) {
			sdp_session_set_lattr(mf->sdp, true, "group", group);
		}
	}

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {
		if (sdp_media_session_rattr(mf->sdpm, mf->sdp, "ice-lite")) {
			info("mediaflow: remote side is ice-lite"
			     " -- force controlling\n");
			set_ice_role(mf, true);
		}
	}

	get_sdp_candidates(mf);

	/*
	 * Handle negotiation about a common crypto-type
	 */

	mf->cryptos_remote = 0;
	if (sdp_media_session_rattr(mf->sdpm, mf->sdp, "fingerprint")) {

		mf->cryptos_remote |= CRYPTO_DTLS_SRTP;
	}

	if (sdp_media_rattr(mf->sdpm, "crypto")) {

		mf->cryptos_remote |= CRYPTO_SDESC;
	}


	// union
	mf->crypto = mf->cryptos_local & mf->cryptos_remote;

	info("mediaflow: negotiated crypto = %s\n",
	     crypto_name(mf->crypto));

	// TODO: check for a common crypto here, reject if nothing
	//       in common ?
	if (mf->cryptos_local && !mf->cryptos_remote) {
		warning("mediaflow: we offered crypto, but got none\n");
		return EPROTO;
	}

	if (mf->cryptos_local && mf->cryptos_remote) {

		if (!mf->crypto) {

			warning("mediaflow: no common crypto in SDP offer"
				" -- rejecting\n");
			err = EPROTO;
			goto out;
		}
	}

	if (mf->crypto & CRYPTO_DTLS_SRTP &&
	    mf->crypto & CRYPTO_SDESC) {
		info("negotiated both cryptos, fallback to dtls-srtp\n");
		mf->crypto = CRYPTO_DTLS_SRTP;
	}

	if (mf->crypto & CRYPTO_DTLS_SRTP) {

		err = handle_dtls_srtp(mf);
		if (err) {
			warning("mediaflow: handle_dtls_srtp failed (%m)\n",
				err);
			goto out;
		}

	}
	if (mf->crypto & CRYPTO_SDESC) {
		err |= handle_sdes_srtp_rx(mf);
		if (err)
			goto out;
	}

 out:
	return err;
}


int mediaflow_handle_offer(struct mediaflow *mf, const char *sdp)
{
	struct mbuf *mbo = NULL;
	int err = 0;

	if (!mf || !sdp)
		return EINVAL;

	if (mf->sdp_state != SDP_IDLE) {
		warning("mediaflow: invalid sdp state %d (%s)\n",
			mf->sdp_state, __func__);
	}
	mf->sdp_state = SDP_HOFF;

	++mf->stat.n_sdp_recv;

	mf->sdp_offerer = false;

	set_ice_role(mf, false);

	mbo = mbuf_alloc(1024);
	if (!mbo)
		return ENOMEM;

	err = mbuf_write_str(mbo, sdp);
	if (err)
		goto out;

	mbo->pos = 0;

#if 1
	debug("---------- recv SDP offer ----------\n");
	debug("%s", sdp);
	debug("------------------------------------\n");
#endif

	err = sdp_decode(mf->sdp, mbo, true);
	if (err) {
		warning("mediaflow: could not parse SDP offer"
			" [%zu bytes] (%m)\n",
			mbo->end, err);
		goto out;
	}

	mf->got_sdp = true;

	/* after the SDP offer has been parsed,
	   we can start to analyze it */

	err = post_sdp_decode(mf);
	if (err)
		goto out;


	start_codecs(mf);

	if (sdp_media_rformat(mf->video.sdpm, NULL)) {

		info("mediaflow: SDP has video enabled\n");

		mf->video.has_media = true;
		start_video_codecs(mf);
	}
	else {
		info("mediaflow: video is disabled\n");
	}

 out:
	mem_deref(mbo);

	return err;
}


int mediaflow_handle_answer(struct mediaflow *mf, const char *sdp)
{
	struct mbuf *mb;
	bool offer = false;
	int err = 0;

	if (!mf || !sdp)
		return EINVAL;

	if (mf->sdp_state != SDP_GOFF) {
		warning("mediaflow: invalid sdp state (%s)\n", __func__);
	}
	mf->sdp_state = SDP_DONE;

	++mf->stat.n_sdp_recv;

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	err = mbuf_write_str(mb, sdp);
	if (err)
		goto out;

	mb->pos = 0;

#if 1
	debug("---------- recv SDP answer ----------\n");
	debug("%s", sdp);
	debug("------------------------------------\n");
#endif

	err = sdp_decode(mf->sdp, mb, offer);
	if (err) {
		warning("mediaflow: could not parse SDP answer"
			" [%zu bytes] (%m)\n", mb->end, err);
		goto out;
	}

	mf->got_sdp = true;

	/* after the SDP has been parsed,
	   we can start to analyze it
	   (this must be done _after_ sdp_decode() )
	*/

	err = post_sdp_decode(mf);
	if (err)
		goto out;

	start_codecs(mf);

	if (sdp_media_rformat(mf->video.sdpm, NULL)) {

		info("mediaflow: SDP has video enabled\n");

		mf->video.has_media = true;
		start_video_codecs(mf);
	}
	else {
		info("mediaflow: video is disabled\n");
	}


 out:
	mem_deref(mb);

	return err;
}


/*
 * This function does 2 things:
 *
 * - handle offer
 * - generate answer
 */
int mediaflow_offeranswer(struct mediaflow *mf,
			  char *answer, size_t answer_sz,
			  const char *offer)
{
	int err = 0;

	if (!mf || !answer || !offer)
		return EINVAL;

	err = mediaflow_handle_offer(mf, offer);
	if (err)
		return err;

	err = mediaflow_generate_answer(mf, answer, answer_sz);
	if (err)
		return err;

	return 0;
}


void mediaflow_sdpstate_reset(struct mediaflow *mf)
{
	if (!mf)
		return;

	mf->sdp_state = SDP_IDLE;

	sdp_session_del_lattr(mf->sdp, "x-OFFER");
	sdp_session_del_lattr(mf->sdp, "x-ANSWER");

	mf->got_sdp = false;
	mf->sent_sdp = false;
}


int mediaflow_send_audio(struct mediaflow *mf,
			 const int16_t *sampv, size_t sampc)
{
	struct auenc_state *aes;
	const struct aucodec *ac;
	int err;

	if (!mf || !sampv)
		return EINVAL;

	pthread_mutex_lock(&mf->mutex_enc);

	/* check if media-stream is ready for sending */
	if (!mediaflow_is_ready(mf)) {
		warning("mediaflow: send_audio: not ready\n");
		err = EINTR;
		goto out;
	}

	/* find the active Audio encoder state and corresponding Codec */
	aes = mf->aes;
	if (!aes) {
		warning("mediaflow: send_audio: no audio encoder\n");
		err = ENOENT;
		goto out;
	}
	ac = auenc_get(aes);

	if (!ac->ench) {
		warning("mediaflow: send_audio: cannot send, no encoder"
			" for codec '%s'\n", ac->name);
		err = ENOSYS;
		goto out;
	}

	/* use that codec to encode audio and send it */
	err = ac->ench(aes, sampv, sampc);
	if (err) {
		warning("mediaflow: send_audio: encode failed: %m\n", err);
		goto out;
	}

 out:
	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}


int mediaflow_send_rtp(struct mediaflow *mf, const struct rtp_header *hdr,
		       const uint8_t *pld, size_t pldlen)
{
	struct mbuf *mb;
	size_t headroom = 0;
	int err = 0;

	if (!mf || !pld || !pldlen || !hdr)
		return EINVAL;

	MAGIC_CHECK(mf);

	/* check if media-stream is ready for sending */
	if (!mediaflow_is_ready(mf)) {
		warning("mediaflow: send_rtp: not ready\n");
		return EINTR;
	}

	headroom = get_headroom(mf);

	mb = mbuf_alloc(headroom + 256);
	if (!mb)
		return ENOMEM;

	mb->pos = headroom;
	err  = rtp_hdr_encode(mb, hdr);
	err |= mbuf_write_mem(mb, pld, pldlen);
	if (err)
		goto out;

	mb->pos = headroom;

	update_tx_stats(mf, pldlen); /* This INCLUDES the rtp header! */

	err = udp_send(rtp_sock(mf->rtp), &mf->rcand.addr, mb);
	if (err)
		goto out;

 out:
	mem_deref(mb);

	return err;
}


/* NOTE: might be called from different threads */
int mediaflow_send_raw_rtp(struct mediaflow *mf, const uint8_t *buf,
			   size_t len)
{
	struct mbuf *mb;
	size_t headroom;
	int err;

	if (!mf || !buf)
		return EINVAL;

	MAGIC_CHECK(mf);

	/* check if media-stream is ready for sending */
	if (!mediaflow_is_ready(mf)) {
		warning("mediaflow: send_raw_rtp(%zu bytes): not ready"
			" [ice=%d, crypto=%d]\n",
			len, mf->ice_ready, mf->crypto_ready);
		return EINTR;
	}

	pthread_mutex_lock(&mf->mutex_enc);

	headroom = get_headroom(mf);

	mb = mbuf_alloc(headroom + len);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	mb->pos = headroom;
	err = mbuf_write_mem(mb, buf, len);
	if (err)
		goto out;
	mb->pos = headroom;

	if (len >= RTP_HEADER_SIZE)
		update_tx_stats(mf, len - RTP_HEADER_SIZE);

	err = udp_send(rtp_sock(mf->rtp), &mf->rcand.addr, mb);
	if (err)
		goto out;

 out:
	mem_deref(mb);

	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}


void mediaflow_rtp_start_send(struct mediaflow *mf)
{
	if (!mf)
		return;

	if (!mf->sent_rtp) {
		info("mediaflow: first RTP packet sent\n");
		mf->sent_rtp = true;
		check_rtpstart(mf);
	}
}


int mediaflow_send_raw_rtcp(struct mediaflow *mf,
			    const uint8_t *buf, size_t len)
{
	struct mbuf *mb;
	size_t headroom;
	int err;

	if (!mf || !buf || !len)
		return EINVAL;

	MAGIC_CHECK(mf);

	/* check if media-stream is ready for sending */
	if (!mediaflow_is_ready(mf)) {
		warning("mediaflow: send_raw_rtcp(%zu bytes): not ready"
			" [ice=%d, crypto=%d]\n",
			len, mf->ice_ready, mf->crypto_ready);
		return EINTR;
	}

	pthread_mutex_lock(&mf->mutex_enc);

	headroom = get_headroom(mf);

	mb = mbuf_alloc(headroom + 256);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	mb->pos = headroom;
	err = mbuf_write_mem(mb, buf, len);
	if (err)
		goto out;
	mb->pos = headroom;

	err = udp_send(rtp_sock(mf->rtp), &mf->rcand.addr, mb);
	if (err)
		goto out;

 out:
	mem_deref(mb);

	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}


int mediaflow_get_rtcpstats(struct mediaflow *mf, struct rtcp_stats *stats)
{
	if (!mf || !stats)
		return EINVAL;

	if (!mediaflow_is_ready(mf))
		return EINTR;

	*stats = mf->stats;

	return 0;
}


static bool sa_ipv4_is_private(const struct sa *sa)
{
	static const struct {
		uint32_t addr;
		uint32_t mask;
	} netv[] = {
		{ 0x0a000000, 0xff000000u},  /* 10.0.0.0/8     */
		{ 0xac100000, 0xfff00000u},  /* 172.16.0.0/12  */
		{ 0xc0a80000, 0xffff0000u},  /* 192.168.0.0/16 */
	};
	uint32_t addr = sa_in(sa);
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(netv); i++) {

		if ((addr & netv[i].mask) == netv[i].addr)
			return true;
	}

	return false;
}


static bool rcandidate_ds_handler(const char *name, const char *val, void *arg)
{
	struct mediaflow *mf = arg;
	struct ice_cand_attr rcand;
	int err;

	err = ice_cand_attr_decode(&rcand, val);
	if (err || rcand.compid != ICE_COMPID_RTP ||
	    rcand.proto != IPPROTO_UDP)
		goto out;

	err = trice_rcand_add(NULL, mf->trice, rcand.compid,
			      rcand.foundation, rcand.proto, rcand.prio,
			      &rcand.addr, rcand.type, rcand.tcptype);
	if (err) {
		warning("mediaflow: rcand: trice_rcand_add failed"
			" [%J] (%m)\n",
			&rcand.addr, err);
	}

 out:
	return false;
}


static void trice_estab_handler(struct ice_candpair *pair,
				const struct stun_msg *msg, void *arg)
{
	struct mediaflow *mf = arg;
	void *sock;

	info("mediaflow: ice pair established  %H\n",
	     trice_candpair_debug, pair);

	/* verify local candidate */
	sock = trice_lcand_sock(mf->trice, pair->lcand);
	if (!sock) {
		warning("mediaflow: estab: lcand has no sock [%H]\n",
			trice_cand_print, pair->lcand);
		return;
	}

	/* We use the first pair that is working */
	if (!mf->ice_ready) {
		struct stun_attr *attr;

		mem_deref(mf->sel_lcand);
		mf->sel_lcand = mem_ref(pair->lcand);
		mf->rcand = pair->rcand->attr;
		mf->ice_ready = true;

		attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
		if (attr && !mf->peer_software) {
			(void)str_dup(&mf->peer_software, attr->v.software);
		}

		info("mediaflow: trice: setting peer to %H [%s]\n",
		     print_cand, pair->rcand,
		     mf->peer_software);

#if 1
		// TODO: extra for PRFLX
		udp_handler_set(pair->lcand->us, trice_udp_recv_handler, mf);
#endif

		ice_established_handler(mf, &pair->rcand->attr.addr);
	}
}


static void trice_failed_handler(int err, uint16_t scode,
				 struct ice_candpair *pair, void *arg)
{
	struct mediaflow *mf = arg;

	info("mediaflow: candpair failed [%H]\n",
	     trice_candpair_debug, pair);

	// TODO: check if checklist is complete AND EOC
}


static void tmr_nat_handler(void *arg)
{
	struct mediaflow *mf = arg;
	const struct sa *raddr;

	raddr = sdp_media_raddr(mf->sdpm);

	mf->rcand.addr = *raddr;

	ice_established_handler(mf, raddr);
}


/*
 * Start the mediaflow state-machine.
 *
 * this should be called after SDP exchange is complete. we will now
 * start sending ICE connectivity checks to all known remote candidates
 */
int mediaflow_start_ice(struct mediaflow *mf)
{
	struct le *le;

	if (!mf)
		return EINVAL;

	MAGIC_CHECK(mf);

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {

		int err;

		sdp_media_rattr_apply(mf->sdpm, "candidate",
				      rcandidate_ds_handler, mf);

		/* add permission for ALL TURN-Clients */
		for (le = mf->turnconnl.head; le; le = le->next) {
			struct turn_conn *conn = le->data;

			if (conn->turnc && conn->turn_allocated) {
				add_permission_to_remotes_ds(mf, conn->turnc);
			}
		}

		info("mediaflow: start_ice: starting ICE checklist with"
		     " %u remote candidates\n",
		     list_count(trice_rcandl(mf->trice)));

		err = trice_checklist_start(mf->trice, mf->trice_stun,
					    20, true,
					    trice_estab_handler,
					    trice_failed_handler,
					    mf);
		if (err) {
			warning("could not start ICE checklist (%m)\n", err);
			return err;
		}
	}
	else if (mf->nat == MEDIAFLOW_NAT_NONE) {

		/* async callback for the "established" handler */
		tmr_start(&mf->tmr_nat, 0, tmr_nat_handler, mf);
	}

	return 0;
}


int mediaflow_add_rcand(struct mediaflow *mf, const char *sdp,
			const char *mid, int idx)
{
	struct ice_cand_attr rcand;
	struct le *le;
	struct pl pl;
	char attr[256];
	int err;

	if (!mf)
		return EINVAL;

	if (0 == str_casecmp(sdp, "a=end-of-candidates")) {
		mf->ice_remote_eoc = true;
		return 0;
	}

	if (re_regex(sdp, strlen(sdp), "candidate:[^\r\n]+", &pl)) {
		pl_set_str(&pl, sdp);
	}

	pl_strcpy(&pl, attr, sizeof(attr));

	/* ignore candidates that we cannot decode */
	if (ice_cand_attr_decode(&rcand, attr) ||
	    rcand.compid != ICE_COMPID_RTP ||
	    rcand.proto != IPPROTO_UDP)
		return 0;

	++mf->stat.n_cand_recv;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		info("mediaflow: new remote candidate (%H)\n",
		     trice_cand_print, &rcand);

		err = trice_rcand_add(NULL, mf->trice, rcand.compid,
				      rcand.foundation, rcand.proto,
				      rcand.prio,
				      &rcand.addr, rcand.type, rcand.tcptype);
		if (err) {
			warning("mediaflow: add_rcand: trice_rcand_add failed"
				" [%J] (%m)\n",
				&rcand.addr, err);
		}

		/* add permission for ALL TURN-Clients */
		for (le = mf->turnconnl.head; le; le = le->next) {
			struct turn_conn *tc = le->data;

			if (tc->turnc && tc->turn_allocated)
				add_turn_permission_ds(mf, tc->turnc, &rcand);
		}

		/* NOTE: checklist must be re-started for every new
		 *       remote candidate
		 */

		info("mediaflow: start_ice: starting ICE checklist with"
		     " %u remote candidates\n",
		     list_count(trice_rcandl(mf->trice)));

		err = trice_checklist_start(mf->trice, mf->trice_stun,
					    20, true,
					    trice_estab_handler,
					    trice_failed_handler,
					    mf);
		if (err) {
			warning("could not start ICE checklist (%m)\n",
				err);
			return err;
		}
		break;

	case MEDIAFLOW_ICELITE:
		err = icelite_cand_add(mf->ice_lite, &rcand);
		if (err) {
			warning("mediaflow: lite_cand_add failed (%m)\n",
				err);
		}
		break;

	default:
		break;
	}

	return 0;
}

static int start_audio(struct mediaflow *mf)
{
	const struct aucodec *ac;

	if (mf->aes == NULL)
		return ENOSYS;

	ac = auenc_get(mf->aes);
	if (ac && ac->enc_start)
		ac->enc_start(mf->aes);

	ac = audec_get(mf->ads);
	if (ac && ac->dec_start)
		ac->dec_start(mf->ads);

	return 0;
}


static int stop_audio(struct mediaflow *mf)
{
	const struct aucodec *ac;

	if (!mf)
		return EINVAL;

	/* audio */
	ac = auenc_get(mf->aes);
	if (ac && ac->enc_stop)
		ac->enc_stop(mf->aes);

	ac = audec_get(mf->ads);
	if (ac && ac->dec_stop)
		ac->dec_stop(mf->ads);

	return 0;
}


static int hold_video(struct mediaflow *mf, bool hold)
{
	const struct vidcodec *vc;

	vc = viddec_get(mf->video.vds);
	if (vc && vc->dec_holdh) {
		info("mediaflow: hold_media: holding"
		     " video decoder (%s)\n", vc->name);

		vc->dec_holdh(mf->video.vds, hold);
	}
	
	vc = videnc_get(mf->video.ves);
	if (vc && vc->enc_holdh) {
		info("mediaflow: hold_media: holding"
		     " video encoder (%s)\n", vc->name);
		
		vc->enc_holdh(mf->video.ves, hold);
	}

	return 0;
}


int mediaflow_hold_media(struct mediaflow *mf, bool hold)
{
	int err = 0;
	
	if (!mf)
		return EINVAL;

	err = hold ? stop_audio(mf) : start_audio(mf);
	err |= hold_video(mf, hold);

	mf->hold = hold;
	
	return err;
}


int mediaflow_start_media(struct mediaflow *mf)
{
	int err = 0;
	
	if (!mf)
		return EINVAL;

	if (mf->hold && mf->started)
		return mediaflow_hold_media(mf, false);
		
	
	if (mf->started)
		return 0;

	mf->started = true;
	
	start_audio(mf);
	
	if (mf->video.has_media) {
		const struct vidcodec *vc;

		vc = viddec_get(mf->video.vds);
		if (vc && vc->dec_starth) {
			info("mediaflow: start_media: starting"
			     " video decoder (%s)\n", vc->name);

			err = vc->dec_starth(mf->video.vds);
			if (err) {
				warning("mediaflow: could not start"
					" video decoder (%m)\n", err);
				err = 0;
			}
		}

		if (mf->video.started)
			mediaflow_set_video_send_active(mf, mf->video.started);
	}

	if (!tmr_isrunning(&mf->tmr_rtp))
		tmr_start(&mf->tmr_rtp, 5000, timeout_rtp, mf);

	return err;
}

int mediaflow_set_video_send_active(struct mediaflow *mf, bool video_active)
{
	const struct vidcodec *vc;
	int err = ENOSYS;

	if (mf->video.has_media) {
		if (video_active) {

			vc = videnc_get(mf->video.ves);
			if (vc && vc->enc_starth) {
				info("mediaflow: start_media: starting"
				     " video encoder (%s)\n", vc->name);

				err = vc->enc_starth(mf->video.ves);
				if (err) {
					warning("mediaflow: could not start"
						" video encoder (%m)\n", err);
					return err;
				}
			}
			mf->video.started = true;
		}
		else {

			vc = videnc_get(mf->video.ves);
			if (vc && vc->enc_stoph) {
				info("mediaflow: stop_media: stopping"
				     " video encoder (%s)\n", vc->name);
				vc->enc_stoph(mf->video.ves);
			}
			mf->video.started = false;
		}
	}
	return err;
}


bool mediaflow_is_sending_video(struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->video.started;
}


int mediaflow_aucodec_stats(struct mediaflow *mf, struct mbuf **mbp)
{
	const struct aucodec *ac;
	int err = ENOSYS;

	if (!mf)
		return EINVAL;

	ac = audec_get(mf->ads);
	if (ac && ac->dec_stats) {
		err = ac->dec_stats(mf->ads, mbp);
	}

	return err;
}


void mediaflow_stop_media(struct mediaflow *mf)
{
	const struct aucodec *ac;
	const struct vidcodec *vc;

	if (!mf)
		return;

	if (!mf->started)
		return;

	mf->started = false;

	/* audio */
	ac = auenc_get(mf->aes);
	if (ac && ac->enc_stop)
		ac->enc_stop(mf->aes);

	ac = audec_get(mf->ads);
	if (ac && ac->dec_stop)
		ac->dec_stop(mf->ads);

	/* video */
	vc = videnc_get(mf->video.ves);
	if (vc && vc->enc_stoph) {
		info("mediaflow: stop_media: stopping"
		     " video encoder (%s)\n", vc->name);
		vc->enc_stoph(mf->video.ves);
	}

	vc = viddec_get(mf->video.vds);
	if (vc && vc->dec_stoph) {
		info("mediaflow: stop_media: stopping"
		     " video decoder (%s)\n", vc->name);
		vc->dec_stoph(mf->video.vds);
	}

	tmr_cancel(&mf->tmr_rtp);
	mf->sent_rtp = false;
	mf->got_rtp = false;
}


static uint32_t calc_prio(enum ice_cand_type type, int af, int turn_proto)
{
	uint16_t lpref = 0;

	if (af == AF_INET6)
		lpref = turn_proto==IPPROTO_UDP ? 65535 : 65533;
	else
		lpref = turn_proto==IPPROTO_UDP ? 65534 : 65532;

	return ice_cand_calc_prio(type, lpref, ICE_COMPID_RTP);
}


static void submit_local_candidate(struct mediaflow *mf,
				   enum ice_cand_type type,
				   const struct sa *addr,
				   const struct sa *rel_addr, bool eoc,
				   int turn_proto, void **sockp)
{
	struct ice_cand_attr attr = {
		.foundation = "1",  /* NOTE: same foundation for all */
		.compid     = ICE_COMPID_RTP,
		.proto      = IPPROTO_UDP,
		.prio       = ice_cand_calc_prio(type, 0, ICE_COMPID_RTP),
		.addr       = *addr,
		.type       = type,
		.tcptype    = 0,
	};
	char cand[512];
	size_t candc = eoc ? 2 : 1;

	struct zapi_candidate candv[] = {
		{
			.mid = "audio",
			.mline_index = 0,
			.sdp = cand,
		},
		{
			.mid = "audio",
			.mline_index = 0,
			.sdp = "a=end-of-candidates",
		},
	};

	if (mf->nat == MEDIAFLOW_TRICKLEICE_DUALSTACK) {

		struct ice_lcand *lcand;
		void *sock;
		int err;

		attr.prio = calc_prio(type, sa_af(addr), turn_proto);

		if (turn_proto == IPPROTO_UDP)
			sock = mf->us_turn;  /* NOTE this */
		else
			sock = NULL;

		err = trice_lcand_add(&lcand, mf->trice, attr.compid,
				      attr.proto, attr.prio, addr, NULL,
				      attr.type, rel_addr,
				      0 /* tcptype */,
				      sock, LAYER_ICE);
		if (err) {
			warning("mediaflow: add local cand failed (%m)\n",
				err);
			return;
		}

		if (sockp)
			*sockp = lcand->us;

		/* hijack the UDP-socket of the local candidate
		 *
		 * NOTE: this must be done for all local candidates
		 */
		udp_handler_set(lcand->us, trice_udp_recv_handler, mf);

		re_snprintf(cand, sizeof(cand), "a=candidate:%H",
			    ice_cand_attr_encode, lcand);

		/* also add the candidate to SDP */
		err = sdp_media_set_lattr(mf->sdpm, false,
					  "candidate",
					  "%H", ice_cand_attr_encode, lcand);
		if (err)
			return;
	}
#if 1
	else {
		if (rel_addr)
			attr.rel_addr = *rel_addr;

		re_snprintf(cand, sizeof(cand), "a=candidate:%H",
			    ice_cand_attr_encode, &attr);
	}
#endif

	if (mf->lcandh)
		mf->lcandh(candv, candc, mf->arg);
}


static void gather_stun_resp_handler(int err, uint16_t scode,
				     const char *reason,
				     const struct stun_msg *msg, void *arg)
{
	struct mediaflow *mf = arg;
	struct stun_attr *map = NULL, *attr;

	if (err) {
		warning("mediaflow: stun_resp %m\n", err);
		return;
	}

	if (scode) {
		warning("mediaflow: stun_resp %u %s\n", scode, reason);
		return;
	}

	map = stun_msg_attr(msg, STUN_ATTR_XOR_MAPPED_ADDR);
	if (!map) {
		warning("mediaflow: xor_mapped_addr attr missing\n");
		return;
	}

	mf->stun_ok = true;

	attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
	info("mediaflow: STUN allocation OK"
	     " (mapped=%J) [%s]\n",
	     &map->v.xor_mapped_addr,
	     attr ? attr->v.software : "");

	submit_local_candidate(mf, ICE_CAND_TYPE_SRFLX,
			       &map->v.xor_mapped_addr, &mf->laddr_default,
			       true, IPPROTO_UDP, NULL);

	mf->ice_local_eoc = true;

	if (mf->gatherh)
		mf->gatherh(mf->arg);
}


// TODO: should be done PER interface
int mediaflow_gather_stun(struct mediaflow *mf, const struct sa *stun_srv)
{
	struct stun *stun = NULL;
	void *sock = NULL;
	int err;

	if (!mf || !stun_srv)
		return EINVAL;

	if (mf->ct_gather)
		return EALREADY;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		if (!mf->trice)
			return EINVAL;

		stun = mf->trice_stun;
		sock = mf->us_turn;
		break;

	default:
		return EINVAL;
	}

	if (!stun || !sock) {
		warning("mediaflow: gather_stun: no STUN/SOCK instance\n");
		return EINVAL;
	}

	err = stun_request(&mf->ct_gather, stun, IPPROTO_UDP,
			   sock, stun_srv, 0,
			   STUN_METHOD_BINDING, NULL, 0, false,
			   gather_stun_resp_handler, mf, 0);
	if (err) {
		warning("mediaflow: stun_request failed (%m)\n", err);
		return err;
	}

	mf->stun_server = true;

	return 0;
}


static void turnc_perm_handler(void *arg)
{
	struct mediaflow *mf = arg;
	(void)arg;

	info("turnconn: TURN permission added OK\n");
}


static void add_turn_permission_ds(struct mediaflow *mf,
				   struct turnc *turnc,
				   const struct ice_cand_attr *rcand)
{
	bool add;
	int err;

	if (!mf || !rcand)
		return;

	if (AF_INET != sa_af(&rcand->addr))
		return;

	if (rcand->type == ICE_CAND_TYPE_HOST)
		add = !sa_ipv4_is_private(&rcand->addr);
	else
		add = true;

	if (add) {
		info("mediaflow: DS: adding TURN permission"
		     " to remote address %s.%j <turnc=%p>\n",
		     ice_cand_type2name(rcand->type),
		     &rcand->addr, turnc);

		err = turnc_add_perm(turnc, &rcand->addr,
				     turnc_perm_handler, mf);
		if (err) {
			warning("mediaflow: failed to"
				" add permission (%m)\n",
				err);
		}
	}
}


static void add_permission_to_remotes(struct mediaflow *mf)
{
	struct turn_conn *conn;
	struct le *le;

	if (!mf)
		return;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		// todo: this works only for UDP for now, we must
		//       iterate over all TURN-clients
		conn = turnconn_find_allocated(&mf->turnconnl, IPPROTO_UDP);
		if (!mf->trice || !conn)
			return;

		for (le = list_head(trice_rcandl(mf->trice));
		     le;
		     le = le->next) {

			struct ice_rcand *rcand = le->data;

			add_turn_permission_ds(mf, conn->turnc, &rcand->attr);
		}
		break;

	default:
		break;
	}
}


static void add_permission_to_remotes_ds(struct mediaflow *mf,
					 struct turnc *turnc)
{
	struct le *le;

	if (!mf->trice)
		return;

	for (le = list_head(trice_rcandl(mf->trice));
	     le;
	     le = le->next) {

		struct ice_rcand *rcand = le->data;

		add_turn_permission_ds(mf, turnc, &rcand->attr);
	}
}


/*
 * Translate an IPv4-address to a NAT64-mapped IPv6-address
 *
 *   input:  1.2.3.4
 *   output: 64:ff9b::1.2.3.4
 *
 */
static int sa_translate_nat64(struct sa *sa6, const struct sa *sa4)
{
	char buf[256];
	uint16_t port;

	if (!sa6 || !sa4)
		return EINVAL;

	if (sa_af(sa4) != AF_INET)
		return EAFNOSUPPORT;

	if (re_snprintf(buf, sizeof(buf), "64:ff9b::%j", sa4) < 0)
		return ENOMEM;

	port = sa_port(sa4);

	return sa_set_str(sa6, buf, port);
}


/* all outgoing UDP-packets must be sent via
 * the TCP-connection to the TURN server
 */
static bool turntcp_send_handler(int *err, struct sa *dst,
				 struct mbuf *mb, void *arg)
{
	struct turn_conn *tc = arg;

#if 0
	re_printf("   ~~~~ turntcp: SEND [presz=%zu] (%zu bytes to %J)\n",
		  mb->pos, mbuf_get_left(mb), dst);
#endif

	*err = turnc_send(tc->turnc, dst, mb);
	if (*err) {
		re_printf("mediaflow: turnc_send failed (%zu bytes to %J)\n",
			mbuf_get_left(mb), dst);
	}

	return true;
}


static void turnconn_estab_handler(struct turn_conn *conn,
				   const struct sa *relay_addr,
				   const struct sa *mapped_addr, void *arg)
{
	struct mediaflow *mf = arg;
	void *sock = NULL;
	int err;

	/* NOTE: important to ship the SRFLX before RELAY cand. */

	if (conn->proto == IPPROTO_UDP) {
		submit_local_candidate(mf, ICE_CAND_TYPE_SRFLX,
				       mapped_addr, &mf->laddr_default, false,
				       conn->proto, NULL);
	}

	submit_local_candidate(mf, ICE_CAND_TYPE_RELAY,
			       relay_addr, mapped_addr, true,
			       conn->proto, &sock);

	if (conn->proto == IPPROTO_TCP) {
		/* NOTE: this is needed to snap up outgoing UDP-packets */
		conn->us_app = mem_ref(sock);
		err = udp_register_helper(&conn->uh_app, sock, LAYER_TURN,
					  turntcp_send_handler, NULL, conn);
		if (err) {
			warning("mediaflow: TURN failed to register UDP-helper"
				" (%m)\n", err);
			goto error;
		}
	}

	mf->ice_local_eoc = true;

	add_permission_to_remotes_ds(mf, conn->turnc);
	add_permission_to_remotes(mf);

	if (mf->gatherh)
		mf->gatherh(mf->arg);

	return;

 error:
	/* NOTE: only flag an error if ICE is not established yet */
	if (!mf->ice_ready)
		ice_error(mf, err ? err : EPROTO);
}


static void turnconn_error_handler(int err, void *arg)
{
	struct mediaflow *mf = arg;

	/* NOTE: only flag an error if ICE is not established yet */
	if (!mf->ice_ready)
		ice_error(mf, err ? err : EPROTO);
}


/*
 * Gather RELAY and SRFLX candidates (UDP only)
 */
int mediaflow_gather_turn(struct mediaflow *mf, const struct sa *turn_srv,
			  const char *username, const char *password)
{
	struct sa turn_srv6, laddr_turn;
	void *sock = NULL;
	int err;

	if (!mf || !turn_srv)
		return EINVAL;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		if (!mf->trice)
			return EINVAL;

		sock = mf->us_turn;

		err = udp_local_get(sock, &laddr_turn);
		if (err)
			return err;

		/* NOTE: this should only be done if we detect that
		 *       we are behind a NAT64
		 */
		if (sa_af(&laddr_turn) != sa_af(turn_srv)) {

			err = sa_translate_nat64(&turn_srv6, turn_srv);
			if (err) {
				warning("gather_turn: sa_translate_nat64(%j)"
					" failed (%m)\n",
					turn_srv, err);
				return err;
			}

			re_printf("mediaflow: Dualstack: TRANSLATE NAT64"
			     " (%J ----> %J)\n",
			     turn_srv, &turn_srv6);

			turn_srv = &turn_srv6;
		}
		break;

	default:
		return EINVAL;
	}

	debug("mediaflow: turnc_alloc: username='%s' srv=%J\n",
	      username, turn_srv);

	err = turnconn_alloc(NULL, &mf->turnconnl,
			     turn_srv, IPPROTO_UDP, false,
			     username, password,
			     sock,
			     turnconn_estab_handler,
			     turnconn_error_handler, mf
			     );
	if (err) {
		warning("mediaflow: turnc_alloc failed (%m)\n", err);
		return err;
	}

	return 0;
}


/*
 * Add a new TURN-server and gather RELAY candidates (TCP or TLS)
 */
int mediaflow_gather_turn_tcp(struct mediaflow *mf, const struct sa *turn_srv,
			      const char *username, const char *password,
			      bool secure)
{
	struct turn_conn *tc;
	int err = 0;

	if (!mf || !turn_srv)
		return EINVAL;

	if (mf->nat != MEDIAFLOW_TRICKLEICE_DUALSTACK) {
		warning("gather_turn_tcp: only implemented for DS\n");
		return EINVAL;
	}

	err = turnconn_alloc(&tc, &mf->turnconnl,
			     turn_srv, IPPROTO_TCP, secure,
			     username, password,
			     NULL,
			     turnconn_estab_handler,
			     turnconn_error_handler, mf
			     );
	if (err)
		return err;

	return err;
}


/* Get the local port of the UDP/RTP-socket */
uint16_t mediaflow_lport(const struct mediaflow *mf)
{
	if (!mf)
		return 0;

	if (mf->trice) {

		if (mf->us_turn) {
			struct sa laddr;

			if (udp_local_get(mf->us_turn, &laddr))
				return 0;

			return sa_port(&laddr);
		}
		else if (mf->sel_lcand)
			return sa_port(&mf->sel_lcand->attr.addr);
		else {
			return 0;
		}
	}

	return sa_port(rtp_local(mf->rtp));
}


size_t mediaflow_remote_cand_count(const struct mediaflow *mf)
{
	if (!mf)
		return 0;

	switch (mf->nat) {

	case MEDIAFLOW_TRICKLEICE_DUALSTACK:
		return list_count(trice_rcandl(mf->trice));

	case MEDIAFLOW_ICELITE:
		return list_count(icelite_rcandl(mf->ice_lite));

	default:
		return 0;
	}
}


enum media_crypto mediaflow_crypto(const struct mediaflow *mf)
{
	return mf ? mf->crypto : CRYPTO_NONE;
}


struct auenc_state *mediaflow_encoder(const struct mediaflow *mf)
{
	return mf ? mf->aes : NULL;
}


struct audec_state *mediaflow_decoder(const struct mediaflow *mf)
{
	return mf ? mf->ads : NULL;
}


struct videnc_state *mediaflow_video_encoder(const struct mediaflow *mf)
{
	return mf ? mf->video.ves : NULL;
}


struct viddec_state *mediaflow_video_decoder(const struct mediaflow *mf)
{
	return mf ? mf->video.vds : NULL;
}


int mediaflow_debug(struct re_printf *pf, const struct mediaflow *mf)
{
	int err = 0;

	if (!mf)
		return 0;

	err = re_hprintf(pf, "%c%c%c%c ice=%s.%J [%s] tx=%llu rx=%llu",
			 mf->got_sdp ? 'S' : ' ',
			 mf->ice_ready ? 'I' : ' ',
			 mf->crypto_ready ? 'D' : ' ',
			 mediaflow_is_rtpstarted(mf) ? 'R' : ' ',
			 ice_cand_type2name(mf->rcand.type),
			 &mf->rcand.addr,
			 mf->peer_software,
			 mf->stat.tx.bytes,
			 mf->stat.rx.bytes);

	return err;
}


void mediaflow_set_rtpstate_handler(struct mediaflow *mf,
				      mediaflow_rtp_state_h *rtpstateh)
{
	if (!mf)
		return;

	mf->rtpstateh = rtpstateh;
}


const char *mediaflow_peer_software(const struct mediaflow *mf)
{
	return mf ? mf->peer_software : NULL;
}


bool mediaflow_has_video(const struct mediaflow *mf)
{
	return mf ? mf->video.has_media : false;
}


int mediaflow_video_debug(struct re_printf *pf, const struct mediaflow *mf)
{
	if (!mf)
		return 0;

	if (mf->video.vds) {
		const struct vidcodec *vc = viddec_get(mf->video.vds);

		if (vc->dec_debugh)
			return vc->dec_debugh(pf, mf->video.vds);
	}

	return 0;
}


const struct tls_conn *mediaflow_dtls_connection(const struct mediaflow *mf)
{
	return mf ? mf->tls_conn : NULL;
}


bool mediaflow_is_started(const struct mediaflow *mf)
{
	return mf ? mf->started : false;
}


void mediaflow_set_gather_handler(struct mediaflow *mf,
				  mediaflow_gather_h *gatherh)
{
	if (!mf)
		return;

	mf->gatherh = gatherh;
}


void mediaflow_set_video_handlers(struct mediaflow *mf,
				mediaflow_create_preview_h cpvh,
				mediaflow_release_preview_h rpvh,
				mediaflow_create_view_h cvh,
				mediaflow_release_view_h rvh,
				void *arg)
{
	mf->video.cpvh = cpvh;
	mf->video.rpvh = rpvh;
	mf->video.cvh = cvh;
	mf->video.rvh = rvh;
	mf->video.arg = arg;
}


void mediaflow_set_video_preview(struct mediaflow *mf,
				 void *view)
{
	const struct vidcodec *vc;

	if (mf->video.ves) {
		vc = videnc_get(mf->video.ves);
		vc->enc_previewh(mf->video.ves, view);
	}
}


void mediaflow_set_video_view(struct mediaflow *mf,
				 void *view)
{
	const struct vidcodec *vc;

	if (mf->video.vds) {
		vc = viddec_get(mf->video.vds);
		vc->dec_viewh(mf->video.vds, view);
	}
}


void mediaflow_set_video_capture_device(struct mediaflow *mf,
					const char *devid)
{
	const struct vidcodec *vc;

	if (mf->video.ves) {
		vc = videnc_get(mf->video.ves);
		vc->enc_deviceh(mf->video.ves, devid);
	}
}


bool mediaflow_got_sdp(const struct mediaflow *mf)
{
	return mf ? mf->got_sdp : false;
}


/*
 * return TRUE if one SDP sent AND one SDP received
 */
bool mediaflow_sdp_is_complete(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->got_sdp && mf->sent_sdp;
}


static bool are_all_turnconn_allocated(const struct mediaflow *mf)
{
	struct le *le;

	for (le = mf->turnconnl.head; le; le = le->next) {
		struct turn_conn *conn = le->data;

		if (!conn->turn_allocated)
			return false;
	}

	return true;
}


bool mediaflow_is_gathered(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	debug("mediaflow: is_gathered:  turnconnl=%u  stun=%d/%d\n",
	      list_count(&mf->turnconnl),
	      mf->stun_server, mf->stun_ok);

	if (!list_isempty(&mf->turnconnl))
		return are_all_turnconn_allocated(mf);

	if (mf->stun_server)
		return mf->stun_ok;

	return true;
}


uint32_t mediaflow_get_local_ssrc(struct mediaflow *mf, enum media_type type)
{
	if (!mf || type >= MEDIA_NUM)
		return 0;

	return mf->lssrcv[type];
}


int mediaflow_get_remote_ssrc(const struct mediaflow *mf, enum media_type type,
			      uint32_t *ssrcp)
{
	struct sdp_media *sdpm;
	const char *rssrc;
	struct pl pl_ssrc;
	int err;

	sdpm = type == MEDIA_AUDIO ? mf->sdpm : mf->video.sdpm;

	rssrc = sdp_media_rattr(sdpm, "ssrc");
	if (!rssrc)
		return ENOENT;

	err = re_regex(rssrc, str_len(rssrc), "[0-9]+", &pl_ssrc);
	if (err)
		return err;

	*ssrcp = pl_u32(&pl_ssrc);

	return 0;
}


bool mediaflow_dtls_ready(struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->crypto_ready;
}


bool mediaflow_ice_ready(struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->ice_ready;
}


void mediaflow_background(struct mediaflow *mf, enum media_bg_state bgst)
{
	if (!mf)
		return;

#if HAVE_VIDEO
	if (mf->video.ves) {
		const struct vidcodec *vc;
		
		vc = videnc_get(mf->video.ves);
		if (vc) {
			if (vc->enc_bgh)
				vc->enc_bgh(mf->video.ves, bgst);
		}		
	}
	if (mf->video.vds) {
		const struct vidcodec *vc;
		
		vc = viddec_get(mf->video.vds);
		if (vc) {
			if (vc->dec_bgh)
				vc->dec_bgh(mf->video.vds, bgst);
		}		
	}
#endif
}
