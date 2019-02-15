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

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <re.h>
#include <rew.h>
#include "avs_log.h"
#include "avs_version.h"
#include "avs_aucodec.h"
#include "avs_dce.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_turn.h"
#include "avs_extmap.h"
#include "avs_media.h"
#include "avs_vidcodec.h"
#include "avs_network.h"
#include "avs_kase.h"
#include "priv_mediaflow.h"
#include "avs_mediastats.h"
#include "avs_msystem.h"

#include <sodium.h>

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif


#define MAGIC 0xed1af100
#define MAGIC_CHECK(s) \
	if (MAGIC != s->magic) {                                        \
		warning("%s: wrong magic struct=%p (magic=0x%08x)\n",   \
			__REFUNC__, s, s->magic);			\
		BREAKPOINT;                                             \
	}


#define MAX_SRTP_KEY_SIZE (32+14)  /* Master key and salt */


enum {
	RTP_TIMEOUT_MS = 20000,
	RTP_FIRST_PKT_TIMEOUT_MS = 20000,
	RTP_RESTART_TIMEOUT_MS = 2000,
	DTLS_MTU       = 1480,
	SSRC_MAX       = 4,
	ICE_INTERVAL   = 50,    /* milliseconds */
	PORT_DISCARD   = 9,     /* draft-ietf-ice-trickle-05 */
	UDP_SOCKBUF_SIZE = 160*1024,  /* same as Android */
};

enum {
	AUDIO_BANDWIDTH = 50,   /* kilobits/second */
	VIDEO_BANDWIDTH = 800,  /* kilobits/second */
};

enum {
	GROUP_PTIME = 60
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

	const struct mediaflow *mf;     /* pointer to parent */
	const struct ice_lcand *lcand;  /* pointer */
	struct sa addr;
	char ifname[64];
	bool is_default;
};

struct auformat {
	struct sdp_format *fmt;
	const struct aucodec *ac;
	
	struct le le; /* Member of audio format list */
};

struct dtls_peer {
	struct le le;
	size_t headroom;
	struct sa addr;
};

struct mediaflow {

	struct mqueue *mq;

	/* common stuff */
	char *clientid_local;
	char *clientid_remote;
	char *userid_remote;
	struct sa laddr_default;
	char tag[32];
	bool terminated;
	int af;
	int err;

	/* RTP/RTCP */
	struct udp_sock *rtp;
	struct rtp_stats audio_stats_rcv;
	struct rtp_stats audio_stats_snd;
	struct rtp_stats video_stats_rcv;
	struct rtp_stats video_stats_snd;
	struct aucodec_stats codec_stats;

	struct tmr tmr_rtp;
	struct tmr tmr_got_rtp;
	uint32_t lssrcv[MEDIA_NUM];
	char cname[16];             /* common for audio+video */
	char msid[36];
	char *label;

	/* SDP */
	struct sdp_session *sdp;
	bool sdp_offerer;
	bool got_sdp;
	bool sent_sdp;
	enum sdp_state sdp_state;
	char sdp_rtool[64];
	struct extmap *extmap;

	/* ice: */
	struct trice *trice;
	struct stun *trice_stun;
	struct udp_helper *trice_uh;
	struct ice_candpair *sel_pair;    /* chosen candidate-pair */
	struct udp_sock *us_stun;
	struct list turnconnl;
	struct tmr tmr_error;

	char ice_ufrag[16];
	char ice_pwd[32];
	bool ice_ready;
	char *peer_software;
	uint64_t ts_nat_start;

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
	enum media_crypto crypto_fallback;
	struct udp_helper *uh_srtp;
	struct srtp *srtp_tx;
	struct srtp *srtp_rx;
	struct tls *dtls;
	struct dtls_sock *dtls_sock;
	struct udp_helper *dtls_uh;   /* for outgoing DTLS-packet */
	struct tls_conn *tls_conn;
	struct list dtls_peers;     /* list of DTLS-peers (struct dtls_peer) */
	enum media_setup setup_local;
	enum media_setup setup_remote;
	bool crypto_ready;
	bool crypto_verified;
	uint64_t ts_dtls;
	struct kase *kase;
	enum srtp_suite srtp_suite;

	/* Codec handling */
	struct media_ctx *mctx;
	struct auenc_state *aes;
	struct audec_state *ads;
	pthread_mutex_t mutex_enc;  /* protect the encoder state */
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
		bool disabled;
	} video;

	/* Data */
	struct {
		struct sdp_media *sdpm;

		struct dce *dce;
		struct dce_channel *dce_ch;

		bool has_media;
		bool ready;
		uint64_t ts_connect;
	} data;

	/* Audio */
	struct {
		struct sdp_media *sdpm;
		struct list formatl;

		bool disabled;
		bool local_cbr;
		bool remote_cbr;
	} audio;
    
	/* User callbacks */
	mediaflow_estab_h *estabh;
	mediaflow_close_h *closeh;
	mediaflow_stopped_h *stoppedh;
	mediaflow_restart_h *restarth;
	mediaflow_rtp_state_h *rtpstateh;
	mediaflow_gather_h *gatherh;
	void *arg;

	struct {
		struct {
			uint64_t ts_first;
			uint64_t ts_last;
			size_t bytes;
		} tx, rx;

		size_t n_sdp_recv;
		size_t n_srtp_dropped;
		size_t n_srtp_error;
	} stat;

	bool sent_rtp;
	bool got_rtp;

	struct list interfacel;

	struct mediaflow_stats mf_stats;
	bool privacy_mode;
	bool group_mode;

	struct {
		void *arg;
	} extcodec;


	struct zapi_ice_server turnv[MAX_TURN_SERVERS];
	size_t turnc;

	
	/* magic number check at the end of the struct */
	uint32_t magic;	
};

struct lookup_entry {
	struct mediaflow *mf;
	struct zapi_ice_server turn;
	char *host;
	int port;
	int proto;
	bool secure;
	uint64_t ts;
	
	struct le le;
};


struct vid_ref {
	struct vidcodec *vc;
	struct mediaflow *mf;
};

/* Use standard logging */
#if 0
#undef debug
#undef info
#undef warning
#define debug(...)   mf_log(mf, LOG_LEVEL_DEBUG, __VA_ARGS__);
#define info(...)    mf_log(mf, LOG_LEVEL_INFO,  __VA_ARGS__);
#define warning(...) mf_log(mf, LOG_LEVEL_WARN,  __VA_ARGS__);
#endif


#if TARGET_OS_IPHONE
#undef OS
#define OS "ios"
#endif


/* 0.0.0.0 port 0 */
static const struct sa dummy_dtls_peer = {

	.u = {
		.in = {
			 .sin_family = AF_INET,
			 .sin_port = 0,
			 .sin_addr = {0}
		 }
	},

	.len = sizeof(struct sockaddr_in)

};


/* prototypes */
static int print_cand(struct re_printf *pf, const struct ice_cand_attr *cand);
static void add_turn_permission(struct mediaflow *mf,
				   struct turn_conn *conn,
				   const struct ice_cand_attr *rcand);
static void add_permission_to_remotes(struct mediaflow *mf);
static void add_permission_to_remotes_ds(struct mediaflow *mf,
					 struct turn_conn *conn);
static void external_rtp_recv(struct mediaflow *mf,
			      const struct sa *src, struct mbuf *mb);
static bool headroom_via_turn(size_t headroom);

#if 0
static void mf_log(const struct mediaflow *mf, enum log_level level,
		   const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);

	n = re_snprintf(buf, sizeof(buf), "[%p] ", mf);
	str_ncpy(&buf[n], fmt, strlen(fmt)+1);
	fmt = buf;

	vloglv(level, fmt, ap);
	va_end(ap);
}
#endif


static struct dtls_peer *dtls_peer_find(struct mediaflow *mf,
					size_t headroom, const struct sa *addr)
{
	struct le *le;

	for (le = list_head(&mf->dtls_peers); le; le = le->next) {
		struct dtls_peer *dtls_peer = le->data;
		const bool t1 = headroom_via_turn(headroom);
		const bool t2 = headroom_via_turn(dtls_peer->headroom);

		if (t1 == t2 &&
		    sa_cmp(addr, &dtls_peer->addr, SA_ALL))
			return dtls_peer;
	}

	return NULL;
}


static const char *crypto_name(enum media_crypto crypto)
{
	switch (crypto) {

	case CRYPTO_NONE:      return "None";
	case CRYPTO_DTLS_SRTP: return "DTLS-SRTP";
	case CRYPTO_KASE:      return "KASE";

	case CRYPTO_BOTH:      return "KASE + DTLS-SRTP";
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
	if (cryptos & CRYPTO_KASE) {
		err |= re_hprintf(pf, "%s ", crypto_name(CRYPTO_KASE));
	}

	return err;
}

const char *mediaflow_setup_name(enum media_setup setup)
{
	switch (setup) {

	case SETUP_ACTPASS: return "actpass";
	case SETUP_ACTIVE:  return "active";
	case SETUP_PASSIVE: return "passive";
	default: return "?";
	}
}


static enum media_setup setup_resolve(const char *name)
{
	if (0 == str_casecmp(name, "actpass")) return SETUP_ACTPASS;
	if (0 == str_casecmp(name, "active")) return SETUP_ACTIVE;
	if (0 == str_casecmp(name, "passive")) return SETUP_PASSIVE;

	return (enum media_setup)-1;
}


static const char *sock_prefix(size_t headroom)
{
	if (headroom >= 36) return "TURN-Ind";
	if (headroom >= 4) return "TURN-Chan";

	return "Socket";
}


static bool headroom_via_turn(size_t headroom)
{
	return headroom >= 4;
}


bool mediaflow_dtls_peer_isset(const struct mediaflow *mf)
{
	struct dtls_peer *dtls_peer;

	if (!mf)
		return false;

	dtls_peer = list_ledata(mf->dtls_peers.head);
	if (!dtls_peer)
		return false;

	return sa_isset(&dtls_peer->addr, SA_ALL);
}


static int dtls_peer_print(struct re_printf *pf,
			   const struct dtls_peer *dtls_peer)
{
	if (!dtls_peer)
		return 0;

	return re_hprintf(pf, "%s|%zu|%J",
			  sock_prefix(dtls_peer->headroom),
			  dtls_peer->headroom,
			  &dtls_peer->addr);
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

	if (!mf->sel_pair)
		return 0;

	if (mf->sel_pair->lcand->attr.type == ICE_CAND_TYPE_RELAY)
		return 36;
	else
		return 0;


	return headroom;
}


static void ice_error(struct mediaflow *mf, int err)
{
	warning("mediaflow(%p): error in ICE-transport (%m)\n", mf, err);

	mf->ice_ready = false;
	mf->err = err;

	list_flush(&mf->interfacel);

	list_flush(&mf->turnconnl);

	mf->trice_uh = mem_deref(mf->trice_uh);  /* note: destroy first */
	mf->sel_pair = mem_deref(mf->sel_pair);

	mf->terminated = true;

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


static void tmr_error_handler(void *arg)
{
	struct mediaflow *mf = arg;

	ice_error(mf, mf->err);
}


static void crypto_error(struct mediaflow *mf, int err)
{
	warning("mediaflow(%p): error in DTLS (%m)\n", mf, err);

	mf->crypto_ready = false;
	mf->err = err;
	mf->tls_conn = mem_deref(mf->tls_conn);

	mf->terminated = true;

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


bool mediaflow_is_ready(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	if (!mf->ice_ready)
		return false;

	if (mf->cryptos_local == CRYPTO_NONE)
		return true;

	if (mf->crypto == CRYPTO_NONE)
		return false;
	else
		return mf->crypto_ready;

	return true;
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


static void auenc_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	error("mediaflow(%p): auenc_error_handler: %s\n", mf, msg);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}
}


static void audec_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	error("mediaflow(%p): audec_error_handler: %s\n", mf, msg);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}
}


static int voenc_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;
	int err;

	if (!mf)
		return EINVAL;

	if (!mf->sent_rtp) {
		mqueue_push(mf->mq, MQ_RTP_START, NULL);
	}

	err = mediaflow_send_raw_rtp(mf, pkt, len);
	if (err == 0){
		mediastats_rtp_stats_update(&mf->audio_stats_snd, pkt, len, 0);
	}

	return err;
}


static int voenc_rtcp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;

	return mediaflow_send_raw_rtcp(mf, pkt, len);
}

static void regen_lssrc(struct mediaflow *mf)
{
	uint32_t lssrc;

	/* Generate a new local ssrc that is DIFFERENT,
	 * to what we already have...
	 */
	
	do {
		lssrc = rand_u32();
	}
	while(lssrc == mf->lssrcv[MEDIA_AUDIO]);

	mf->lssrcv[MEDIA_AUDIO] = lssrc;
}

/* XXX: Move to mediamanager */

static int start_codecs(struct mediaflow *mf)
{
	const struct aucodec *ac;
	const struct sdp_format *fmt;
	struct aucodec_param prm;
	const char *rssrc;
	int err = 0;

	pthread_mutex_lock(&mf->mutex_enc);

	fmt = sdp_media_rformat(mf->audio.sdpm, NULL);
	if (!fmt) {
		warning("mediaflow(%p): no common codec\n", mf);
		err = ENOENT;
		goto out;
	}

	ac = fmt->data;
	if (!ac) {
		warning("mediaflow(%p): no aucodec in sdp data\n", mf);
		err = EINVAL;
		goto out;
	}

	debug("mediaflow(%p): starting audio codecs (%s/%u/%d)\n",
	      mf, fmt->name, fmt->srate, fmt->ch);

	rssrc = sdp_media_rattr(mf->audio.sdpm, "ssrc");

	prm.local_ssrc = mf->lssrcv[MEDIA_AUDIO];
	prm.remote_ssrc = rssrc ? atoi(rssrc) : 0;
	prm.pt = fmt->pt;
	prm.srate = ac->srate;
	prm.ch = ac->ch;

	if (fmt->params) {
		if (0 == re_regex(fmt->params, strlen(fmt->params), "cbr=1")) {

			mf->audio.remote_cbr = true;

			info("mediaflow(%p): remote side asking for CBR\n", mf);
		}
	}
	if (mf->audio.remote_cbr) {
		/* Make sure to force CBR */
		mediaflow_set_audio_cbr(mf, true);
	}
	prm.cbr = mf->audio.local_cbr;

	if (ac->enc_alloc && !mf->aes) {
		err = ac->enc_alloc(&mf->aes, ac, NULL,
				    &prm,
				    voenc_rtp_handler,
				    voenc_rtcp_handler,
				    auenc_error_handler,
				    mf->extcodec.arg,
				    mf);
		if (err) {
			warning("mediaflow(%p): encoder failed (%m)\n",
				mf, err);
			goto out;
		}

		if (mf->started && ac->enc_start) {
			// SSJ does this ever happen ?
			error("mediaflow(%p): unexpected start of "
			      "audio encoder\n", mf); 

			ac->enc_start(mf->aes, mf->audio.local_cbr,
				      NULL, &mf->mctx);
		}        
	}
	mediastats_rtp_stats_init(&mf->audio_stats_snd, fmt->pt, 2000);

	if (ac->dec_alloc && !mf->ads){
		err = ac->dec_alloc(&mf->ads, ac, NULL,
				    &prm,
				    audec_error_handler,
				    mf->extcodec.arg,
				    mf);
		if (err) {
			warning("mediaflow(%p): decoder failed (%m)\n",
				mf, err);
			goto out;
		}

		if (mf->started && ac->dec_start) {
			// SSJ does this ever happen?			
			error("mediaflow(%p): unexpected start of "
			      "audio decoder\n", mf); 
			ac->dec_start(mf->ads, &mf->mctx);
		}
	}
	mediastats_rtp_stats_init(&mf->audio_stats_rcv, fmt->pt, 2000);

 out:
	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}


static int videnc_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;

	int err = mediaflow_send_raw_rtp(mf, pkt, len);
	if (err == 0) {
		uint32_t bwalloc = 0;
		const struct vidcodec *vc = videnc_get(mf->video.ves);
		if (vc && vc->enc_bwalloch) {
			bwalloc = vc->enc_bwalloch(mf->video.ves);
		}
		mediastats_rtp_stats_update(&mf->video_stats_snd,
					    pkt, len, bwalloc);
	}

	return err;
}


static int videnc_rtcp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;

	return mediaflow_send_raw_rtcp(mf, pkt, len);
}


static void vidcodec_error_handler(int err, const char *msg, void *arg)
{
	struct mediaflow *mf = arg;

	warning("mediaflow(%p): video-codec error '%s' (%m)\n", mf, msg, err);

	mf->err = err;
	if (mf->closeh) {
		mf->closeh(err, mf->arg);
	}

	// TODO: should we also close video-states and ICE+DTLS ?
}


static void update_ssrc_array( uint32_t array[], size_t *count, uint32_t val)
{
	size_t i;

	for (i = 0; i < *count; i++) {
		if (val == array[i]){
			break;
		}
	}

	if ( i == *count) {
		array[*count] = val;
		(*count)++;
	}
}


static bool rssrc_handler(const char *name, const char *value, void *arg)
{
	struct vidcodec_param *prm = arg;
	struct pl pl;
	uint32_t ssrc;
	int err;

	if (prm->remote_ssrcc >= ARRAY_SIZE(prm->remote_ssrcv))
		return true;

	err = re_regex(value, strlen(value), "[0-9]+", &pl);
	if (err)
		return false;

	ssrc = pl_u32(&pl);

	update_ssrc_array( prm->remote_ssrcv, &prm->remote_ssrcc, ssrc);

	return false;
}


static const uint8_t app_label[4] = "DATA";


