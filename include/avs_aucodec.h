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


/*
 * Audio codec
 */

struct media_ctx;
struct auenc_state;
struct audec_state;
struct aucodec;
struct mediaflow;


/* RTP packetization (send) handler */
typedef int (auenc_packet_h)(uint8_t pt, uint32_t ts,
			     const uint8_t *pld, size_t pld_len,
			     void *arg);

/* incoming PCM audio (recv) handler */
typedef int (audec_recv_h)(const int16_t *sampv, size_t sampc,
			   void *arg);

typedef void (auenc_err_h)(int err, const char *msg, void *arg);

typedef int  (auenc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (auenc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef int  (auenc_alloc_h)(struct auenc_state **aesp,
			     struct media_ctx **mctxp,
			     const struct aucodec *ac, const char *fmtp,
			     int pt, uint32_t srate, uint8_t ch,
			     auenc_rtp_h *rtph,
			     auenc_rtcp_h *rtcph,
			     auenc_packet_h *pkth,
			     auenc_err_h *errh,
			     void *arg);

typedef int  (auenc_encode_h)(struct auenc_state *aes,
			      const int16_t *sampv, size_t sampc);
typedef int  (auenc_start_h)(struct auenc_state *aes);
typedef void (auenc_stop_h)(struct auenc_state *aes);

typedef void (audec_err_h)(int err, const char *msg, void *arg);


typedef int  (audec_alloc_h)(struct audec_state **adsp,
			     struct media_ctx **mctxp,
			     const struct aucodec *ac,
			     const char *fmtp, int pt,
			     uint32_t srate, uint8_t ch,
			     audec_recv_h *recvh,
			     audec_err_h *errh,
			     void *arg);
typedef int  (audec_decode_h)(struct audec_state *ads,
			      const struct rtp_header *hdr,
			      const uint8_t *pld, size_t pld_len);
typedef int  (audec_rtp_h)(struct audec_state *ads,
			   const uint8_t *pkt, size_t len);
typedef int  (audec_rtcp_h)(struct audec_state *ads,
			    const uint8_t *pkt, size_t len);
typedef int  (audec_start_h)(struct audec_state *ads);
typedef int  (audec_stats_h)(struct audec_state *ads, struct mbuf **mb);
typedef void (audec_stop_h)(struct audec_state *ads);


struct aucodec {
	struct le le;
	const char *pt;
	const char *name;
	uint32_t srate;
	uint8_t ch;
	const char *fmtp;
	bool has_rtp;

	auenc_alloc_h *enc_alloc;
	auenc_encode_h *ench;
	auenc_start_h *enc_start;
	auenc_stop_h *enc_stop;

	audec_alloc_h *dec_alloc;
	audec_decode_h *dech;
	audec_rtp_h *dec_rtph;
	audec_rtcp_h *dec_rtcph;
	audec_start_h *dec_start;
	audec_stats_h *dec_stats;
	audec_stop_h *dec_stop;

	sdp_fmtp_enc_h *fmtp_ench;
	sdp_fmtp_cmp_h *fmtp_cmph;
	void *data;
};

void aucodec_register(struct list *aucodecl, struct aucodec *ac);
void aucodec_unregister(struct aucodec *ac);
const struct aucodec *aucodec_find(struct list *aucodecl,
				   const char *name, uint32_t srate,
				   uint8_t ch);

const struct aucodec *auenc_get(struct auenc_state *aes);
const struct aucodec *audec_get(struct audec_state *ads);
