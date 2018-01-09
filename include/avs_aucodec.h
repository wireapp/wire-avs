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

struct aucodec_param {
	uint32_t local_ssrc;
	uint32_t remote_ssrc;
	uint8_t  pt;
	uint32_t srate;
	uint8_t  ch;
	bool cbr;
};

struct media_ctx;
struct auenc_state;
struct audec_state;
struct aucodec;
struct mediaflow;
struct aucodec_stats;


typedef void (auenc_err_h)(int err, const char *msg, void *arg);

typedef int  (auenc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (auenc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef int  (auenc_alloc_h)(struct auenc_state **aesp,
			     const struct aucodec *ac, const char *fmtp,
			     struct aucodec_param *prm,
			     auenc_rtp_h *rtph,
			     auenc_rtcp_h *rtcph,
			     auenc_err_h *errh,
			     void *extcodec_arg,
			     void *arg);

typedef int  (auenc_start_h)(struct auenc_state *aes,
			     bool cbr,
			     const struct aucodec_param *prm,
			     struct media_ctx **mctxp);
typedef void (auenc_stop_h)(struct auenc_state *aes);

typedef void (audec_err_h)(int err, const char *msg, void *arg);


typedef int  (audec_alloc_h)(struct audec_state **adsp,
			     const struct aucodec *ac,
			     const char *fmtp,
			     struct aucodec_param *prm,
			     audec_err_h *errh,
			     void *extcodec_arg,
			     void *arg);
typedef int  (audec_rtp_h)(struct audec_state *ads,
			   const uint8_t *pkt, size_t len);
typedef int  (audec_rtcp_h)(struct audec_state *ads,
			    const uint8_t *pkt, size_t len);
typedef int  (audec_start_h)(struct audec_state *ads, struct media_ctx **mctxp);
typedef void (audec_stop_h)(struct audec_state *ads);
typedef int  (audec_get_stats)(struct audec_state *ads, struct aucodec_stats *stats);

struct aucodec {
	struct le le;
	struct le ext_le; /* member of external codec list */
	const char *pt;
	const char *name;
	uint32_t srate;
	uint8_t ch;
	const char *fmtp;
	const char *fmtp_cbr;

	auenc_alloc_h *enc_alloc;
	auenc_start_h *enc_start;
	auenc_stop_h *enc_stop;

	audec_alloc_h *dec_alloc;
	audec_rtp_h *dec_rtph;
	audec_rtcp_h *dec_rtcph;
	audec_start_h *dec_start;
	audec_stop_h *dec_stop;
	audec_get_stats *get_stats;

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

#endif /* #ifndef AVS_AUCODEC_H */