static int send_rtcp_app(struct mediaflow *mf, const uint8_t *pkt, size_t len)
{
	struct mbuf *mb = mbuf_alloc(256);
	int err;

	err = rtcp_encode(mb, RTCP_APP, 0, (uint32_t)0, app_label, pkt, len);
	if (err) {
		warning("mediaflow(%p): rtcp_encode failed (%m)\n", mf, err);
		goto out;
	}

	err = mediaflow_send_raw_rtcp(mf, mb->buf, mb->end);
	if (err) {
		warning("mediaflow(%p): send_raw_rtcp failed (%m)\n", mf, err);
	}

 out:
	mem_deref(mb);
	return err;
}


static int dce_send_data_handler(uint8_t *pkt, size_t len, void *arg)
{
	struct mediaflow *mf = arg;
	struct mbuf mb;
	int err = 0;

	if (!mediaflow_is_ready(mf)) {
		warning("mediaflow(%p): send_data(%zu bytes): not ready"
			" [ice=%d, crypto=%d]\n",
			mf, len, mf->ice_ready, mf->crypto_ready);
		return EINTR;
	}

	mb.buf = pkt;
	mb.pos = 0;
	mb.size = len;
	mb.end = len;

	info("mediaflow(%p): sending DCE packet: %zu\n",
	     mf, mbuf_get_left(&mb));

	switch (mf->crypto) {

	case CRYPTO_DTLS_SRTP:
		if (mf->tls_conn) {
			err = dtls_send(mf->tls_conn, &mb);
		}
		else {
			warning("mediaflow(%p): dce_send_data:"
				" no DTLS connection\n", mf);
			return ENOENT;
		}
		break;

	case CRYPTO_KASE:
		err = send_rtcp_app(mf, pkt, len);
		if (err) {
			warning("mediaflow(%p): dce_send_data:"
				" rtcp_send_app [%zu bytes] (%m)\n",
				mf, len, err);
		}
		break;

	default:
		warning("mediaflow(%p): dce_send_data: invalid crypto %d\n",
			mf, mf->crypto);
		return EPROTO;
	};

	return err;
}


static void dce_estab_handler(void *arg)
{
	struct mediaflow *mf = arg;

	mf->mf_stats.dce_estab = tmr_jiffies() - mf->data.ts_connect;

	info("mediaflow(%p): dce established (%d ms)\n",
	     mf, mf->mf_stats.dce_estab);

	mf->data.ready = true;
}


static int start_video_codecs(struct mediaflow *mf)
{
	const struct vidcodec *vc;
	const struct sdp_format *fmt;
	struct vidcodec_param prm;
	struct vid_ref *vr;
	int err = 0;

	fmt = sdp_media_rformat(mf->video.sdpm, NULL);
	if (!fmt) {
		warning("mediaflow(%p): no common video-codec\n", mf);
		err = ENOENT;
		goto out;
	}

	vr = fmt->data;
	vc = vr->vc;
	if (!vc) {
		warning("mediaflow(%p): no vidcodec in sdp data\n", mf);
		err = EINVAL;
		goto out;
	}

	/* Local SSRCs */
	memcpy(prm.local_ssrcv, &mf->lssrcv[1], sizeof(prm.local_ssrcv));
	prm.local_ssrcc = 2;

	/* Remote SSRCs */
	prm.remote_ssrcc = 0;
	if (sdp_media_rattr_apply(mf->video.sdpm, "ssrc",
				  rssrc_handler, &prm)) {
		warning("mediaflow(%p): too many remote SSRCs\n", mf);
	}

	debug("mediaflow(%p): starting video codecs (%s/%u/%d)"
	      " [params=%s, rparams=%s]\n",
	      mf, fmt->name, fmt->srate, fmt->ch, fmt->params, fmt->rparams);

	if (vc->enc_alloch && !mf->video.ves) {

		err = vc->enc_alloch(&mf->video.ves, &mf->video.mctx, vc,
				     fmt->rparams, fmt->pt,
				     mf->video.sdpm, &prm,
				     videnc_rtp_handler,
				     videnc_rtcp_handler,
				     vidcodec_error_handler,
				     mf->extcodec.arg,
				     mf);
		if (err) {
			warning("mediaflow(%p): video encoder failed (%m)\n",
				mf, err);
			goto out;
		}

		if (mf->started && vc->enc_starth) {
			err = vc->enc_starth(mf->video.ves, mf->group_mode);
			if (err) {
				warning("mediaflow(%p): could not start"
					" video encoder (%m)\n", mf, err);
				goto out;
			}
		}
	}
	mediastats_rtp_stats_init(&mf->video_stats_snd, fmt->pt, 10000);

	if (vc->dec_alloch && !mf->video.vds){
		err = vc->dec_alloch(&mf->video.vds, &mf->video.mctx, vc,
				     fmt->params, fmt->pt,
				     mf->video.sdpm, &prm,
				     vidcodec_error_handler,
				     mf->extcodec.arg,
				     mf);
		if (err) {
			warning("mediaflow(%p): video decoder failed (%m)\n",
				mf, err);
			goto out;
		}

		if (mf->started && vc->dec_starth) {
			err = vc->dec_starth(mf->video.vds, mf->userid_remote);
			if (err) {
				warning("mediaflow(%p): could not start"
					" video decoder (%m)\n", mf, err);
				return err;
			}
		}
	}

	mediastats_rtp_stats_init(&mf->video_stats_rcv, fmt->pt, 10000);

 out:
	return err;
}


static void timeout_rtp(void *arg)
{
	struct mediaflow *mf = arg;

	tmr_start(&mf->tmr_rtp, 2000, timeout_rtp, mf);

	if (mediaflow_is_rtpstarted(mf)) {

		int diff = tmr_jiffies() - mf->stat.rx.ts_last;

		if (diff > RTP_TIMEOUT_MS) {

			warning("mediaflow(%p): no RTP packets recvd for"
				" %d ms -- stop\n",
				mf, diff);

			mf->terminated = true;
			mf->ice_ready = false;

			if (mf->closeh)
				mf->closeh(ETIMEDOUT, mf->arg);
		}
		else if (diff > RTP_RESTART_TIMEOUT_MS) {
			if (mf->restarth) {
				mf->restarth(mf->arg);
			}
		}
	}
}


static void tmr_got_rtp_handler(void *arg)
{
	struct mediaflow *mf = arg;
	
	if (mf->closeh)
		mf->closeh(ETIMEDOUT, mf->arg);
}


/* this function is only called once */
static void mediaflow_established_handler(struct mediaflow *mf)
{
	if (mf->terminated)
		return;
	if (!mediaflow_is_ready(mf))
		return;

	info("mediaflow(%p): ICE+DTLS established\n", mf);

	if (!tmr_isrunning(&mf->tmr_rtp))
		tmr_start(&mf->tmr_rtp, 1000, timeout_rtp, mf);

	if (mf->estabh) {
		const struct sdp_format *fmt;

		fmt = sdp_media_rformat(mf->audio.sdpm, NULL);

		mf->estabh(crypto_name(mf->crypto),
			   fmt ? fmt->name : "?",
			   mf->arg);
	}
}


static bool udp_helper_send_handler_srtp(int *err, struct sa *dst,
					 struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	(void)dst;

	if (packet_is_rtp_or_rtcp(mb) && mf->srtp_tx) {

		if (packet_is_rtcp_packet(mb)) {

			/* drop short RTCP packets */
			if (mbuf_get_left(mb) <= 8)
				return true;

			*err = srtcp_encrypt(mf->srtp_tx, mb);
			if (*err) {
				warning("mediaflow(%p): srtcp_encrypt() failed"
					"(%m)\n",
					mf, *err);
			}
		}
		else {
			*err = srtp_encrypt(mf->srtp_tx, mb);
			if (*err) {
				warning("mediaflow(%p): "
					"srtp_encrypt() [%zu bytes] "
					"failed (%m)\n",
					mf, mbuf_get_left(mb), *err);
			}
		}
	}

	return false;
}


static int send_packet(struct mediaflow *mf, size_t headroom,
		       const struct sa *raddr, struct mbuf *mb_pkt,
		       enum packet pkt)
{
	struct mbuf *mb = NULL;
	size_t len = mbuf_get_left(mb_pkt);
	int err = 0;

	if (!mf)
		return EINVAL;

	info("mediaflow(%p): send_packet `%s' (%zu bytes) via %s to %J\n",
	     mf,
	     packet_classify_name(pkt),
	     mbuf_get_left(mb_pkt),
	     sock_prefix(headroom), raddr);

	mb = mbuf_alloc(headroom + len);
	if (!mb)
		return ENOMEM;

	mb->pos = headroom;
	mbuf_write_mem(mb, mbuf_buf(mb_pkt), len);
	mb->pos = headroom;

	/* now invalid */
	mb_pkt = NULL;

	if (mf->ice_ready && mf->sel_pair) {

		struct ice_lcand *lcand = NULL;
		void *sock;

		sock = trice_lcand_sock(mf->trice, mf->sel_pair->lcand);
		if (!sock) {
			warning("mediaflow(%p): send: selected lcand %p"
				" has no sock [%H]\n",
				mf,
				mf->sel_pair->lcand,
				trice_cand_print, mf->sel_pair->lcand);
			err = ENOTCONN;
			goto out;
		}

		if (AF_INET6 == sa_af(raddr)) {
			lcand = trice_lcand_find2(mf->trice,
						  ICE_CAND_TYPE_HOST,
						  AF_INET6);
			if (lcand) {
				info("mediaflow(%p): send_packet: \n",
				     " using local IPv6 socket\n", mf);
				sock = lcand->us;
			}
		}

		debug("mediaflow(%p): send helper: udp_send: "
		      "sock=%p raddr=%p mb=%p\n", sock, raddr, mb);
		err = udp_send(sock, raddr, mb);
		if (err) {
			warning("mediaflow(%p): send helper error"
				" raddr=%J (%m)\n",
				mf, raddr, err);
		}
	}
	else {
		warning("mediaflow(%p): send_packet: "
			"drop %zu bytes (ICE not ready)\n",
			mf, len);
	}

 out:
	mem_deref(mb);

	return err;
}


/* ONLY for outgoing DTLS packets! */
static bool send_dtls_handler(int *err, struct sa *dst_unused,
			      struct mbuf *mb_pkt, void *arg)
{
	struct mediaflow *mf = arg;
	const enum packet pkt = packet_classify_packet_type(mb_pkt);
	const size_t start = mb_pkt->pos;
	struct le *le;
	int rc;
	bool success = false;

	if (pkt != PACKET_DTLS) {
		warning("mediaflow(%p): send_dtls: not a DTLS packet?\n", mf);
		return false;
	}

	++mf->mf_stats.dtls_pkt_sent;

	/*
	 * Send packet to all DTLS peers for better robustness
	 */
	for (le = mf->dtls_peers.head; le; le = le->next) {
		struct dtls_peer *dtls_peer = le->data;

		mb_pkt->pos = start;

		info("mediaflow(%p): dtls_helper: send DTLS packet #%u"
		     " to %H (%zu bytes)"
		     " \n",
		     mf,
		     mf->mf_stats.dtls_pkt_sent,
		     dtls_peer_print, dtls_peer,
		     mbuf_get_left(mb_pkt));

		rc = send_packet(mf, dtls_peer->headroom,
				 &dtls_peer->addr, mb_pkt, pkt);
		if (!rc)
			success = true;
		else {
			*err = rc;
			warning("mediaflow(%p): send_dtls_handler:"
				" send_packet failed (%m)\n", mf, rc);
		}
	}

	if (success)
		*err = 0;

	return true;
}


/* For Dual-stack only */
static bool udp_helper_send_handler_trice(int *err, struct sa *dst,
					 struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	enum packet pkt;
	int lerr;
	(void)dst;

	pkt = packet_classify_packet_type(mb);
	if (pkt == PACKET_DTLS) {
		warning("mediaflow(%p): dont use this to send DTLS packets\n",
			mf);
	}

	if (pkt == PACKET_STUN)
		return false;    /* continue */

	if (mf->ice_ready && mf->sel_pair) {

		void *sock;

		sock = trice_lcand_sock(mf->trice, mf->sel_pair->lcand);
		if (!sock) {
			warning("mediaflow(%p): send: selected lcand %p "
				"has no sock [%H]\n",
				mf, mf->sel_pair,
				trice_cand_print, mf->sel_pair->lcand);
		}

		lerr = udp_send(sock, &mf->sel_pair->rcand->attr.addr, mb);
		if (lerr) {
			warning("mediaflow(%p): helper: udp_send failed"
				" rcand=[%H] (%m)\n",
				mf, trice_cand_print, mf->sel_pair->rcand,
				lerr);
		}
	}
	else {
		warning("mediaflow(%p): helper: cannot send"
			" %zu bytes to %J, ICE not ready! (packet=%s)\n",
			mf, mbuf_get_left(mb), dst,
			packet_classify_name(pkt));
		*err = ENOTCONN;
	}

	return true;
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

	if (0 == pl_strcasecmp(&hash, "sha-256")) {
		type = TLS_FINGERPRINT_SHA256;
		sz_dtls = 32;
	}
	else {
		warning("mediaflow(%p): dtls_srtp: unknown fingerprint"
			" '%r'\n", mf, &hash);
		return false;
	}

	err = tls_peer_fingerprint(tc, type, md_dtls, sizeof(md_dtls));
	if (err) {
		warning("mediaflow(%p): dtls_srtp: could not get"
			" DTLS fingerprint (%m)\n", mf, err);
		return false;
	}

	if (sz_sdp != sz_dtls || 0 != memcmp(md_sdp, md_dtls, sz_sdp)) {
		warning("mediaflow(%p): dtls_srtp: %r fingerprint mismatch\n",
			mf, &hash);
		info("  SDP:  %w\n", md_sdp, sz_sdp);
		info("  DTLS: %w\n", md_dtls, sz_dtls);
		return false;
	}

	info("mediaflow(%p): dtls_srtp: verified %r fingerprint OK\n",
	     mf, &hash);

	return true;
}


static int check_data_channel(struct mediaflow *mf)
{
	bool has_data = mediaflow_has_data(mf);
	int err = 0;

	info("mediaflow(%p): dtls_estab_handler: has_data=%d active=%d\n",
	     mf, has_data, mf->setup_local == SETUP_ACTIVE);

	if (has_data) {
		info("mediaflow(%p): dce: connecting.. (%p)\n",
		     mf, mf->data.dce);

		mf->data.ts_connect = tmr_jiffies();

		err = dce_connect(mf->data.dce,
				  mf->setup_local == SETUP_ACTIVE);
		if (err) {
			warning("mediaflow(%p): dce_connect failed (%m)\n",
				mf, err);
			return err;
		}
	}

	return err;
}


static size_t get_keylen(enum srtp_suite suite)
{
	switch (suite) {

	case SRTP_AES_CM_128_HMAC_SHA1_32: return 16;
	case SRTP_AES_CM_128_HMAC_SHA1_80: return 16;
	case SRTP_AES_256_CM_HMAC_SHA1_32: return 32;
	case SRTP_AES_256_CM_HMAC_SHA1_80: return 32;
	case SRTP_AES_128_GCM:             return 16;
	case SRTP_AES_256_GCM:             return 32;
	default: return 0;
	}
}


static size_t get_saltlen(enum srtp_suite suite)
{
	switch (suite) {

	case SRTP_AES_CM_128_HMAC_SHA1_32: return 14;
	case SRTP_AES_CM_128_HMAC_SHA1_80: return 14;
	case SRTP_AES_256_CM_HMAC_SHA1_32: return 14;
	case SRTP_AES_256_CM_HMAC_SHA1_80: return 14;
	case SRTP_AES_128_GCM:             return 12;
	case SRTP_AES_256_GCM:             return 12;
	default: return 0;
	}
}


static void dtls_estab_handler(void *arg)
{
	struct mediaflow *mf = arg;
	enum srtp_suite suite;
	uint8_t cli_key[MAX_SRTP_KEY_SIZE], srv_key[MAX_SRTP_KEY_SIZE];
	size_t master_key_len;
	int err;

	if (mf->mf_stats.dtls_estab < 0 && mf->ts_dtls)
		mf->mf_stats.dtls_estab = tmr_jiffies() - mf->ts_dtls;

	info("mediaflow(%p): DTLS established (%d ms)\n",
	     mf, mf->mf_stats.dtls_estab);

	info("           cipher %s\n",
	     tls_cipher_name(mf->tls_conn));

	if (mf->got_sdp) {
		if (!verify_fingerprint(mf, mf->sdp, mf->audio.sdpm, mf->tls_conn)) {
			warning("mediaflow(%p): dtls_srtp: could not verify"
				" remote fingerprint\n", mf);
			err = EAUTH;
			goto error;
		}
		mf->crypto_verified = true;
	}

	err = tls_srtp_keyinfo(mf->tls_conn, &suite,
			       cli_key, sizeof(cli_key),
			       srv_key, sizeof(srv_key));
	if (err) {
		warning("mediaflow(%p): dtls: no SRTP keyinfo (%m)\n",
			mf, err);
		goto error;
	}

	mf->srtp_suite = suite;

	master_key_len = get_keylen(suite) + get_saltlen(suite);

	info("mediaflow(%p): DTLS established (suite=%s, master_key=%zu)\n",
	     mf, srtp_suite_name(suite), master_key_len);

	if (master_key_len == 0) {
		warning("mediaflow(%p): dtls: empty master key\n", mf);
	}

	if (master_key_len == 0) {
		warning("mediaflow: dtls: empty master key\n");
	}

	mf->srtp_tx = mem_deref(mf->srtp_tx);
	err = srtp_alloc(&mf->srtp_tx, suite,
			 mf->setup_local == SETUP_ACTIVE ? cli_key : srv_key,
			 master_key_len, 0);
	if (err) {
		warning("mediaflow(%p): dtls: failed to allocate SRTP for TX"
			" (%m)\n",
			mf, err);
		goto error;
	}

	err = srtp_alloc(&mf->srtp_rx, suite,
			 mf->setup_local == SETUP_ACTIVE ? srv_key : cli_key,
			 master_key_len, 0);
	if (err) {
		warning("mediaflow(%p): dtls: failed to allocate SRTP for RX"
			" (%m)\n",
			mf, err);
		goto error;
	}

	mf->crypto_ready = true;

	mediaflow_established_handler(mf);

	check_data_channel(mf);

	/* Wipe the keys from memory */
	memset(cli_key, 0, sizeof(cli_key));
	memset(srv_key, 0, sizeof(srv_key));

	return;

 error:
	warning("mediaflow(%p): DTLS-SRTP error (%m)\n", mf, err);

	/* Wipe the keys from memory */
	memset(cli_key, 0, sizeof(cli_key));
	memset(srv_key, 0, sizeof(srv_key));

	if (mf->closeh)
		mf->closeh(err, mf->arg);
}


