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

#ifndef AVS_AUCODEC_H
#define AVS_AUCODEC_H    1

/*
 * Audio codec
 */

struct avs_aucodec_param {
	uint32_t local_ssrc;
	uint32_t remote_ssrc;
	uint8_t  pt;
	uint32_t srate;
	uint8_t  ch;
	bool cbr;
};

struct media_ctx;
struct avs_auenc_state;
struct avs_audec_state;
struct avs_aucodec;
struct mediaflow;
struct avs_aucodec_stats;


typedef void (avs_auenc_err_h)(int err, const char *msg, void *arg);

typedef int  (avs_auenc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (avs_auenc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef int  (avs_auenc_alloc_h)(struct avs_auenc_state **aesp,
				 const struct avs_aucodec *ac, const char *fmtp,
				 struct avs_aucodec_param *prm,
				 avs_auenc_rtp_h *rtph,
				 avs_auenc_rtcp_h *rtcph,
				 avs_auenc_err_h *errh,
				 void *arg);

typedef int  (avs_auenc_start_h)(struct avs_auenc_state *aes,
				 bool cbr,
				 void *extcodec_arg,
				 const struct avs_aucodec_param *prm,
				 struct media_ctx **mctxp);

typedef void (avs_auenc_stop_h)(struct avs_auenc_state *aes);

typedef void (avs_audec_err_h)(int err, const char *msg, void *arg);


typedef int  (avs_audec_alloc_h)(struct avs_audec_state **adsp,
			     const struct avs_aucodec *ac,
			     const char *fmtp,
			     struct avs_aucodec_param *prm,
			     avs_audec_err_h *errh,
			     void *arg);
typedef int  (avs_audec_rtp_h)(struct avs_audec_state *ads,
			   const uint8_t *pkt, size_t len);
typedef int  (avs_audec_rtcp_h)(struct avs_audec_state *ads,
			    const uint8_t *pkt, size_t len);
typedef int  (avs_audec_start_h)(struct avs_audec_state *ads,
			     struct media_ctx **mctxp,
			     void *extcodec_arg);
typedef void (avs_audec_stop_h)(struct avs_audec_state *ads);
typedef int  (avs_audec_get_stats)(struct avs_audec_state *ads, struct avs_aucodec_stats *stats);

struct avs_aucodec {
	struct le le;
	struct le ext_le; /* member of external codec list */
	const char *pt;
	const char *name;
	uint32_t srate;
	uint8_t ch;
	const char *fmtp;
	const char *fmtp_cbr;

	avs_auenc_alloc_h *enc_alloc;
	avs_auenc_start_h *enc_start;
	avs_auenc_stop_h *enc_stop;

	avs_audec_alloc_h *dec_alloc;
	avs_audec_rtp_h *dec_rtph;
	avs_audec_rtcp_h *dec_rtcph;
	avs_audec_start_h *dec_start;
	avs_audec_stop_h *dec_stop;
	avs_audec_get_stats *get_stats;

	sdp_fmtp_enc_h *fmtp_ench;
	sdp_fmtp_cmp_h *fmtp_cmph;
	void *data;
};

void avs_aucodec_register(struct list *aucodecl, struct avs_aucodec *ac);
void avs_aucodec_unregister(struct avs_aucodec *ac);
const struct avs_aucodec *avs_aucodec_find(struct list *aucodecl,
				       const char *name, uint32_t srate,
				       uint8_t ch);

const struct avs_aucodec *avs_auenc_get(struct avs_auenc_state *aes);
const struct avs_aucodec *avs_audec_get(struct avs_audec_state *ads);

#endif /* #ifndef AVS_AUCODEC_H */