static void dtls_recv_handler(struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;

	info("mediaflow(%p): dtls_recv_handler: %zu bytes\n",
	     mf, mbuf_get_left(mb));

	if (mf->data.dce)
		dce_recv_pkt(mf->data.dce, mbuf_buf(mb), mbuf_get_left(mb));
}


static void dtls_close_handler(int err, void *arg)
{
	struct mediaflow *mf = arg;

	MAGIC_CHECK(mf);

	info("mediaflow(%p): dtls-connection closed (%m)\n", mf, err);

	mf->tls_conn = mem_deref(mf->tls_conn);
	mf->err = err;

	if (!mf->crypto_ready) {

		if (mf->closeh)
			mf->closeh(err, mf->arg);
	}
}


/*
 * called ONCE when we receive DTLS Client Hello from the peer
 *
 * this function is only called when the ICE-layer is established
 */
static void dtls_conn_handler(const struct sa *unused_peer, void *arg)
{
	struct mediaflow *mf = arg;
	bool okay;
	int err;

	info("mediaflow(%p): incoming DTLS connect\n", mf);

	/* NOTE: The DTLS peer should be set in handle_dtls_packet */
	if (!mediaflow_dtls_peer_isset(mf)) {
		warning("mediaflow(%p): dtls_conn_handler:"
			" DTLS peer is not set\n", mf);
	}

	/* peer is a dummy address, must not be set/used */
	if (sa_in(unused_peer) || sa_port(unused_peer)) {
		warning("mediaflow(%p): internal error, unused peer (%J)\n",
			mf, unused_peer);
	}

	if (mf->setup_local == SETUP_ACTPASS) {
		info("mediaflow(%p): dtls_conn: local setup not decided yet"
		     ", drop packet\n",
		     mf);
		return;
	}

	if (mf->ice_ready) {

		okay = 1;
	}
	else {
		okay = 0;
	}

	if (!okay) {
		warning("mediaflow(%p): ICE is not ready. "
			"cannot accept DTLS\n", mf);
		return;
	}

	mf->ts_dtls = tmr_jiffies();

	if (mf->tls_conn) {
		warning("mediaflow(%p): DTLS already accepted\n", mf);
		return;
	}

	err = dtls_accept(&mf->tls_conn, mf->dtls, mf->dtls_sock,
			  dtls_estab_handler, dtls_recv_handler,
			  dtls_close_handler, mf);
	if (err) {
		warning("mediaflow(%p): dtls_accept failed (%m)\n", mf, err);
		goto error;
	}

	info("mediaflow(%p): dtls accepted\n", mf);

	return;

 error:
	crypto_error(mf, err);
}


static void dtls_peer_destructor(void *data)
{
	struct dtls_peer *dtls_peer = data;

	list_unlink(&dtls_peer->le);
}


static int add_dtls_peer(struct mediaflow *mf, size_t headroom,
			 const struct sa *peer)
{
	struct dtls_peer *dtls_peer;

	if (!mf || !peer)
		return EINVAL;

	info("mediaflow(%p): add_dtls_peer:"
	     " headroom=%zu, peer=%J\n", mf, headroom, peer);

	dtls_peer = dtls_peer_find(mf, headroom, peer);
	if (dtls_peer) {
		warning("mediaflow(%p): find: dtls peer already exist (%H)\n",
			mf, dtls_peer_print, dtls_peer);
		return EALREADY;
	}

	dtls_peer = mem_zalloc(sizeof(*dtls_peer), dtls_peer_destructor);
	if (!dtls_peer)
		return ENOMEM;

	dtls_peer->headroom = headroom;
	dtls_peer->addr = *peer;

	list_append(&mf->dtls_peers, &dtls_peer->le, dtls_peer);

	return 0;
}


static int start_crypto(struct mediaflow *mf, const struct sa *peer)
{
	int err = 0;

	if (mf->crypto_ready) {
		info("mediaflow(%p): ice-estab: crypto already ready\n", mf);
		return 0;
	}

	switch (mf->crypto) {

	case CRYPTO_NONE:
		/* Do nothing */
		break;

	case CRYPTO_DTLS_SRTP:

		if (mf->setup_local == SETUP_ACTIVE) {

			size_t headroom = 0;

			if (mf->tls_conn) {
				info("mediaflow(%p): dtls_connect,"
				     " already connecting ..\n", mf);
				goto out;
			}

			/* NOTE: must be done before dtls_connect() */
			headroom = get_headroom(mf);

			info("mediaflow(%p): dtls connect via %s to peer %J\n",
			     mf, sock_prefix(headroom), peer);

			mf->ts_dtls = tmr_jiffies();

			if (!dtls_peer_find(mf, headroom, peer)) {

				err = add_dtls_peer(mf, headroom, peer);
				if (err) {
					warning("mediaflow(%p): start_crypto:"
						" could not add dtls peer"
						" (%m)\n", mf, err);
					return err;
				}
			}

			err = dtls_connect(&mf->tls_conn, mf->dtls,
					   mf->dtls_sock, &dummy_dtls_peer,
					   dtls_estab_handler,
					   dtls_recv_handler,
					   dtls_close_handler, mf);
			if (err) {
				warning("mediaflow(%p): dtls_connect()"
					" failed (%m)\n", mf, err);
				return err;
			}
		}
		break;

	case CRYPTO_KASE:
		mf->crypto_ready = true;

		check_data_channel(mf);
		break;

	default:
		warning("mediaflow(%p): established: "
			"unknown crypto '%s' (%d)\n",
			mf, crypto_name(mf->crypto), mf->crypto);
		break;
	}

 out:
	return err;
}


/* this function is only called once */
static void ice_established_handler(struct mediaflow *mf,
				    const struct sa *peer)
{
	int err;

	info("mediaflow(%p): ICE-transport established [got_sdp=%d]"
	     " (peer = %s.%J)\n",
	     mf,
	     mf->got_sdp,
	     mf->sel_pair
	      ? ice_cand_type2name(mf->sel_pair->rcand->attr.type)
	      : "?",
	     peer);

	if (mf->mf_stats.nat_estab < 0 && mf->ts_nat_start) {
		mf->mf_stats.nat_estab = tmr_jiffies() - mf->ts_nat_start;
	}

	if (!dtls_peer_find(mf, get_headroom(mf), peer)) {
		err = add_dtls_peer(mf, get_headroom(mf), peer);
		if (err) {
			warning("mediaflow(%p): ice_estab:"
				" could not add dtls peer"
				" (%m)\n", mf, err);

			crypto_error(mf, err);
			return;
		}
	}

	if (mf->crypto_ready) {
		info("mediaflow(%p): ice-estab: crypto already ready\n", mf);
		goto out;
	}

	err = start_crypto(mf, peer);
	if (err) {
		crypto_error(mf, err);
	}

 out:
	mediaflow_established_handler(mf);
}


static int handle_kase(struct mediaflow *mf)
{
	const char *rkase;
	uint8_t pubkey[256] = "";
	size_t pubkey_len = sizeof(pubkey);
	uint8_t session_tx[KASE_SESSIONKEY_SIZE];
	uint8_t session_rx[KASE_SESSIONKEY_SIZE];
	bool is_client;
	int err;

	info("mediaflow(%p): using crypto KASE\n", mf);

	switch (mf->setup_local) {

	case SETUP_ACTIVE:
		is_client = true;
		break;

	case SETUP_PASSIVE:
		is_client = false;
		break;

	default:
		warning("mediaflow(%p): kase: local setup not decided"
			" -- abort\n", mf);
		return EPROTO;
	}

	rkase = sdp_media_rattr(mf->audio.sdpm, "x-KASEv1");
	if (!rkase) {
		warning("mediaflow(%p): kase: missing sdp attribute\n", mf);
		return ENOENT;
	}

	err = base64_decode(rkase, str_len(rkase), pubkey, &pubkey_len);
	if (err) {
		warning("mediaflow(%p): kase: base64 decode failed (%m)\n",
			mf, err);
		return err;
	}

	err = kase_get_sessionkeys(session_tx, session_rx, mf->kase, pubkey,
				   is_client,
				   mf->clientid_local,
				   mf->clientid_remote);
	if (err) {
		warning("mediaflow(%p): kase: [%s] could not get"
			" session keys (%m)\n",
			mf, is_client ? "Client" : "Server", err);
		return err;
	}

	err = srtp_alloc(&mf->srtp_tx, SRTP_AES_CM_128_HMAC_SHA1_80,
			 session_tx, 30, 0);
	if (err) {
		warning("mediaflow(%p): kase: failed to allocate SRTP for TX"
			" (%m)\n",
			mf, err);
		goto out;
	}

	err = srtp_alloc(&mf->srtp_rx, SRTP_AES_CM_128_HMAC_SHA1_80,
			 session_rx, 30, 0);
	if (err) {
		warning("mediaflow(%p): kase: failed to allocate SRTP for RX"
			" (%m)\n",
			mf, err);
		goto out;
	}

	mf->srtp_suite = SRTP_AES_CM_128_HMAC_SHA1_80;

	info("mediaflow(%p): KASE established\n", mf);

 out:
	sodium_memzero(session_tx, sizeof(session_tx));
	sodium_memzero(session_rx, sizeof(session_rx));

	return err;
}


static void handle_dtls_packet(struct mediaflow *mf, const struct sa *src,
			       struct mbuf *mb)
{
	size_t headroom = mb->pos;
	struct dtls_peer *dtls_peer;
	int err;

	++mf->mf_stats.dtls_pkt_recv;

	info("mediaflow(%p): dtls: recv %zu bytes from %s|%J\n",
	     mf, mbuf_get_left(mb), sock_prefix(mb->pos), src);

	if (!mf->got_sdp) {

		info("mediaflow(%p): SDP is not ready --"
		     " drop DTLS packet from %J\n",
		     mf, src);
		return;
	}

	if (!mediaflow_ice_ready(mf)) {

		info("mediaflow(%p): ICE is not ready (checklist-%s) --"
		     " drop DTLS packet from %J\n",
		     mf,
		     trice_checklist_isrunning(mf->trice) ? "Running"
		                                          : "Not-Running",
		     src);
		return;
	}

	if (!mediaflow_dtls_peer_isset(mf)) {
		info("mediaflow(%p): DTLS peer is not set --"
		     " drop DTLS packet from %J\n", mf, src);
		return;
	}

	dtls_peer = dtls_peer_find(mf, headroom, src);
	if (!dtls_peer) {

		info("mediaflow(%p): packet: dtls_peer not found"
		     " -- adding (%s|%zu|%J)\n",
		     mf, sock_prefix(headroom), headroom, src);

		err = add_dtls_peer(mf, headroom, src);
		if (err) {
			warning("mediaflow(%p): packet:"
				" could not add dtls peer"
				" (%m)\n", mf, err);
		}
	}

	dtls_recv_packet(mf->dtls_sock, &dummy_dtls_peer, mb);
}


static bool udp_helper_recv_handler_srtp(struct sa *src, struct mbuf *mb,
					 void *arg)
{
	struct mediaflow *mf = arg;
	size_t len = mbuf_get_left(mb);
	const enum packet pkt = packet_classify_packet_type(mb);
	int err;

	if (pkt == PACKET_DTLS) {
		handle_dtls_packet(mf, src, mb);
		return true;
	}

	if (packet_is_rtp_or_rtcp(mb)) {

		/* the SRTP is not ready yet .. */
		if (!mf->srtp_rx) {
			mf->stat.n_srtp_dropped++;
			goto next;
		}

		if (packet_is_rtcp_packet(mb)) {

			err = srtcp_decrypt(mf->srtp_rx, mb);
			if (err) {
				mf->stat.n_srtp_error++;
				warning("mediaflow(%p): srtcp_decrypt failed"
					" [%zu bytes] (%m)\n", mf, len, err);
				return true;
			}
		}
		else {
			err = srtp_decrypt(mf->srtp_rx, mb);
			if (err) {
				mf->stat.n_srtp_error++;
				if (err != EALREADY) {
					warning("mediaflow(%p): srtp_decrypt"
						" failed"
						" [%zu bytes from %J] (%m)\n",
						mf, len, src, err);
				}
				return true;
			}
		}

		if (packet_is_rtcp_packet(mb)) {

			struct rtcp_msg *msg = NULL;
			size_t pos = mb->pos;
			bool is_app = false;
			int r;

			r = rtcp_decode(&msg, mb);
			if (r) {
				warning("mediaflow(%p): failed to decode"
					" incoming RTCP"
					" packet (%m)\n", mf, r);
				goto done;
			}
			mb->pos = pos;

			if (msg->hdr.pt == RTCP_APP) {

				if (0 != memcmp(msg->r.app.name,
						app_label, 4)) {

					warning("mediaflow(%p): "
						"invalid app name '%b'\n",
						mf, msg->r.app.name, (size_t)4);
					goto done;
				}

				is_app = true;

				if (mf->data.dce) {
					dce_recv_pkt(mf->data.dce,
						     msg->r.app.data,
						     msg->r.app.data_len);
				}
			}

		done:
			mem_deref(msg);

			/* NOTE: dce handler might deref mediaflow */
			if (is_app)
				return true;
		}
	}

 next:
	if (packet_is_rtp_or_rtcp(mb)) {

		/* If external RTP is enabled, forward RTP/RTCP packets
		 * to the relevant au/vid-codec.
		 *
		 * otherwise just pass it up to internal RTP-stack
		 */
		external_rtp_recv(mf, src, mb);
		return true; /* handled */
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

	if (!packet_is_rtcp_packet(mb)) {
		update_rx_stats(mf, mbuf_get_left(mb));
	}
	else {
		/* RTCP is sent to both audio+video */

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
		info("mediaflow(%p): first RTP packet received (%zu bytes)\n",
		     mf, mbuf_get_left(mb));
		mf->got_rtp = true;
		tmr_cancel(&mf->tmr_got_rtp);
		check_rtpstart(mf);
	}

	err = rtp_hdr_decode(&hdr, mb);
	mb->pos = start;
	if (err) {
		warning("mediaflow(%p): rtp header decode (%m)\n", mf, err);
		goto out;
	}

	fmt = sdp_media_lformat(mf->audio.sdpm, hdr.pt);
	if (fmt) {

		/* now, pass on the raw RTP/RTCP packet to the decoder */

		if (ac && ac->dec_rtph) {
			ac->dec_rtph(mf->ads,
				     mbuf_buf(mb), mbuf_get_left(mb));

			mediastats_rtp_stats_update(&mf->audio_stats_rcv,
					 mbuf_buf(mb), mbuf_get_left(mb), 0);
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

			uint32_t bwalloc = 0;
			if (vc->dec_bwalloch) {
				bwalloc = vc->dec_bwalloch(mf->video.vds);
			}
			mediastats_rtp_stats_update(&mf->video_stats_rcv,
					 mbuf_buf(mb),mbuf_get_left(mb),
						    bwalloc);
		}

		goto out;
	}

	info("mediaflow(%p): recv: no SDP format found"
	     " for payload type %d\n", mf, hdr.pt);

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


int mediaflow_print_ice(struct re_printf *pf, const struct mediaflow *mf)
{
	int err = 0;

	if (!mf)
		return 0;

	err = trice_debug(pf, mf->trice);

	return err;
}


int mediaflow_summary(struct re_printf *pf, const struct mediaflow *mf)
{
	struct le *le;
	double dur_tx;
	double dur_rx;
	char cid_local_anon[ANON_CLIENT_LEN];
	char cid_remote_anon[ANON_CLIENT_LEN];
	char uid_remote_anon[ANON_ID_LEN];
	int err = 0;

	if (!mf)
		return 0;

	dur_tx = (double)(mf->stat.tx.ts_last - mf->stat.tx.ts_first) / 1000.0;
	dur_rx = (double)(mf->stat.rx.ts_last - mf->stat.rx.ts_first) / 1000.0;

	err |= re_hprintf(pf,
			  "mediaflow(%p): ------------- mediaflow summary -------------\n", mf);
	err |= re_hprintf(pf, "clientid_local:  %s\n",
			  anon_client(cid_local_anon, mf->clientid_local));
	err |= re_hprintf(pf, "clientid_remote: %s\n", 
			  anon_client(cid_remote_anon, mf->clientid_remote));
	err |= re_hprintf(pf, "userid_remote: %s\n", 
			  anon_id(uid_remote_anon, mf->userid_remote));
	err |= re_hprintf(pf, "\n");
	err |= re_hprintf(pf, "sdp: state=%d, got_sdp=%d, sent_sdp=%d\n",
			  mf->sdp_state, mf->got_sdp, mf->sent_sdp);
	err |= re_hprintf(pf, "     remote_tool=%s\n", mf->sdp_rtool);

	err |= re_hprintf(pf, "nat: (ready=%d)\n",
			  mf->ice_ready);
	err |= re_hprintf(pf, "remote candidates:\n");

	err |= mediaflow_print_ice(pf, mf);

	if (mf->sel_pair) {
		err |= re_hprintf(pf, "selected local candidate:   %H\n",
				  trice_cand_print, mf->sel_pair->lcand);
		err |= re_hprintf(pf, "selected remote candidate:  %H\n",
				  trice_cand_print, mf->sel_pair->rcand);
	}
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

		err |= re_hprintf(pf, "        peers: (%u)\n",
				  list_count(&mf->dtls_peers));
		for (le = mf->dtls_peers.head; le; le = le->next) {
			struct dtls_peer *dtls_peer = le->data;

			err |= re_hprintf(pf,
					  "        * peer = %H\n",
					  dtls_peer_print, dtls_peer);
		}

		err |= re_hprintf(pf,
				  "        verified=%d\n"
				  "        setup_local=%s\n"
				  "        setup_remote=%s\n"
				  "",
				  mf->crypto_verified,
				  mediaflow_setup_name(mf->setup_local),
				  mediaflow_setup_name(mf->setup_remote)
				  );
		err |= re_hprintf(pf, "        setup_time=%d ms\n",
				  mf->mf_stats.dtls_estab);
		err |= re_hprintf(pf, "        packets sent=%u, recv=%u\n",
				  mf->mf_stats.dtls_pkt_sent,
				  mf->mf_stats.dtls_pkt_recv);
	}
	err |= re_hprintf(pf, "        srtp  = %s\n",
			  srtp_suite_name(mf->srtp_suite));
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, "RTP packets:\n");
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

	err |= re_hprintf(pf, "\n");
	err |= re_hprintf(pf, "SDP recvd:       %zu\n", mf->stat.n_sdp_recv);
	err |= re_hprintf(pf, "SRTP dropped:    %zu\n",
			  mf->stat.n_srtp_dropped);
	err |= re_hprintf(pf, "SRTP errors:     %zu\n",
			  mf->stat.n_srtp_error);

	err |= re_hprintf(pf, "\naudio_active: %d\n", !mf->audio.disabled);
	err |= re_hprintf(pf, "\nvideo_media:  %d\n", mf->video.has_media);

	if (1) {

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


int mediaflow_rtp_summary(struct re_printf *pf, const struct mediaflow *mf)
{
	struct aucodec_stats *voe_stats;
	int err = 0;

	if (!mf)
		return 0;

	err |= re_hprintf(pf,
			  "mediaflow(%p): ------------- mediaflow RTP summary -------------\n", mf);

	if (!mf->audio.disabled) {
		voe_stats = mediaflow_codec_stats((struct mediaflow*)mf);
		err |= re_hprintf(pf,"Audio TX: \n");
		if (voe_stats) {
			err |= re_hprintf(pf,"Level (dB) %.1f %.1f %.1f \n",
					  voe_stats->in_vol.min,
					  voe_stats->in_vol.avg,
					  voe_stats->in_vol.max);
		}
		err |= re_hprintf(pf,"Bit rate (kbps) %.1f %.1f %.1f \n",
				  mf->audio_stats_snd.bit_rate_stats.min,
				  mf->audio_stats_snd.bit_rate_stats.avg,
				  mf->audio_stats_snd.bit_rate_stats.max);
		err |= re_hprintf(pf,"Packet rate (1/s) %.1f %.1f %.1f \n",
				  mf->audio_stats_snd.pkt_rate_stats.min,
				  mf->audio_stats_snd.pkt_rate_stats.avg,
				  mf->audio_stats_snd.pkt_rate_stats.max);
		err |= re_hprintf(pf,"Loss rate (pct) %.1f %.1f %.1f \n",
				  mf->audio_stats_snd.pkt_loss_stats.min,
				  mf->audio_stats_snd.pkt_loss_stats.avg,
				  mf->audio_stats_snd.pkt_loss_stats.max);

		err |= re_hprintf(pf,"Audio RX: \n");
		if (voe_stats) {
			err |= re_hprintf(pf,"Level (dB) %.1f %.1f %.1f \n",
					  voe_stats->out_vol.min,
					  voe_stats->out_vol.avg,
					  voe_stats->out_vol.max);
		}
		err |= re_hprintf(pf,"Bit rate (kbps) %.1f %.1f %.1f \n",
				  mf->audio_stats_rcv.bit_rate_stats.min,
				  mf->audio_stats_rcv.bit_rate_stats.avg,
				  mf->audio_stats_rcv.bit_rate_stats.max);
		err |= re_hprintf(pf,"Packet rate (1/s) %.1f %.1f %.1f \n",
				  mf->audio_stats_rcv.pkt_rate_stats.min,
				  mf->audio_stats_rcv.pkt_rate_stats.avg,
				  mf->audio_stats_rcv.pkt_rate_stats.max);
		err |= re_hprintf(pf,"Loss rate (pct) %.1f %.1f %.1f \n",
				  mf->audio_stats_rcv.pkt_loss_stats.min,
				  mf->audio_stats_rcv.pkt_loss_stats.avg,
				  mf->audio_stats_rcv.pkt_loss_stats.max);
		err |= re_hprintf(pf,"Mean burst length %.1f %.1f %.1f \n",
				  mf->audio_stats_rcv.pkt_mbl_stats.min,
				  mf->audio_stats_rcv.pkt_mbl_stats.avg,
				  mf->audio_stats_rcv.pkt_mbl_stats.max);
		if (voe_stats){
			err |= re_hprintf(pf,"JB size (ms) %.1f %.1f %.1f \n",
					  voe_stats->jb_size.min,
					  voe_stats->jb_size.avg,
					  voe_stats->jb_size.max);
			err |= re_hprintf(pf,"RTT (ms) %.1f %.1f %.1f \n",
					  voe_stats->rtt.min,
					  voe_stats->rtt.avg,
					  voe_stats->rtt.max);
		}
		err |= re_hprintf(pf,"Packet dropouts (#) %d \n",
				  mf->audio_stats_rcv.dropouts);
	}
	if (mf->video.has_media){
		err |= re_hprintf(pf,"Video TX: \n");
		err |= re_hprintf(pf,"Bit rate (kbps) %.1f %.1f %.1f \n",
				  mf->video_stats_snd.bit_rate_stats.min,
				  mf->video_stats_snd.bit_rate_stats.avg,
				  mf->video_stats_snd.bit_rate_stats.max);
		err |= re_hprintf(pf,"Alloc rate (kbps) %.1f %.1f %.1f \n",
				  mf->video_stats_snd.bw_alloc_stats.min,
				  mf->video_stats_snd.bw_alloc_stats.avg,
				  mf->video_stats_snd.bw_alloc_stats.max);
		err |= re_hprintf(pf,"Frame rate (1/s) %.1f %.1f %.1f \n",
				  mf->video_stats_snd.frame_rate_stats.min,
				  mf->video_stats_snd.frame_rate_stats.avg,
				  mf->video_stats_snd.frame_rate_stats.max);
		err |= re_hprintf(pf,"Loss rate (pct) %.1f %.1f %.1f \n",
				  mf->video_stats_snd.pkt_loss_stats.min,
				  mf->video_stats_snd.pkt_loss_stats.avg,
				  mf->video_stats_snd.pkt_loss_stats.max);

		err |= re_hprintf(pf,"Video RX: \n");
		err |= re_hprintf(pf,"Bit rate (kbps) %.1f %.1f %.1f \n",
				  mf->video_stats_rcv.bit_rate_stats.min,
				  mf->video_stats_rcv.bit_rate_stats.avg,
				  mf->video_stats_rcv.bit_rate_stats.max);
		err |= re_hprintf(pf,"Alloc rate (kbps) %.1f %.1f %.1f \n",
				  mf->video_stats_rcv.bw_alloc_stats.min,
				  mf->video_stats_rcv.bw_alloc_stats.avg,
				  mf->video_stats_rcv.bw_alloc_stats.max);
		err |= re_hprintf(pf,"Frame rate (1/s) %.1f %.1f %.1f \n",
				  mf->video_stats_rcv.frame_rate_stats.min,
				  mf->video_stats_rcv.frame_rate_stats.avg,
				  mf->video_stats_rcv.frame_rate_stats.max);
		err |= re_hprintf(pf,"Loss rate (pct) %.1f %.1f %.1f \n",
				  mf->video_stats_rcv.pkt_loss_stats.min,
				  mf->video_stats_rcv.pkt_loss_stats.avg,
				  mf->video_stats_rcv.pkt_loss_stats.max);
		err |= re_hprintf(pf,"Packet dropouts (#) %d \n",
				  mf->video_stats_rcv.dropouts);
	}

	err |= re_hprintf(pf,
			  "-----------------------------------------------\n");

	return err;
}


/* NOTE: all udp-helpers must be free'd before RTP-socket */
static void destructor(void *arg)
{
	struct mediaflow *mf = arg;
	void *p;

	if (MAGIC != mf->magic) {
		warning("mediaflow(%p): destructor: bad magic (0x%08x)\n",
			mf, mf->magic);
	}

	mf->terminated = true;

	mf->estabh = NULL;
	mf->closeh = NULL;
	mf->restarth = NULL;

	if (mf->started)
		mediaflow_stop_media(mf);

	info("mediaflow(%p): destroyed (%H) got_sdp=%d\n",
	     mf, print_errno, mf->err, mf->got_sdp);

	/* print a nice summary */
	if (mf->got_sdp) {
		info("%H\n", mediaflow_summary, mf);
		info("%H\n", mediaflow_rtp_summary, mf);
	}

	tmr_cancel(&mf->tmr_rtp);
	tmr_cancel(&mf->tmr_got_rtp);	
	tmr_cancel(&mf->tmr_error);

	/* XXX: voe is calling to mediaflow_xxx here */
	/* deref the encoders/decodrs first, as they may be multithreaded,
	 * and callback in here...
	 * Remove decoder first as webrtc might still send RTCP packets
	 */
	p = mf->ads;
	mf->ads = NULL;	
	mem_deref(p);

	p = mf->aes;
	mf->aes = NULL;
	mem_deref(p);

	p = mf->video.ves;
	mf->video.ves = NULL;
	mem_deref(p);

	p = mf->video.vds;
	mf->video.vds = NULL;
	mem_deref(p);

	mf->data.dce = mem_deref(mf->data.dce);

	mf->tls_conn = mem_deref(mf->tls_conn);

	list_flush(&mf->interfacel);
	list_flush(&mf->dtls_peers);

	mf->trice_uh = mem_deref(mf->trice_uh);  /* note: destroy first */
	mf->sel_pair = mem_deref(mf->sel_pair);
	mf->trice = mem_deref(mf->trice);
	mf->trice_stun = mem_deref(mf->trice_stun);
	mem_deref(mf->us_stun);
	list_flush(&mf->turnconnl);

	mem_deref(mf->dtls_sock);

	mem_deref(mf->uh_srtp);

	mem_deref(mf->rtp); /* must be free'd after ICE and DTLS */
	list_flush(&mf->audio.formatl);	
	mem_deref(mf->sdp);

	mem_deref(mf->srtp_tx);
	mem_deref(mf->srtp_rx);
	mem_deref(mf->dtls);
	mem_deref(mf->ct_gather);
	mem_deref(mf->kase);

	mem_deref(mf->label);
	mem_deref(mf->video.label);

	mem_deref(mf->peer_software);

	mem_deref(mf->mq);
	mem_deref(mf->userid_remote);
	mem_deref(mf->clientid_remote);
	mem_deref(mf->clientid_local);

	mem_deref(mf->extmap);
}


/* XXX: check if we need this, or it can be moved ? */
static void stun_udp_recv_handler(const struct sa *src,
				  struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	struct stun_unknown_attr ua;
	struct stun_msg *msg = NULL;

	debug("mediaflow(%p): stun: receive %zu bytes from %J\n",
	      mf, mbuf_get_left(mb), src);

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
		if (!mf->sent_rtp) {
			info("mediaflow(%p): first RTP packet sent\n", mf);
			mf->sent_rtp = true;
			if (!mf->got_rtp) {
				if (!tmr_isrunning(&mf->tmr_got_rtp)) {
					tmr_start(&mf->tmr_got_rtp,
						  RTP_FIRST_PKT_TIMEOUT_MS,
						  tmr_got_rtp_handler,
						  mf);
				}
			}
			check_rtpstart(mf);
		}
		break;
	}
}


/*
 * See https://tools.ietf.org/html/draft-ietf-rtcweb-jsep-14#section-5.1.1
 */
static const char *sdp_profile(enum media_crypto cryptos)
{
	if (cryptos & CRYPTO_DTLS_SRTP)
		return "UDP/TLS/RTP/SAVPF";

	return "RTP/SAVPF";
}


/* should not reach here */
static void rtp_recv_handler(const struct sa *src,
			     struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	(void)src;
	(void)mb;

	info("mediaflow(%p): nobody cared about incoming packet (%zu bytes)\n",
	     mf, mbuf_get_left(mb));
}


static int init_ice(struct mediaflow *mf)
{
	struct trice_conf conf = {
		.nom = ICE_NOMINATION_AGGRESSIVE,
		.debug = false,
		.trace = false,
#if TARGET_OS_IPHONE
		.ansi = false,
#elif defined (__ANDROID__)
		.ansi = false,
#else
		.ansi = true,
#endif
		.enable_prflx = !mf->privacy_mode
	};
	enum ice_role role = ICE_ROLE_UNKNOWN;  /* NOTE: this is set later */
	int err;

	err = trice_alloc(&mf->trice, &conf, role,
			  mf->ice_ufrag, mf->ice_pwd);
	if (err) {
		warning("mediaflow(%p): DUALSTACK trice error (%m)\n",
			mf, err);
		goto out;
	}

	err = trice_set_software(mf->trice, avs_version_str());
	if (err)
		goto out;

	err = stun_alloc(&mf->trice_stun, NULL, NULL, NULL);
	if (err)
		goto out;

	/*
	 * tuning the STUN transaction values
	 *
	 * RTO=150 and RC=7 gives around 12 seconds timeout
	 */
	stun_conf(mf->trice_stun)->rto = 150;  /* milliseconds */
	stun_conf(mf->trice_stun)->rc =    8;  /* retransmits */

	/* Virtual socket for directing outgoing Packets */
	err = udp_register_helper(&mf->trice_uh, mf->rtp,
				  LAYER_ICE,
				  udp_helper_send_handler_trice,
				  NULL, mf);
	if (err)
		goto out;

 out:
	return err;
}

static void af_destructor(void *arg)
{
	struct auformat *af = arg;

	list_unlink(&af->le);
	mem_deref(af->fmt);
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
int mediaflow_alloc(struct mediaflow **mfp, const char *clientid_local,
		    struct tls *dtls,
		    const struct list *aucodecl,
		    const struct sa *laddr_sdp,
		    enum media_crypto cryptos,
		    mediaflow_estab_h *estabh,
		    mediaflow_stopped_h *stoppedh,
		    mediaflow_close_h *closeh,
		    mediaflow_restart_h *restarth,
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
	mf->privacy_mode = false;
	mf->group_mode = false;
	mf->af = sa_af(laddr_sdp);

	mf->mf_stats.turn_alloc = -1;
	mf->mf_stats.nat_estab  = -1;
	mf->mf_stats.dtls_estab = -1;
	mf->mf_stats.dce_estab  = -1;

	tmr_init(&mf->tmr_rtp);
	tmr_init(&mf->tmr_got_rtp);
	tmr_init(&mf->tmr_error);
	
	if (!str_isset(clientid_local)) {
		warning("mediaflow(%p): alloc: missing local clientid\n", mf);
		err = EINVAL;
		goto out;
	}

	err = str_dup(&mf->clientid_local, clientid_local);
	if (err)
		goto out;

	err = mqueue_alloc(&mf->mq, mq_callback, mf);
	if (err)
		goto out;

	mf->dtls   = mem_ref(dtls);
	mf->setup_local    = SETUP_ACTPASS;
	mf->setup_remote   = SETUP_ACTPASS;
	mf->cryptos_local = cryptos;
	mf->crypto_fallback = CRYPTO_DTLS_SRTP;

	mf->estabh = estabh;
	mf->stoppedh = stoppedh;
	mf->closeh = closeh;
	mf->restarth = restarth;
	mf->arg    = arg;

	err = pthread_mutex_init(&mf->mutex_enc, NULL);
	if (err)
		goto out;

	rand_str(mf->ice_ufrag, sizeof(mf->ice_ufrag));
	rand_str(mf->ice_pwd, sizeof(mf->ice_pwd));

	/* RTP must listen on 0.0.0.0 so that we can send/recv
	   packets on all interfaces */
	sa_init(&laddr_rtp, AF_INET);

	err = udp_listen(&mf->rtp, &laddr_rtp, rtp_recv_handler, mf);
	if (err) {
		warning("mediaflow(%p): rtp_listen failed (%m)\n", mf, err);
		goto out;
	}

	lport = PORT_DISCARD;

	err = sdp_session_alloc(&mf->sdp, laddr_sdp);
	if (err)
		goto out;

	(void)sdp_session_set_lattr(mf->sdp, true, "tool", avs_version_str());

	err = sdp_media_add(&mf->audio.sdpm, mf->sdp, "audio",
			    PORT_DISCARD,
			    sdp_profile(cryptos));
	if (err)
		goto out;

	sdp_media_set_lbandwidth(mf->audio.sdpm,
				 SDP_BANDWIDTH_AS, AUDIO_BANDWIDTH);

	/* needed for new versions of WebRTC */
	err = sdp_media_set_alt_protos(mf->audio.sdpm, 2,
				       "UDP/TLS/RTP/SAVPF", "RTP/SAVPF");
	if (err)
		goto out;

	sdp_media_set_lattr(mf->audio.sdpm, false, "mid", "audio");

	rand_str(mf->cname, sizeof(mf->cname));
	rand_str(mf->msid, sizeof(mf->msid));
	err = uuid_v4(&mf->label);
	err |= uuid_v4(&mf->video.label);
	if (err)
		goto out;

	regen_lssrc(mf);
	debug("mediaflow(%p): local SSRC is %u\n",
	      mf, mf->lssrcv[MEDIA_AUDIO]);

	err = sdp_media_set_lattr(mf->audio.sdpm, false, "ssrc", "%u cname:%s",
				  mf->lssrcv[MEDIA_AUDIO], mf->cname);
	if (err)
		goto out;

	/* ICE */
	err = init_ice(mf);
	if (err)
		goto out;

	/* populate SDP with all known audio-codecs */
	LIST_FOREACH(aucodecl, le) {
		struct aucodec *ac = list_ledata(le);
		struct auformat *af;

		af = mem_zalloc(sizeof(*af), af_destructor);
		if (!af) {
			err = ENOMEM;
			goto out;
		}
		err = sdp_format_add(&af->fmt, mf->audio.sdpm, false,
				     ac->pt, ac->name, ac->srate, ac->ch,
				     ac->fmtp_ench, ac->fmtp_cmph, ac, false,
				     "%s", ac->fmtp);
		if (err)
			goto out;

		af->ac = ac;
		list_append(&mf->audio.formatl, &af->le, af);
	}

	/* Set ICE-options */
	sdp_session_set_lattr(mf->sdp, false, "ice-options",
			      "trickle");

	/* Mandatory */
	sdp_media_set_lattr(mf->audio.sdpm, false, "rtcp-mux", NULL);

	lport = PORT_DISCARD;
	sdp_media_set_lport_rtcp(mf->audio.sdpm, PORT_DISCARD);


	sdp_media_set_lattr(mf->audio.sdpm, false, "ice-ufrag",
			    "%s", mf->ice_ufrag);
	sdp_media_set_lattr(mf->audio.sdpm, false, "ice-pwd",
			    "%s", mf->ice_pwd);


	mf->srtp_suite = (enum srtp_suite)-1;

	/* we enable support for DTLS-SRTP by default, so that the
	   SDP attributes are sent in the offer. the attributes
	   might change later though, depending on the SDP answer */

	if (cryptos & CRYPTO_DTLS_SRTP) {

		struct sa laddr_dtls;

		sa_set_str(&laddr_dtls, "0.0.0.0", 0);

		if (!mf->dtls) {
			warning("mediaflow(%p): dtls context is missing\n", mf);
		}

		err = dtls_listen(&mf->dtls_sock, &laddr_dtls,
				  NULL, 2, LAYER_DTLS,
				  dtls_conn_handler, mf);
		if (err) {
			warning("mediaflow(%p): dtls_listen failed (%m)\n",
				mf, err);
			goto out;
		}

		/* Virtual socket for re-directing outgoing DTLS-packet */
		err = udp_register_helper(&mf->dtls_uh,
					  dtls_udp_sock(mf->dtls_sock),
					  LAYER_DTLS_TRANSPORT,
					  send_dtls_handler,
					  NULL, mf);
		if (err)
			goto out;

		dtls_set_mtu(mf->dtls_sock, DTLS_MTU);

		err = sdp_media_set_lattr(mf->audio.sdpm, true,
					  "fingerprint", "sha-256 %H",
					  dtls_print_sha256_fingerprint,
					  mf->dtls);
		if (err)
			goto out;

		err = sdp_media_set_lattr(mf->audio.sdpm, true, "setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			goto out;
	}
	if (cryptos & CRYPTO_KASE) {

		err = kase_alloc(&mf->kase);
		if (err)
			goto out;

		err = sdp_media_set_lattr(mf->audio.sdpm, true, "x-KASEv1", "%H",
					  kase_print_publickey, mf->kase);
		if (err)
			goto out;

		err = sdp_media_set_lattr(mf->audio.sdpm, true, "setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			goto out;
	}

	/* install UDP socket helpers */
	err |= udp_register_helper(&mf->uh_srtp, mf->rtp, LAYER_SRTP,
				   udp_helper_send_handler_srtp,
				   udp_helper_recv_handler_srtp,
				   mf);
	if (err)
		goto out;

	{
		int dce_err;

		dce_err = dce_alloc(&mf->data.dce,
				    dce_send_data_handler,
				    dce_estab_handler,
				    mf);
		if (dce_err) {
			info("mediaflow(%p): dce_alloc failed (%m)\n",
			     mf, dce_err);
		}
	}

	mf->laddr_default = *laddr_sdp;
	sa_set_port(&mf->laddr_default, lport);

	err = extmap_alloc(&mf->extmap);

	info("mediaflow(%p): created new mediaflow with"
	     " local port %u and %u audio-codecs"
	     " \n",
	     mf, lport, list_count(aucodecl));

 out:
	if (err)
		mem_deref(mf);
	else if (mfp)
		*mfp = mf;

	return err;
}


int mediaflow_set_setup(struct mediaflow *mf, enum media_setup setup)
{
	int err;

	if (!mf)
		return EINVAL;

	info("mediaflow(%p): local_setup: `%s' --> `%s'\n",
	     mf,
	     mediaflow_setup_name(mf->setup_local),
	     mediaflow_setup_name(setup));

	if (setup != mf->setup_local) {

		if (mf->setup_local == SETUP_ACTPASS) {

			mf->setup_local = setup;
		}
		else {
			warning("mediaflow(%p): set_setup: Illegal transition"
				" from `%s' to `%s'\n",
				mf, mediaflow_setup_name(mf->setup_local),
				mediaflow_setup_name(setup));
			return EPROTO;
		}
	}

	err = sdp_media_set_lattr(mf->audio.sdpm, true, "setup",
				  mediaflow_setup_name(mf->setup_local));
	if (err)
		return err;

	if (mf->video.sdpm) {
		err = sdp_media_set_lattr(mf->video.sdpm, true,
					"setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			return err;
	}

	return 0;
}


bool mediaflow_is_sdp_offerer(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->sdp_offerer;
}


enum media_setup mediaflow_local_setup(const struct mediaflow *mf)
{
	if (!mf)
		return (enum media_setup)-1;

	return mf->setup_local;
}

static bool vid_fmtp_cmp_handler(const char *params1, const char *params2,
			     void *data)
{
	struct vid_ref *vr = data;

	if (vr->vc && vr->vc->fmtp_cmph)
		return vr->vc->fmtp_cmph(params1, params2, vr->vc);

	return true;
}


static int vid_fmtp_enc_handler(struct mbuf *mb, const struct sdp_format *fmt,
				bool offer, void *data)
{
	struct vid_ref *vr = data;
	struct videnc_fmt_data fmtdata;

	if (!vr->vc || !vr->mf)
		return 0;

	memset(&fmtdata, 0, sizeof(fmtdata));
	if (vr->vc->codec_ref) {
		fmtdata.ref_fmt = sdp_media_format(vr->mf->video.sdpm, true, NULL, -1,
					   vr->vc->codec_ref->name, -1, -1);
	}

	fmtdata.extmap = vr->mf->extmap;

	if (vr->vc->fmtp_ench)
		return vr->vc->fmtp_ench(mb, fmt, offer, &fmtdata);
	else
		return 0;
}


int mediaflow_disable_audio(struct mediaflow *mf)
{
	int err = 0;

	err = sdp_media_set_lattr(mf->audio.sdpm, false, "inactive", NULL);
	if (err)
		goto out;

	mf->audio.disabled = true;

out:
	return err;
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

	info("mediaflow(%p): adding video-codecs (%u)\n",
	     mf, list_count(vidcodecl));

	err = sdp_media_add(&mf->video.sdpm, mf->sdp, "video",
			    PORT_DISCARD,
			    sdp_profile(mf->cryptos_local));
	if (err)
		goto out;

	sdp_media_set_lbandwidth(mf->video.sdpm,
				 SDP_BANDWIDTH_AS, VIDEO_BANDWIDTH);

	/* needed for new versions of WebRTC */
	err = sdp_media_set_alt_protos(mf->video.sdpm, 2,
				       "UDP/TLS/RTP/SAVPF", "RTP/SAVPF");
	if (err)
		goto out;


	/* SDP media attributes */

	sdp_media_set_lattr(mf->video.sdpm, false, "mid", "video");
	sdp_media_set_lattr(mf->video.sdpm, false, "rtcp-mux", NULL);

	sdp_media_set_lport_rtcp(mf->video.sdpm, PORT_DISCARD);

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
					"setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			goto out;
	}

	{
		size_t ssrcc = list_count(vidcodecl);
		uint32_t ssrcv[SSRC_MAX];
		char ssrc_group[16];
		char ssrc_fid[sizeof(ssrc_group)*SSRC_MAX + 5];
		int i = 0;
		int k = 0;

		if (ssrcc > SSRC_MAX) {
			warning("mediaflow(%p): max %d SSRC's\n", mf, SSRC_MAX);
			err = EOVERFLOW;
			goto out;
		}

		*ssrc_fid = '\0';

		LIST_FOREACH(vidcodecl, le) {
			struct vidcodec *vc = list_ledata(le);
			struct vid_ref *vr;
		   
			vr = mem_zalloc(sizeof(*vr), NULL);
			if (!vr)
				goto out;

			vr->mf = mf;
			vr->vc = vc;

			err = sdp_format_add(NULL, mf->video.sdpm, false,
					     vc->pt, vc->name, 90000, 1,
					     vid_fmtp_enc_handler,
					     vid_fmtp_cmp_handler,
					     vr, true,
					     "%s", vc->fmtp);
			mem_deref(vr);
			if (err) {
				
				goto out;
			}

			ssrcv[i] = rand_u32();
			re_snprintf(ssrc_group, sizeof(ssrc_group),
				    "%u ", ssrcv[i]);
			strcat(ssrc_fid, ssrc_group);
			++i;
		}
		if (strlen(ssrc_fid))
			ssrc_fid[strlen(ssrc_fid) - 1] = '\0';

		err = sdp_media_set_lattr(mf->video.sdpm, false, "ssrc-group",
					  "FID %s", ssrc_fid);
		if (err)
			goto out;

		if (ssrcc > 0)
			mf->lssrcv[MEDIA_VIDEO] = ssrcv[0];
		if (ssrcc > 1)
			mf->lssrcv[MEDIA_VIDEO_RTX] = ssrcv[1];

		for (k = 0; k < i; ++k) {
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

 out:
	return err;
}


int mediaflow_add_data(struct mediaflow *mf)
{
	int err;

	if (!mf)
		return EINVAL;

	info("mediaflow(%p): add_data: adding data channel\n", mf);

	err = sdp_media_add(&mf->data.sdpm, mf->sdp, "application",
			    PORT_DISCARD,
			    "DTLS/SCTP");
	if (err)
		goto out;

	sdp_media_set_lattr(mf->data.sdpm, false, "mid", "data");

	sdp_media_set_lattr(mf->data.sdpm, false,
			    "ice-ufrag", "%s", mf->ice_ufrag);
	sdp_media_set_lattr(mf->data.sdpm, false,
			    "ice-pwd", "%s", mf->ice_pwd);

	if (mf->dtls) {
		err = sdp_media_set_lattr(mf->data.sdpm, true,
					  "fingerprint", "sha-256 %H",
					  dtls_print_sha256_fingerprint,
					  mf->dtls);
		if (err) {
			warning("mediaflow(%p): add_data: failed to lattr "
				"'fingerprint': %m\n", mf, err);
			goto out;
		}

		err = sdp_media_set_lattr(mf->data.sdpm, true,
					"setup",
					mediaflow_setup_name(mf->setup_local));
		if (err) {
			warning("mediaflow(%p): add_data: failed to lattr "
				"'setup': %m\n", mf, err);
			goto out;
		}
	}

	err = sdp_format_add(NULL, mf->data.sdpm, false,
			     "5000", NULL, 0, 0,
			     NULL, NULL, NULL, false, NULL);
	if (err)
		goto out;

	err = sdp_media_set_lattr(mf->data.sdpm, true,
			      "sctpmap", "5000 webrtc-datachannel 16");
	if (err) {
		warning("mediaflow(%p): add_data: failed to add lattr: %m\n",
			mf, err);
		goto out;
	}

 out:
	return err;
}


void mediaflow_set_tag(struct mediaflow *mf, const char *tag)
{
	if (!mf)
		return;

	str_ncpy(mf->tag, tag, sizeof(mf->tag));
}


static int handle_setup(struct mediaflow *mf)
{
	const char *rsetup;
	enum media_setup setup_local;
	int err;

	rsetup = sdp_media_session_rattr(mf->audio.sdpm, mf->sdp, "setup");

	info("mediaflow(%p): remote_setup=%s\n", mf, rsetup);

	mf->setup_remote = setup_resolve(rsetup);

	switch (mf->setup_remote) {

	case SETUP_ACTPASS:
		/* RFC 5763 setup:active is RECOMMENDED */
		if (mf->setup_local == SETUP_ACTPASS)
			setup_local = SETUP_ACTIVE;
		else
			setup_local = mf->setup_local;
		break;

	case SETUP_ACTIVE:
		setup_local = SETUP_PASSIVE;
		break;

	case SETUP_PASSIVE:
		setup_local = SETUP_ACTIVE;
		break;

	default:
		warning("mediaflow(%p): illegal setup '%s' from remote\n",
			mf, rsetup);
		return EPROTO;
	}

	info("mediaflow(%p): local_setup=%s\n",
	     mf, mediaflow_setup_name(mf->setup_local));

	mediaflow_set_setup(mf, setup_local);

	err = sdp_media_set_lattr(mf->audio.sdpm, true, "setup",
				  mediaflow_setup_name(mf->setup_local));
	if (err)
		return err;

	if (mf->video.sdpm) {
		err = sdp_media_set_lattr(mf->video.sdpm, true,
					"setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			return err;
	}

	if (mf->data.sdpm) {
		err = sdp_media_set_lattr(mf->data.sdpm, true,
					"setup",
					mediaflow_setup_name(mf->setup_local));
		if (err)
			return err;
	}

	return 0;
}


static int handle_dtls_srtp(struct mediaflow *mf)
{
	const char *fingerprint;
	struct pl fp_name;
	re_printf_h *fp_printh;
	int err;

	fingerprint = sdp_media_session_rattr(mf->audio.sdpm, mf->sdp,
					      "fingerprint");

	err = re_regex(fingerprint, str_len(fingerprint),
		       "[^ ]+ [0-9A-F:]*", &fp_name, NULL);
	if (err) {
		warning("mediaflow(%p): could not parse fingerprint attr\n",
			mf);
		return err;
	}

	debug("mediaflow(%p): DTLS-SRTP fingerprint selected (%r)\n",
	      mf, &fp_name);

	if (0 == pl_strcasecmp(&fp_name, "sha-256")) {
		fp_printh = (re_printf_h *)dtls_print_sha256_fingerprint;
	}
	else {
		warning("mediaflow(%p): unsupported fingerprint (%r)\n",
			mf, &fp_name);
		return EPROTO;
	}

	err = sdp_media_set_lattr(mf->audio.sdpm, true, "fingerprint", "%r %H",
				  &fp_name, fp_printh, mf->dtls);
	if (err)
		return err;


	err = handle_setup(mf);
	if (err) {
		warning("mediaflow(%p): handle_setup failed (%m)\n", mf, err);
		return err;
	}

	debug("mediaflow(%p): incoming SDP offer has DTLS fingerprint = '%s'\n",
	      mf, fingerprint);

	/* DTLS has already been established, before SDP o/a */
	if (mf->crypto_ready && mf->tls_conn && !mf->crypto_verified) {

		info("mediaflow(%p): sdp: verifying DTLS fp\n", mf);

		if (!verify_fingerprint(mf, mf->sdp, mf->audio.sdpm, mf->tls_conn)) {
			warning("mediaflow(%p): dtls_srtp: could not verify"
				" remote fingerprint\n", mf);
			return EAUTH;
		}

		mf->crypto_verified = true;
	}

	return 0;
}


static void demux_packet(struct mediaflow *mf, const struct sa *src,
			 struct mbuf *mb)
{
	enum packet pkt;
	bool hdld;

	pkt = packet_classify_packet_type(mb);

	if (mf->trice) {

		/* if the incoming UDP packet is not in the list of
		 * remote ICE candidates, we should not trust it.
		 * note that new remote candidates are added dynamically
		 * as PRFLX in the ICE-layer.
		 */
		if (!trice_rcand_find(mf->trice, ICE_COMPID_RTP,
				      IPPROTO_UDP, src)) {

			debug("mediaflow(%p): demux: unauthorized"
			      " %s packet from %J"
			      " (rcand-list=%u)\n",
			      mf, packet_classify_name(pkt), src,
			      list_count(trice_rcandl(mf->trice)));
		}
	}

	switch (pkt) {

	case PACKET_RTP:
	case PACKET_RTCP:
		hdld = udp_helper_recv_handler_srtp((struct sa *)src, mb, mf);
		if (!hdld) {
			warning("mediaflow(%p): rtp packet not handled\n", mf);
		}
		break;

	case PACKET_DTLS:
		handle_dtls_packet(mf, src, mb);
		break;

	case PACKET_STUN:
		stun_udp_recv_handler(src, mb, mf);
		break;

	default:
		warning("mediaflow(%p): @@@ udp: dropping %zu bytes from %J\n",
			mf, mbuf_get_left(mb), src);
		break;
	}
}


static void trice_udp_recv_handler(const struct sa *src, struct mbuf *mb,
				   void *arg)
{
	struct mediaflow *mf = arg;

	demux_packet(mf, src, mb);
}


static void interface_destructor(void *data)
{
	struct interface *ifc = data;

	list_unlink(&ifc->le);
	/*mem_deref(ifc->lcand);*/
}


static int interface_add(struct mediaflow *mf, struct ice_lcand *lcand,
			 const char *ifname, const struct sa *addr)
{
	struct interface *ifc;

	ifc = mem_zalloc(sizeof(*ifc), interface_destructor);
	if (!ifc)
		return ENOMEM;


	ifc->lcand = lcand;
	ifc->addr = *addr;
	if (ifname)
		str_ncpy(ifc->ifname, ifname, sizeof(ifc->ifname));
	ifc->is_default = sa_cmp(addr, &mf->laddr_default, SA_ADDR);
	ifc->mf = mf;

	list_append(&mf->interfacel, &ifc->le, ifc);

	return 0;
}


static struct interface *interface_find(const struct list *interfacel,
					const struct sa *addr)
{
	struct le *le;

	for (le = list_head(interfacel); le; le = le->next) {
		struct interface *ifc = le->data;

		if (sa_cmp(addr, &ifc->addr, SA_ADDR))
			return ifc;
	}

	return NULL;
}


/*
 * Calculate the local preference for ICE
 *
 * - The interface type takes precedence over address family
 * - IPv4 takes precedence over IPv6, due to stability
 *
 */
static uint16_t calc_local_preference(const char *ifname, int af)
{
	uint16_t lpref_af, lpref_ifc;

	/* VPN */
	if (0 == re_regex(ifname, str_len(ifname), "ipsec") ||
	    0 == re_regex(ifname, str_len(ifname), "utun")) {

		lpref_ifc = 1;
	}
	/* GPRS */
	else if (0 == re_regex(ifname, str_len(ifname), "pdp_ip")) {

		lpref_ifc = 2;
	}
	/* Normal interface */
	else {
		lpref_ifc = 3;
	}

	switch (af) {

	default:
	case AF_INET:
		lpref_af = 2;
		break;

	case AF_INET6:
		lpref_af = 1;
		break;
	}

	return lpref_ifc<<8 | lpref_af;
}


/* NOTE: only ADDRESS portion of 'addr' is used */
int mediaflow_add_local_host_candidate(struct mediaflow *mf,
				       const char *ifname,
				       const struct sa *addr)
{
	struct ice_lcand *lcand = NULL;
	struct interface *ifc;
	const uint16_t lpref = calc_local_preference(ifname, sa_af(addr));
	const uint32_t prio = ice_cand_calc_prio(ICE_CAND_TYPE_HOST, lpref, 1);
	int err = 0;

	if (!mf || !addr)
		return EINVAL;

	if (!sa_isset(addr, SA_ADDR)) {
		warning("mediaflow(%p): add_cand: address not set\n", mf);
		return EINVAL;
	}
	if (sa_port(addr)) {
		warning("mediaflow(%p): add_local_host: "
			"Port should not be set\n", mf);
		return EINVAL;
	}

	info("mediaflow(%p): add_local_host_cand: "
	     " %s:%j  (lpref=0x%04x prio=0x%08x)\n",
	     mf, ifname, addr, lpref, prio);

	ifc = interface_find(&mf->interfacel, addr);
	if (ifc) {
		info("mediaflow(%p): interface already added\n", mf);
		return 0;
	}

	if (!mf->privacy_mode) {
		err = trice_lcand_add(&lcand, mf->trice,
				      ICE_COMPID_RTP,
				      IPPROTO_UDP, prio, addr, NULL,
				      ICE_CAND_TYPE_HOST, NULL,
				      0,     /* tcptype */
				      NULL,  /* sock */
				      0);
		if (err) {
			warning("mediaflow(%p): add_local_host[%j]"
				" failed (%m)\n",
				mf, addr, err);
			return err;
		}

		/* hijack the UDP-socket of the local candidate
		 *
		 * NOTE: this must be done for all local candidates
		 */
		udp_handler_set(lcand->us, trice_udp_recv_handler, mf);

		err = sdp_media_set_lattr(mf->audio.sdpm, false,
					  "candidate",
					  "%H",
					  ice_cand_attr_encode, lcand);
		if (err)
			return err;

		if (ifname) {
			str_ncpy(lcand->ifname, ifname,
				 sizeof(lcand->ifname));
		}

		udp_sockbuf_set(lcand->us, UDP_SOCKBUF_SIZE);
	}

	err = interface_add(mf, lcand, ifname, addr);
	if (err)
		return err;

	return err;
}


void mediaflow_set_ice_role(struct mediaflow *mf, enum ice_role role)
{
	int err;

	if (!mf)
		return;

	if (sdp_media_session_rattr(mf->audio.sdpm, mf->sdp, "ice-lite")) {
		info("mediaflow(%p): remote side is ice-lite"
		     " -- force controlling\n", mf);

		role = ICE_ROLE_CONTROLLING;
	}

	err = trice_set_role(mf->trice, role);
	if (err) {
		warning("mediaflow(%p): trice_set_role failed (%m)\n",
			mf, err);
		return;
	}
}


int mediaflow_generate_offer(struct mediaflow *mf, char *sdp, size_t sz)
{
	struct mbuf *mb = NULL;
	bool offer = true;
	bool has_video;
	bool has_data;
	int err = 0;

	if (!mf || !sdp)
		return EINVAL;

	if (mf->sdp_state != SDP_IDLE) {
		warning("mediaflow(%p): invalid sdp state %d (%s)\n",
			mf, mf->sdp_state, __func__);
	}
	mf->sdp_state = SDP_GOFF;

	mf->sdp_offerer = true;

	mediaflow_set_ice_role(mf, ICE_ROLE_CONTROLLING);

	/* for debugging */
	sdp_session_set_lattr(mf->sdp, true,
			      offer ? "x-OFFER" : "x-ANSWER", NULL);

	has_video = mf->video.sdpm && !mf->video.disabled;
	has_data = mf->data.sdpm != NULL;
	/* Setup the bundle, depending on usage of video or data */
	if (has_video && has_data) {
		sdp_session_set_lattr(mf->sdp, true,
				      "group", "BUNDLE audio video data");
	}
	else if (has_video) {
		sdp_session_set_lattr(mf->sdp, true,
				      "group", "BUNDLE audio video");
	}
	else if (has_data) {
		sdp_session_set_lattr(mf->sdp, true,
				      "group", "BUNDLE audio data");
	}

	err = sdp_encode(&mb, mf->sdp, offer);
	if (err) {
		warning("mediaflow(%p): sdp encode(offer) failed (%m)\n",
			mf, err);
		goto out;
	}

	if (re_snprintf(sdp, sz, "%b", mb->buf, mb->end) < 0) {
		err = ENOMEM;
		goto out;
	}

	debug("mediaflow(%p) --- generate SDP offer ---------\n", mf);
	debug("%s", sdp);
	debug("----------------------------------------------\n");

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
		warning("mediaflow(%p): invalid sdp state (%s)\n",
			mf, __func__);
	}
	mf->sdp_state = SDP_DONE;

	mf->sdp_offerer = false;

	mediaflow_set_ice_role(mf, ICE_ROLE_CONTROLLED);

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

	debug("mediaflow(%p) -- generate SDP answer ---------\n", mf);
	debug("%s", sdp);
	debug("----------------------------------------------\n");

	mf->sent_sdp = true;

 out:
	mem_deref(mb);

	return err;
}

static bool add_extmap(const char *name, const char *value, void *arg)
{
	struct mediaflow *mf = (struct mediaflow *)arg;

	extmap_set(mf->extmap, value);
	return false;
}

/* after the SDP has been parsed,
   we can start to analyze it
   (this must be done _after_ sdp_decode() )
*/
static int post_sdp_decode(struct mediaflow *mf)
{
	const char *mid, *tool;
	int err = 0;

	if (0 == sdp_media_rport(mf->audio.sdpm)) {
		warning("mediaflow(%p): sdp medialine port is 0 - disabled\n",
			mf);
		return EPROTO;
	}

	tool = sdp_session_rattr(mf->sdp, "tool");
	if (tool) {
		str_ncpy(mf->sdp_rtool, tool, sizeof(mf->sdp_rtool));
	}

	if (mf->trice) {

		const char *rufrag, *rpwd;

		rufrag = sdp_media_session_rattr(mf->audio.sdpm, mf->sdp,
						 "ice-ufrag");
		rpwd   = sdp_media_session_rattr(mf->audio.sdpm, mf->sdp,
						 "ice-pwd");
		if (!rufrag || !rpwd) {
			warning("mediaflow(%p): post_sdp_decode: no remote"
				" ice-ufrag/ice-pwd\n", mf);
			warning("%H\n", sdp_session_debug, mf->sdp);
		}

		err |= trice_set_remote_ufrag(mf->trice, rufrag);
		err |= trice_set_remote_pwd(mf->trice, rpwd);
		if (err)
			goto out;

		if (sdp_media_rattr(mf->audio.sdpm, "end-of-candidates"))
			mf->ice_remote_eoc = true;
	}

	mid = sdp_media_rattr(mf->audio.sdpm, "mid");
	if (mid) {
		debug("mediaflow(%p): updating mid-value to '%s'\n", mf, mid);
		sdp_media_set_lattr(mf->audio.sdpm, true, "mid", mid);
	}

	if (!sdp_media_rattr(mf->audio.sdpm, "rtcp-mux")) {
		warning("mediaflow(%p): no 'rtcp-mux' attribute in SDP"
			" -- rejecting\n", mf);
		err = EPROTO;
		goto out;
	}

	sdp_media_rattr_apply(mf->audio.sdpm, "extmap", add_extmap, mf);

	if (mf->video.sdpm) {
		const char *group;

		mid = sdp_media_rattr(mf->video.sdpm, "mid");
		if (mid) {
			debug("mediaflow(%p): updating video mid-value "
			      "to '%s'\n", mf, mid);
			sdp_media_set_lattr(mf->video.sdpm,
					    true, "mid", mid);
		}

		group = sdp_session_rattr(mf->sdp, "group");
		if (group) {
			sdp_session_set_lattr(mf->sdp, true, "group", group);
		}
		sdp_media_rattr_apply(mf->video.sdpm, "extmap", add_extmap, mf);
	}

	if (mf->data.sdpm) {
		mid = sdp_media_rattr(mf->data.sdpm, "mid");
		if (mid) {
			debug("mediaflow(%p): updating data mid-value "
			      "to '%s'\n", mf, mid);
			sdp_media_set_lattr(mf->data.sdpm,
					    true, "mid", mid);
		}
		sdp_media_rattr_apply(mf->data.sdpm, "extmap", add_extmap, mf);
	}
    
	if (sdp_media_session_rattr(mf->audio.sdpm, mf->sdp, "ice-lite")) {
		info("mediaflow(%p): remote side is ice-lite"
		     " -- force controlling\n", mf);
		mediaflow_set_ice_role(mf, ICE_ROLE_CONTROLLING);
	}

	/*
	 * Handle negotiation about a common crypto-type
	 */

	mf->cryptos_remote = 0;
	if (sdp_media_session_rattr(mf->audio.sdpm, mf->sdp, "fingerprint")) {

		mf->cryptos_remote |= CRYPTO_DTLS_SRTP;
	}

	if (sdp_media_rattr(mf->audio.sdpm, "crypto")) {

		warning("mediaflow(%p): remote peer supports SDESC\n", mf);
	}

	if (sdp_media_rattr(mf->audio.sdpm, "x-KASEv1")) {

		mf->cryptos_remote |= CRYPTO_KASE;
	}

	mf->crypto = mf->cryptos_local & mf->cryptos_remote;

	info("mediaflow(%p): negotiated crypto = %s\n",
	     mf, crypto_name(mf->crypto));

	if (mf->cryptos_local && !mf->cryptos_remote) {
		warning("mediaflow(%p): we offered crypto, but got none\n", mf);
		return EPROTO;
	}

	/* check for a common crypto here, reject if nothing in common
	 */
	if (mf->cryptos_local && mf->cryptos_remote) {

		if (!mf->crypto) {

			warning("mediaflow(%p): no common crypto in SDP offer"
				" -- rejecting\n", mf);
			err = EPROTO;
			goto out;
		}
	}

	if (mf->crypto & CRYPTO_DTLS_SRTP &&
	    mf->crypto & CRYPTO_KASE) {

		info("mediaflow(%p): negotiated cryptos, fallback to '%s'\n",
		     mf, crypto_name(mf->crypto_fallback));
		mf->crypto = mf->crypto_fallback;
	}

	if (mf->crypto & CRYPTO_DTLS_SRTP) {

		err = handle_dtls_srtp(mf);
		if (err) {
			warning("mediaflow(%p): handle_dtls_srtp failed (%m)\n",
				mf, err);
			goto out;
		}
	}

	err = handle_setup(mf);
	if (err) {
		warning("mediaflow(%p): handle_setup failed (%m)\n", mf, err);
		return err;
	}

	if (mf->crypto & CRYPTO_KASE) {
		err = handle_kase(mf);
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
		warning("mediaflow(%p): invalid sdp state %d (%s)\n",
			mf, mf->sdp_state, __func__);
		return EPROTO;
	}
	mf->sdp_state = SDP_HOFF;

	++mf->stat.n_sdp_recv;

	mf->sdp_offerer = false;

	mediaflow_set_ice_role(mf, ICE_ROLE_CONTROLLED);

	mbo = mbuf_alloc(1024);
	if (!mbo)
		return ENOMEM;

	err = mbuf_write_str(mbo, sdp);
	if (err)
		goto out;

	mbo->pos = 0;

	debug("mediaflow(%p) -- recv SDP offer ----------\n", mf);
	debug("%s", sdp);
	debug("------------------------------------------\n");

	err = sdp_decode(mf->sdp, mbo, true);
	if (err) {
		warning("mediaflow(%p): could not parse SDP offer"
			" [%zu bytes] (%m)\n",
			mf, mbo->end, err);
		goto out;
	}

	mf->got_sdp = true;

	/* after the SDP offer has been parsed,
	   we can start to analyze it */

	err = post_sdp_decode(mf);
	if (err)
		goto out;


	if (!mf->audio.disabled) {
		start_codecs(mf);
	}

	if (sdp_media_rformat(mf->video.sdpm, NULL)) {

		info("mediaflow(%p): SDP has video enabled\n", mf);

		mf->video.has_media = true;
		start_video_codecs(mf);
	}
	else {
		info("mediaflow(%p): video is disabled\n", mf);
	}

	if (sdp_media_rformat(mf->data.sdpm, NULL)) {
		info("mediaflow(%p): SDP has data channel\n", mf);
		mf->data.has_media = true;
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
		warning("mediaflow(%p): invalid sdp state (%s)\n",
			mf, __func__);
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

	debug("mediaflow(%p) -- recv SDP answer ----------\n", mf);
	debug("%s", sdp);
	debug("------------------------------------\n");

	err = sdp_decode(mf->sdp, mb, offer);
	if (err) {
		warning("mediaflow(%p): could not parse SDP answer"
			" [%zu bytes] (%m)\n", mf, mb->end, err);
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

	if (!mf->audio.disabled) {
		start_codecs(mf);
	}

	if (sdp_media_rformat(mf->video.sdpm, NULL)) {

		info("mediaflow(%p): SDP has video enabled\n", mf);

		mf->video.has_media = true;
		start_video_codecs(mf);
	}
	else {
		info("mediaflow(%p): video is disabled\n", mf);
	}


	if (sdp_media_rformat(mf->data.sdpm, NULL)) {

		info("mediaflow(%p): SDP has data channel\n", mf);

		mf->data.has_media = true;
	}
	else {
		info("mediaflow(%p): no data channel\n", mf);
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
		warning("mediaflow(%p): send_rtp: not ready\n", mf);
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

	err = udp_send(mf->rtp, &mf->sel_pair->rcand->attr.addr, mb);
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
		warning("mediaflow(%p): send_raw_rtp(%zu bytes): not ready"
			" [ice=%d, crypto=%d]\n",
			mf, len, mf->ice_ready, mf->crypto_ready);
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

	err = udp_send(mf->rtp, &mf->sel_pair->rcand->attr.addr, mb);
	if (err)
		goto out;

 out:
	mem_deref(mb);

	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
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
		warning("mediaflow(%p): send_raw_rtcp(%zu bytes): not ready"
			" [ice=%d, crypto=%d]\n",
			mf, len, mf->ice_ready, mf->crypto_ready);
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

	err = udp_send(mf->rtp, &mf->sel_pair->rcand->attr.addr, mb);
	if (err)
		goto out;

 out:
	mem_deref(mb);

	pthread_mutex_unlock(&mf->mutex_enc);

	return err;
}


static bool rcandidate_handler(const char *name, const char *val, void *arg)
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
		warning("mediaflow(%p): rcand: trice_rcand_add failed"
			" [%J] (%m)\n",
			mf, &rcand.addr, err);
	}

 out:
	return false;
}


static void trice_estab_handler(struct ice_candpair *pair,
				const struct stun_msg *msg, void *arg)
{
	struct mediaflow *mf = arg;
	void *sock;
	int err;

	info("mediaflow(%p): ice pair established  %H\n",
	     mf, trice_candpair_debug, pair);

	/* verify local candidate */
	sock = trice_lcand_sock(mf->trice, pair->lcand);
	if (!sock) {
		warning("mediaflow(%p): estab: lcand has no sock [%H]\n",
			mf, trice_cand_print, pair->lcand);
		return;
	}

#if 0
	if (!pair->nominated) {
		warning("mediaflow(%p): ICE pair is not nominated!\n", mf);
	}
#endif

	/* We use the first pair that is working */
	if (!mf->ice_ready) {
		struct stun_attr *attr;
		struct turn_conn *conn;

		mem_deref(mf->sel_pair);
		mf->sel_pair = mem_ref(pair);

		mf->ice_ready = true;

		attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
		if (attr && !mf->peer_software) {
			(void)str_dup(&mf->peer_software, attr->v.software);
		}

		info("mediaflow(%p): trice: setting peer to %H [%s]\n",
		     mf, print_cand, pair->rcand,
		     mf->peer_software);

#if 1
		// TODO: extra for PRFLX
		udp_handler_set(pair->lcand->us, trice_udp_recv_handler, mf);
#endif


		/* add TURN channel */
		conn = turnconn_find_allocated(&mf->turnconnl,
					       IPPROTO_UDP);
		if (conn && AF_INET == sa_af(&pair->rcand->attr.addr)) {

			info("mediaflow(%p): adding TURN channel to %J\n",
			     mf, &pair->rcand->attr.addr);

			err = turnconn_add_channel(conn,
						   &pair->rcand->attr.addr);
			if (err) {
				warning("mediaflow(%p): could not add TURN"
					" channel (%m)\n", mf, err);
			}
		}

		ice_established_handler(mf, &pair->rcand->attr.addr);
	}
}


static bool all_failed(const struct list *lst)
{
	struct le *le;

	if (list_isempty(lst))
		return false;

	for (le = list_head(lst); le; le = le->next) {

		struct ice_candpair *pair = le->data;

		if (pair->state != ICE_CANDPAIR_FAILED)
			return false;
	}

	return true;
}


static void trice_failed_handler(int err, uint16_t scode,
				 struct ice_candpair *pair, void *arg)
{
	struct mediaflow *mf = arg;

	info("mediaflow(%p): candpair not working [%H]\n",
	     mf, trice_candpair_debug, pair);


	if (!list_isempty(trice_validl(mf->trice)))
		return;

	if (all_failed(trice_checkl(mf->trice))) {

		int to = (int)(tmr_jiffies() - mf->ts_nat_start);

		warning("mediaflow(%p): all pairs failed"
			" after %d milliseconds"
			" (checklist=%u, validlist=%u)\n",
			mf, to,
			list_count(trice_checkl(mf->trice)),
			list_count(trice_validl(mf->trice))
			);

		mf->ice_ready = false;
		mf->err = EPROTO;

		tmr_start(&mf->tmr_error, 0, tmr_error_handler, mf);
	}
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
	int err;

	if (!mf)
		return EINVAL;

	MAGIC_CHECK(mf);

	mf->ts_nat_start = tmr_jiffies();

	sdp_media_rattr_apply(mf->audio.sdpm, "candidate",
			      rcandidate_handler, mf);

	/* add permission for ALL TURN-Clients */
	for (le = mf->turnconnl.head; le; le = le->next) {
		struct turn_conn *conn = le->data;

		if (conn->turnc && conn->turn_allocated) {
			add_permission_to_remotes_ds(mf, conn);
		}
	}

	info("mediaflow(%p): start_ice: starting ICE checklist with"
	     " %u remote candidates\n",
	     mf, list_count(trice_rcandl(mf->trice)));

	err = trice_checklist_start(mf->trice, mf->trice_stun,
				    ICE_INTERVAL,
				    trice_estab_handler,
				    trice_failed_handler,
				    mf);
	if (err) {
		warning("mediaflow(%p): could not start ICE checklist (%m)\n",
			mf, err);
		return err;
	}

	return 0;
}


static int start_audio(struct mediaflow *mf)
{
	const struct aucodec *ac;
	int err = 0;

	if (mf->aes == NULL)
		return ENOSYS;

	regen_lssrc(mf);

	ac = auenc_get(mf->aes);
	if (ac && ac->enc_start) {
		struct aucodec_param prm;

		debug("mediaflow(%p): local SSRC is %u\n",
		      mf, mf->lssrcv[MEDIA_AUDIO]);

		memset(&prm, 0, sizeof(prm));
		prm.local_ssrc = mf->lssrcv[MEDIA_AUDIO];
		err = ac->enc_start(mf->aes, mf->audio.local_cbr,
				    &prm, &mf->mctx);
	}

	ac = audec_get(mf->ads);
	if (ac && ac->dec_start)
		err |= ac->dec_start(mf->ads, &mf->mctx);

	return err;
}


static int stop_audio(struct mediaflow *mf)
{
	const struct aucodec *ac;

	if (!mf)
		return EINVAL;

	/* audio */
	mf->mctx = NULL;	
	ac = auenc_get(mf->aes);
	if (ac && ac->enc_stop)
		ac->enc_stop(mf->aes);

	ac = audec_get(mf->ads);
	if (ac && ac->get_stats)
		ac->get_stats(mf->ads, &mf->codec_stats);
	if (ac && ac->dec_stop)
		ac->dec_stop(mf->ads);


	return 0;
}


static int hold_video(struct mediaflow *mf, bool hold)
{
	const struct vidcodec *vc;

	vc = viddec_get(mf->video.vds);
	if (vc && vc->dec_holdh) {
		info("mediaflow(%p): hold_media: holding"
		     " video decoder (%s)\n", mf, vc->name);

		vc->dec_holdh(mf->video.vds, hold);
	}

	vc = videnc_get(mf->video.ves);
	if (vc && vc->enc_holdh) {
		info("mediaflow(%p): hold_media: holding"
		     " video encoder (%s)\n", mf, vc->name);

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

	if (!mf->audio.disabled) {
		err = start_audio(mf);
		if (err) {
			return err;
		}
	}

	if (mf->video.has_media) {
		const struct vidcodec *vc;

		vc = viddec_get(mf->video.vds);
		if (vc && vc->dec_starth) {
			info("mediaflow(%p): start_media: starting"
			     " video decoder (%s)\n", mf, vc->name);

			err = vc->dec_starth(mf->video.vds, mf->userid_remote);
			if (err) {
				warning("mediaflow(%p): could not start"
					" video decoder (%m)\n", mf, err);
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
	int err = 0;

	if (!mf->video.has_media) {
		return ENODEV;
	}

	if (video_active) {
		vc = videnc_get(mf->video.ves);
		if (vc && vc->enc_starth) {
			info("mediaflow(%p): start_media: starting"
			     " video encoder (%s)\n", mf, vc->name);

			err = vc->enc_starth(mf->video.ves, mf->group_mode);
			if (err) {
				warning("mediaflow(%p): could not start"
					" video encoder (%m)\n", mf, err);
				return err;
			}
			mf->video.started = true;
		}
	}
	else {

		vc = videnc_get(mf->video.ves);
		if (vc && vc->enc_stoph) {
			info("mediaflow(%p): stop_media: stopping"
			     " video encoder (%s)\n", mf, vc->name);
			vc->enc_stoph(mf->video.ves);
		}
		mf->video.started = false;
	}

	return err;
}


bool mediaflow_is_sending_video(struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->video.started;
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
	mf->mctx = NULL;	
	ac = auenc_get(mf->aes);
	if (ac && ac->enc_stop)
		ac->enc_stop(mf->aes);

	ac = audec_get(mf->ads);
	if (ac && ac->get_stats)
		ac->get_stats(mf->ads, &mf->codec_stats);
	if (ac && ac->dec_stop)
		ac->dec_stop(mf->ads);

	/* video */
	vc = videnc_get(mf->video.ves);
	if (vc && vc->enc_stoph) {
		info("mediaflow(%p): stop_media: stopping"
		     " video encoder (%s)\n", mf, vc->name);
		vc->enc_stoph(mf->video.ves);
	}

	vc = viddec_get(mf->video.vds);
	if (vc && vc->dec_stoph) {
		info("mediaflow(%p): stop_media: stopping"
		     " video decoder (%s)\n", mf, vc->name);
		vc->dec_stoph(mf->video.vds);
	}

	tmr_cancel(&mf->tmr_rtp);
	mf->sent_rtp = false;
	mf->got_rtp = false;

	if (mf->stoppedh)
		mf->stoppedh(mf->arg);
}

void mediaflow_reset_media(struct mediaflow *mf)
{
	void *p;
	
	if (!mf)
		return;

	mf->mctx = NULL;
	
	p = mf->ads;
	mf->ads = NULL;
	mem_deref(p);

	p = mf->aes;
	mf->aes = NULL;
	mem_deref(p);


	p = mf->video.ves;
	mf->video.ves = NULL;
	mem_deref(p);

	p = mf->video.vds;
	mf->video.vds = NULL;
	mem_deref(p);

	mf->video.mctx = NULL;
}


static uint32_t calc_prio(enum ice_cand_type type, int af,
			  int turn_proto, bool turn_secure)
{
	uint16_t lpref = 0;

	switch (turn_proto) {

	case IPPROTO_UDP:
		lpref = 3;
		break;

	case IPPROTO_TCP:
		if (turn_secure)
			lpref = 1;
		else
			lpref = 2;
		break;
	}

	return ice_cand_calc_prio(type, lpref, ICE_COMPID_RTP);
}


static void submit_local_candidate(struct mediaflow *mf,
				   enum ice_cand_type type,
				   const struct sa *addr,
				   const struct sa *rel_addr, bool eoc,
				   int turn_proto, bool turn_secure,
				   void **sockp, struct udp_sock *sock)
{
	struct ice_cand_attr attr = {
		.foundation = "1",  /* NOTE: same foundation for all */
		.compid     = ICE_COMPID_RTP,
		.proto      = IPPROTO_UDP,
		.prio       = 0,
		.addr       = *addr,
		.type       = type,
		.tcptype    = 0,
	};
	char cand[512];
	struct ice_lcand *lcand;
	int err;
	bool add;

	switch (type) {

	case ICE_CAND_TYPE_RELAY:
		add = true;
		break;

	default:
		add = !mf->privacy_mode;
		break;
	}

	if (!add) {
		debug("mediaflow(%p): NOT adding cand %s (privacy mode)\n",
		      mf, ice_cand_type2name(type));
		return;
	}

	attr.prio = calc_prio(type, sa_af(addr),
			      turn_proto, turn_secure);

	if (turn_proto == IPPROTO_UDP) {

	}
	else
		sock = NULL;


	err = trice_lcand_add(&lcand, mf->trice, attr.compid,
			      attr.proto, attr.prio, addr, NULL,
			      attr.type, rel_addr,
			      0 /* tcptype */,
			      sock, LAYER_ICE);
	if (err) {
		warning("mediaflow(%p): add local cand failed (%m)\n",
			mf, err);
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

	if (add) {
		err = sdp_media_set_lattr(mf->audio.sdpm, false,
					  "candidate",
					  "%H",
					  ice_cand_attr_encode, lcand);
		if (err)
			return;
	}
}


static void gather_stun_resp_handler(int err, uint16_t scode,
				     const char *reason,
				     const struct stun_msg *msg, void *arg)
{
	struct mediaflow *mf = arg;
	struct stun_attr *map = NULL, *attr;

	if (err) {
		warning("mediaflow(%p): stun_resp %m\n", mf, err);
		goto error;
	}

	if (scode) {
		warning("mediaflow(%p): stun_resp %u %s\n", mf, scode, reason);
		goto error;
	}

	map = stun_msg_attr(msg, STUN_ATTR_XOR_MAPPED_ADDR);
	if (!map) {
		warning("mediaflow(%p): xor_mapped_addr attr missing\n", mf);
		goto error;
	}

	mf->stun_ok = true;

	attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
	info("mediaflow(%p): STUN allocation OK"
	     " (mapped=%J) [%s]\n",
	     mf,
	     &map->v.xor_mapped_addr,
	     attr ? attr->v.software : "");

	submit_local_candidate(mf, ICE_CAND_TYPE_SRFLX,
			       &map->v.xor_mapped_addr, &mf->laddr_default,
			       true, IPPROTO_UDP, false, NULL, mf->us_stun);

	mf->ice_local_eoc = true;
	sdp_media_set_lattr(mf->audio.sdpm, true, "end-of-candidates", NULL);

	if (mf->gatherh)
		mf->gatherh(mf->arg);

	return;

 error:
	/* NOTE: only flag an error if ICE is not established yet */
	if (!mf->ice_ready)
		ice_error(mf, err ? err : EPROTO);
}


// TODO: should be done PER interface
int mediaflow_gather_stun(struct mediaflow *mf, const struct sa *stun_srv)
{
	struct stun *stun = NULL;
	struct sa laddr;
	void *sock = NULL;
	int err;

	if (!mf || !stun_srv)
		return EINVAL;

	if (mf->ct_gather)
		return EALREADY;

	sa_init(&laddr, sa_af(stun_srv));

	if (!mf->trice)
		return EINVAL;

	err = udp_listen(&mf->us_stun, &laddr,
			 stun_udp_recv_handler, mf);
	if (err)
		return err;

	stun = mf->trice_stun;
	sock = mf->us_stun;

	if (!stun || !sock) {
		warning("mediaflow(%p): gather_stun: no STUN/SOCK instance\n",
			mf);
		return EINVAL;
	}

	err = stun_request(&mf->ct_gather, stun, IPPROTO_UDP,
			   sock, stun_srv, 0,
			   STUN_METHOD_BINDING, NULL, 0, false,
			   gather_stun_resp_handler, mf, 0);
	if (err) {
		warning("mediaflow(%p): stun_request failed (%m)\n", mf, err);
		return err;
	}

	mf->stun_server = true;

	return 0;
}


static void add_turn_permission(struct mediaflow *mf,
				struct turn_conn *conn,
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
		info("mediaflow(%p): adding TURN permission"
		     " to remote address %s.%j <turnconn=%p>\n",
		     mf,
		     ice_cand_type2name(rcand->type),
		     &rcand->addr, conn);

		err = turnconn_add_permission(conn, &rcand->addr);
		if (err) {
			warning("mediaflow(%p): failed to"
				" add permission (%m)\n",
				mf, err);
		}
	}
}


static void add_permissions(struct mediaflow *mf, struct turn_conn *conn)
{
	struct le *le;

	for (le = list_head(trice_rcandl(mf->trice)); le; le = le->next) {

		struct ice_rcand *rcand = le->data;

		add_turn_permission(mf, conn, &rcand->attr);
	}
}


static void add_permission_to_remotes(struct mediaflow *mf)
{
	struct turn_conn *conn;
	struct le *le;

	if (!mf)
		return;

	if (!mf->trice)
		return;

	for (le = mf->turnconnl.head; le; le = le->next) {

		conn = le->data;

		if (conn->turn_allocated)
			add_permissions(mf, conn);
	}
}


static void add_permission_to_remotes_ds(struct mediaflow *mf,
					 struct turn_conn *conn)
{
	struct le *le;

	if (!mf->trice)
		return;

	for (le = list_head(trice_rcandl(mf->trice)); le; le = le->next) {

		struct ice_rcand *rcand = le->data;

		add_turn_permission(mf, conn, &rcand->attr);
	}
}


/* all outgoing UDP-packets must be sent via
 * the TCP-connection to the TURN server
 */
static bool turntcp_send_handler(int *err, struct sa *dst,
				 struct mbuf *mb, void *arg)
{
	struct turn_conn *tc = arg;

	*err = turnc_send(tc->turnc, dst, mb);
	if (*err) {
		re_printf("mediaflow: turnc_send failed (%zu bytes to %J)\n",
			mbuf_get_left(mb), dst);
	}

	return true;
}


static void turnconn_estab_handler(struct turn_conn *conn,
				   const struct sa *relay_addr,
				   const struct sa *mapped_addr,
				   const struct stun_msg *msg, void *arg)
{
	struct mediaflow *mf = arg;
	void *sock = NULL;
	int err;
	(void)msg;

	info("mediaflow(%p): TURN-%s established (%J)\n",
	     mf, turnconn_proto_name(conn), relay_addr);

	if (mf->mf_stats.turn_alloc < 0 &&
	    conn->ts_turn_resp &&
	    conn->ts_turn_req) {

		mf->mf_stats.turn_alloc = conn->ts_turn_resp
			- conn->ts_turn_req;
	}

#if 0
	if (0) {

		sdp_media_set_laddr(mf->audio.sdpm, relay_addr);
		sdp_media_set_laddr(mf->video.sdpm, relay_addr);

		add_permission_to_relays(mf, conn);
	}
#endif

	/* NOTE: important to ship the SRFLX before RELAY cand. */

	if (conn->proto == IPPROTO_UDP) {
		submit_local_candidate(mf, ICE_CAND_TYPE_SRFLX,
				       mapped_addr, &mf->laddr_default, false,
				       conn->proto, conn->secure, NULL,
				       conn->us_turn);
	}

	submit_local_candidate(mf, ICE_CAND_TYPE_RELAY,
			       relay_addr, mapped_addr, true,
			       conn->proto, conn->secure, &sock,
			       conn->us_turn);

	if (conn->proto == IPPROTO_TCP) {
		/* NOTE: this is needed to snap up outgoing UDP-packets */
		conn->us_app = mem_ref(sock);
		err = udp_register_helper(&conn->uh_app, sock, LAYER_TURN,
					  turntcp_send_handler, NULL, conn);
		if (err) {
			warning("mediaflow(%p): TURN failed to register "
				"UDP-helper (%m)\n", mf, err);
			goto error;
		}
	}

	mf->ice_local_eoc = true;
	sdp_media_set_lattr(mf->audio.sdpm, true, "end-of-candidates", NULL);

	add_permission_to_remotes_ds(mf, conn);
	add_permission_to_remotes(mf);

	/* NOTE: must be called last, since app might deref mediaflow */
	if (mf->gatherh)
		mf->gatherh(mf->arg);


	return;

 error:
	/* NOTE: only flag an error if ICE is not established yet */
	if (!mf->ice_ready)
		ice_error(mf, err ? err : EPROTO);
}


/* incoming packets over TURN - demultiplex to the right module */
static void turnconn_data_handler(struct turn_conn *conn, const struct sa *src,
				  struct mbuf *mb, void *arg)
{
	struct mediaflow *mf = arg;
	struct ice_lcand *lcand;
	enum packet pkt;

	pkt = packet_classify_packet_type(mb);

	if (pkt == PACKET_STUN) {

		debug("mediaflow(%p): incoming STUN-packet via TURN\n", mf);

		// TODO: this supports only one TURN-client for now
		//       add support for multiple clients
		lcand = trice_lcand_find2(mf->trice,
					  ICE_CAND_TYPE_RELAY, sa_af(src));
		if (lcand) {

			/* forward packet to ICE */
			trice_lcand_recv_packet(lcand, src, mb);
		}
		else {
			debug("mediaflow(%p): turnconn: no local candidate\n",
			      mf);
			demux_packet(mf, src, mb);
		}
	}
	else {
		demux_packet(mf, src, mb);
	}
}


static void turnconn_error_handler(int err, void *arg)
{
	struct mediaflow *mf = arg;
	bool one_allocated;
	bool all_failed;

	one_allocated = turnconn_is_one_allocated(&mf->turnconnl);
	all_failed = turnconn_are_all_failed(&mf->turnconnl);

	warning("mediaflow(%p): turnconn_error:  turnconnl=%u"
		"  [one_allocated=%d, all_failed=%d]  (%m)\n",
		mf, list_count(&mf->turnconnl), one_allocated, all_failed, err);

	if (all_failed)
		goto fail;

	if (list_count(&mf->turnconnl) > 1 ||
	    one_allocated) {

		info("mediaflow(%p): ignoring turn error, already have 1\n",
		     mf);
		return;
	}

 fail:
	/* NOTE: only flag an error if ICE is not established yet */
	if (!mf->ice_ready)
		ice_error(mf, err ? err : EPROTO);
}


#if 0
static struct interface *default_interface(const struct mediaflow *mf)
{
	struct le *le;

	for (le = mf->interfacel.head; le; le = le->next) {
		struct interface *ifc = le->data;

		if (ifc->is_default)
			return ifc;
	}

	/* default not found, just return first one */
	return list_ledata(list_head(&mf->interfacel));
}
#endif

/*
 * Gather RELAY and SRFLX candidates (UDP only)
 */
int mediaflow_gather_turn(struct mediaflow *mf, const struct sa *turn_srv,
			  const char *username, const char *password)
{
	struct interface *ifc;
	struct sa turn_srv6;
	void *sock = NULL;
	int err;

	(void)ifc;
	
	if (!mf || !turn_srv)
		return EINVAL;

	info("mediaflow(%p): gather_turn: %J(UDP)\n", mf, turn_srv);
	
	if (!sa_isset(turn_srv, SA_ALL)) {
		warning("mediaflow(%p): gather_turn: no TURN server\n", mf);
		return EINVAL;
	}


	if (!mf->trice)
		return EINVAL;

	/* NOTE: this should only be done if we detect that
	 *       we are behind a NAT64
	 */
	if (mf->af != sa_af(turn_srv)) {

		err = sa_translate_nat64(&turn_srv6, turn_srv);
		if (err) {
			warning("mediaflow(%p): gather_turn: "
				"sa_translate_nat64(%j) failed (%m)\n",
				mf, turn_srv, err);
			return err;
		}

		info("mediaflow(%p): Dualstack: TRANSLATE NAT64"
		     " (%J ----> %J)\n",
		     mf, turn_srv, &turn_srv6);

		turn_srv = &turn_srv6;
	}

#if 0
	/* Reuse UDP-sockets from HOST interface */
	ifc = default_interface(mf);
	if (ifc) {
		info("mediaflow(%p): gather_turn: default interface"
		     " is %s|%j (lcand=%p)\n",
		     mf, ifc->ifname, &ifc->addr, ifc->lcand);

		if (ifc->lcand)
			sock = ifc->lcand->us;
	}
#endif

	info("mediaflow(%p): gather_turn: %J\n", mf, turn_srv);

	err = turnconn_alloc(NULL, &mf->turnconnl,
			     turn_srv, IPPROTO_UDP, false,
			     username, password,
			     mf->af, sock,
			     LAYER_STUN, LAYER_TURN,
			     turnconn_estab_handler,
			     turnconn_data_handler,
			     turnconn_error_handler, mf);
	if (err) {
		warning("mediaflow(%p): turnc_alloc failed (%m)\n", mf, err);
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

	info("mediaflow(%p): gather_turn_tcp: %J(secure=%d)\n",
	     mf, turn_srv, secure);

	err = turnconn_alloc(&tc, &mf->turnconnl,
			     turn_srv, IPPROTO_TCP, secure,
			     username, password,
			     mf->af, NULL,
			     LAYER_STUN, LAYER_TURN,
			     turnconn_estab_handler,
			     turnconn_data_handler,
			     turnconn_error_handler, mf
			     );
	if (err)
		return err;

	return err;
}


size_t mediaflow_remote_cand_count(const struct mediaflow *mf)
{
	if (!mf)
		return 0;

	return list_count(trice_rcandl(mf->trice));
}


void mediaflow_set_fallback_crypto(struct mediaflow *mf, enum media_crypto cry)
{
	if (!mf)
		return;

	mf->crypto_fallback = cry;
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
	if (!mf)
		return NULL;

	MAGIC_CHECK(mf);

	return mf->ads;
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
	struct ice_rcand *rcand = NULL;
	int err = 0;
	char nat_letter = ' ';

	if (!mf)
		return 0;

	nat_letter = mf->ice_ready ? 'I' : ' ';

	if (mf->sel_pair)
		rcand = mf->sel_pair->rcand;

	err = re_hprintf(pf, "%c%c%c%c%c ice=%s-%s.%J [%s] tx=%zu rx=%zu",
			 mf->got_sdp ? 'S' : ' ',
			 nat_letter,
			 mf->crypto_ready ? 'D' : ' ',
			 mediaflow_is_rtpstarted(mf) ? 'R' : ' ',
			 mf->data.ready ? 'C' : ' ',
			 mediaflow_lcand_name(mf),
			 rcand ? ice_cand_type2name(rcand->attr.type) : "?",
			 rcand ? &rcand->attr.addr : NULL,
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


bool mediaflow_has_data(const struct mediaflow *mf)
{
	return mf ? mf->data.sdpm != NULL : false;
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


bool mediaflow_is_gathered(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	debug("mediaflow(%p): is_gathered:  turnconnl=%u/%u  stun=%d/%d\n",
	      mf,
	      mf->turnc, list_count(&mf->turnconnl),
	      mf->stun_server, mf->stun_ok);

	if (!list_isempty(&mf->turnconnl))
		return turnconn_is_one_allocated(&mf->turnconnl);

	if (mf->stun_server)
		return mf->stun_ok;

	if (mf->turnc)
		return false;

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

	sdpm = type == MEDIA_AUDIO ? mf->audio.sdpm : mf->video.sdpm;

	rssrc = sdp_media_rattr(sdpm, "ssrc");
	if (!rssrc)
		return ENOENT;

	err = re_regex(rssrc, str_len(rssrc), "[0-9]+", &pl_ssrc);
	if (err)
		return err;

	*ssrcp = pl_u32(&pl_ssrc);

	return 0;
}


bool mediaflow_dtls_ready(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->crypto_ready;
}


bool mediaflow_ice_ready(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->ice_ready;
}


const struct rtp_stats* mediaflow_rcv_audio_rtp_stats(const struct mediaflow *mf)
{
	if (!mf)
		return NULL;

	return &mf->audio_stats_rcv;
}


const struct rtp_stats* mediaflow_snd_audio_rtp_stats(const struct mediaflow *mf)
{
	if (!mf)
		return NULL;

	return &mf->audio_stats_snd;
}


const struct rtp_stats* mediaflow_rcv_video_rtp_stats(const struct mediaflow *mf)
{
	if (!mf)
		return NULL;

	return &mf->video_stats_rcv;
}


const struct rtp_stats* mediaflow_snd_video_rtp_stats(const struct mediaflow *mf)
{
	if (!mf)
		return NULL;

	return &mf->video_stats_snd;
}


struct aucodec_stats *mediaflow_codec_stats(struct mediaflow *mf)
{
	const struct aucodec *ac;

	if (!mf)
		return NULL;

	ac = audec_get(mf->ads);
	if (ac && ac->get_stats)
		ac->get_stats(mf->ads, &mf->codec_stats);

	return &mf->codec_stats;
}


const struct mediaflow_stats *mediaflow_stats_get(const struct mediaflow *mf)
{
	return mf ? &mf->mf_stats : NULL;
}

int32_t mediaflow_get_media_time(const struct mediaflow *mf)
{
	if (!mf)
		return -1;
    
	int dur_rx = (mf->stat.rx.ts_last - mf->stat.rx.ts_first);
    
	return dur_rx;
}

void mediaflow_set_local_eoc(struct mediaflow *mf)
{
	if (!mf)
		return;

	mf->ice_local_eoc = true;
	sdp_media_set_lattr(mf->audio.sdpm, true, "end-of-candidates", NULL);
}


bool mediaflow_have_eoc(const struct mediaflow *mf)
{
	if (!mf)
		return false;

	return mf->ice_local_eoc && mf->ice_remote_eoc;
}


void mediaflow_enable_privacy(struct mediaflow *mf, bool enabled)
{
	if (!mf)
		return;

	mf->privacy_mode = enabled;

	trice_conf(mf->trice)->enable_prflx = !enabled;
}


void mediaflow_enable_group_mode(struct mediaflow *mf, bool enabled)
{
	if (!mf)
		return;

	mf->group_mode = enabled;

	if (mf->group_mode) {
		sdp_media_set_lattr(mf->audio.sdpm, true, "ptime", "%u", GROUP_PTIME);
	}
	else {
		sdp_media_del_lattr(mf->audio.sdpm, "ptime");
	}
}


const char *mediaflow_lcand_name(const struct mediaflow *mf)
{
	struct ice_lcand *lcand;

	if (!mf)
		return NULL;

	if (!mf->sel_pair)
		return "???";

	lcand = mf->sel_pair->lcand;

	if (lcand)
		return ice_cand_type2name(lcand->attr.type);
	else
		return "???";
}


const char *mediaflow_rcand_name(const struct mediaflow *mf)
{
	if (!mf)
		return NULL;

	if (!mf->sel_pair)
		return "???";

	return ice_cand_type2name(mf->sel_pair->rcand->attr.type);
}


struct dce *mediaflow_get_dce(const struct mediaflow *mf)
{
	if (!mf || !mf->data.dce)
		return NULL;
    
	return mf->data.dce;
}


uint32_t mediaflow_candc(const struct mediaflow *mf, bool local,
			 enum ice_cand_type typ)
{
	struct list *lst;
	struct le *le;
	uint32_t n = 0;

	lst = local ? trice_lcandl(mf->trice) : trice_rcandl(mf->trice);

	for (le = list_head(lst);le;le=le->next) {
		struct ice_cand_attr *cand = le->data;

		if (typ == cand->type)
			++n;
	}

	return n;
}

void mediaflow_set_audio_cbr(struct mediaflow *mf, bool enabled)
{
	struct le *le;
	
	if (!mf)
		return;

	/* If CBR is already set, do not reset mid-call */
	if (!mf->audio.local_cbr)
		mf->audio.local_cbr = enabled;

	LIST_FOREACH(&mf->audio.formatl, le) {
		struct auformat *af = le->data;
		sdp_format_set_params(af->fmt, "%s",
				      mf->audio.local_cbr ? af->ac->fmtp_cbr
				                          : af->ac->fmtp);
	}
}

bool mediaflow_get_audio_cbr(const struct mediaflow *mf, bool local)
{
	if (!mf)
		return false;
    
	return local ? mf->audio.local_cbr : mf->audio.remote_cbr;
}


/* NOTE: Remote clientid can only be set once. */
int mediaflow_set_remote_userclientid(struct mediaflow *mf,
				      const char *userid, const char *clientid) 
{
	int err = 0;

	if (!mf || !str_isset(clientid) || !str_isset(userid))
		return EINVAL;

	if (str_isset(mf->userid_remote)) {
		warning("mediaflow(%p): remote userid is already set\n", mf);
		return EALREADY;
	}

	if (str_isset(mf->clientid_remote)) {
		warning("mediaflow(%p): remote clientid is already set\n", mf);
		return EALREADY;
	}

	err = str_dup(&mf->userid_remote, userid);
	if (err) {
		goto out;
	}

	err =  str_dup(&mf->clientid_remote, clientid);
	if (err) {
		goto out;
	}

out:
	if (err) {
		mf->userid_remote = mem_deref(mf->userid_remote);
		mf->clientid_remote = mem_deref(mf->clientid_remote);
	}

	return err;
}


struct ice_candpair *mediaflow_selected_pair(const struct mediaflow *mf)
{
	return mf ? mf->sel_pair : NULL;
}


enum ice_role mediaflow_local_role(const struct mediaflow *mf)
{
	if (!mf)
		return ICE_ROLE_UNKNOWN;

	return trice_local_role(mf->trice);
}


int mediaflow_set_extcodec(struct mediaflow *mf, void *arg)
{
	if (!mf || !arg)
		return EINVAL;

	mf->extcodec.arg = arg;

	return 0;
}


void mediaflow_video_set_disabled(struct mediaflow *mf, bool dis)
{
	if (!mf)
		return;

	mf->video.disabled = dis;
	sdp_media_set_disabled(mf->video.sdpm, dis);
}


static void gather_turn(struct mediaflow *mf,
			struct zapi_ice_server *turn,
			const struct sa *srv,
			int proto,
			bool secure)
{
	int err = 0;
	
	switch (proto) {
	case IPPROTO_UDP:
		if (secure) {
			warning("mediaflow(%p): secure UDP not supported\n",
				mf);
		}
		err = mediaflow_gather_turn(mf, srv,
					    turn->username, turn->credential);
		if (err) {
			warning("mediaflow(%p): gather_turn: failed (%m)\n",
				mf, err);
			goto out;
		}
		break;

	case IPPROTO_TCP:
		err = mediaflow_gather_turn_tcp(mf, srv,
						turn->username,
						turn->credential,
						secure);
		if (err) {
			warning("mediaflow(%p): gather_turn_tcp: failed (%m)\n",
				mf, err);
			goto out;
		}
		break;

	default:
		warning("mediaflow(%p): unknown protocol (%d)\n",
			mf, proto);
		break;
	}

 out:
	return;
}


static void dns_handler(int dns_err, const struct sa *srv, void *arg)
{
	struct lookup_entry *lent = arg;
	struct mediaflow *mf = lent->mf;
	struct sa turn_srv;
	char addr[32];
	size_t i;

	sa_cpy(&turn_srv, srv);
	sa_set_port(&turn_srv, lent->port);

	re_snprintf(addr, sizeof(addr), "%J", &turn_srv);
	for (i = 0; i < strlen(addr); ++i) {
		if (addr[i] == '.')
			addr[i] = '-';
	}
	
	info("mediaflow(%p): dns_handler: err=%d [%s] -> [%s] took: %dms\n",
	     mf, dns_err, lent->host, addr,
	     (int)(tmr_jiffies() - lent->ts));

	if (dns_err)
		goto out;

	gather_turn(mf, &lent->turn, &turn_srv, lent->proto, lent->secure);
	
 out:
	mem_deref(lent);
}


int mediaflow_add_turnserver(struct mediaflow *mf,
			     struct zapi_ice_server *turn)
{
	if (!mf || !turn)
		return EINVAL;

	if (mf->turnc >= ARRAY_SIZE(mf->turnv))
		return EOVERFLOW;

	info("mediaflow(%p): adding turn: %s\n", mf, turn->url);
	
	mf->turnv[mf->turnc] = *turn;
	++mf->turnc;

	return 0;
}


static void lent_destructor(void *arg)
{
	struct lookup_entry *lent = arg;

	mem_deref(lent->host);
	mem_deref(lent->mf);
}


static int turn_dns_lookup(struct mediaflow *mf,
			   struct zapi_ice_server *turn,
			   struct stun_uri *uri)
{
	struct lookup_entry *lent;
	int err = 0;

	lent = mem_zalloc(sizeof(*lent), lent_destructor);
	if (!lent)
		return ENOMEM;

	lent->mf = mem_ref(mf);
	lent->turn = *turn;
	lent->ts = tmr_jiffies();
	lent->proto = uri->proto;
	lent->secure = uri->secure;
	lent->port = uri->port;
	err = str_dup(&lent->host, uri->host);
	if (err)
		goto out;

	info("mediaflow(%p): dns_lookup for: %s:%d\n",
	     mf, lent->host, lent->port);
	
	err = dns_lookup(lent->host, dns_handler, lent);
	if (err) {
		warning("mediaflow(%p): dns_lookup failed\n", mf);
		goto out;
	}
 out:
	if (err)
		mem_deref(lent);

	return err;
}


int mediaflow_gather_all_turn(struct mediaflow *mf)
{
	size_t i;
	
	if (!mf)
		return EINVAL;
	
	for (i = 0; i < mf->turnc; ++i) {
		struct stun_uri uri;
		struct zapi_ice_server *turn;
		int err;

		turn = &mf->turnv[i];
	
		err = stun_uri_decode(&uri, turn->url);
		if (err) {
			info("mediaflow(%p): resolving turn uri (%s)\n",
			     mf, turn->url);

			turn_dns_lookup(mf, turn, &uri);
			continue;
		}

		if (STUN_SCHEME_TURN != uri.scheme) {
			warning("mediaflow(%p): ignoring scheme %d\n",
				mf, uri.scheme);
			continue;
		}

		gather_turn(mf, turn, &uri.addr, uri.proto, uri.secure);
	}

	return 0;
}

