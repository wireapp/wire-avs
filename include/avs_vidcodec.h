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
 * Video Codec
 */


struct avs_vidcodec_param {
	uint32_t local_ssrcv[2];
	size_t local_ssrcc;

	uint32_t remote_ssrcv[4];
	size_t remote_ssrcc;
};

struct media_ctx;
struct avs_videnc_state;
struct avs_viddec_state;
struct avs_vidcodec;
struct vidframe;

typedef void (avs_videnc_err_h)(int err, const char *msg, void *arg);

typedef int  (avs_videnc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (avs_videnc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef int (avs_videnc_alloc_h)(struct avs_videnc_state **vesp,
			     struct media_ctx **mctxp,
			     const struct avs_vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     struct avs_vidcodec_param *prm,
			     avs_videnc_rtp_h *rtph,
			     avs_videnc_rtcp_h *rtcph,
			     avs_videnc_err_h *errh,
			     void *arg);


typedef int (avs_videnc_packet_h)(bool marker, const uint8_t *hdr, size_t hdr_len,
			      const uint8_t *pld, size_t pld_len, void *arg);

typedef int  (avs_videnc_start_h)(struct avs_videnc_state *ves, bool group_mode,
			      void *extcodec_arg);
typedef void (avs_videnc_stop_h)(struct avs_videnc_state *ves);
typedef void (avs_videnc_hold_h)(struct avs_videnc_state *ves, bool hold);
typedef uint32_t (avs_videnc_bwalloc_h)(struct avs_videnc_state *ves);


typedef void (avs_viddec_err_h)(int err, const char *msg, void *arg);

typedef int (avs_viddec_alloc_h)(struct avs_viddec_state **vdsp,
			     struct media_ctx **mctxp,
			     const struct avs_vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     struct avs_vidcodec_param *prm,
			     avs_viddec_err_h *errh,
			     void *arg);

typedef void (avs_viddec_rtp_h)(struct avs_viddec_state *vds,
			    const uint8_t *pkt, size_t len);
typedef void (avs_viddec_rtcp_h)(struct avs_viddec_state *vds,
			     const uint8_t *pkt, size_t len);
typedef int  (avs_viddec_start_h)(struct avs_viddec_state *vds,
			      const char *userid_remote,
			      void *extcodec_arg);
typedef void (avs_viddec_stop_h)(struct avs_viddec_state *vds);
typedef void (avs_viddec_hold_h)(struct avs_viddec_state *vds, bool hold);
typedef int  (avs_viddec_debug_h)(struct re_printf *pf,
			      const struct avs_viddec_state *vds);
typedef uint32_t (avs_viddec_bwalloc_h)(struct avs_viddec_state *vds);


struct avs_vidcodec {
	struct le le;
	struct le ext_le; /* member of external codec list */
	const char *pt;
	const char *name;
	const char *variant;
	const char *fmtp;

	avs_videnc_alloc_h *enc_alloch;
	avs_videnc_start_h *enc_starth;
	avs_videnc_stop_h *enc_stoph;
	avs_videnc_hold_h *enc_holdh;
	avs_videnc_bwalloc_h *enc_bwalloch;

	avs_viddec_alloc_h *dec_alloch;
	avs_viddec_start_h *dec_starth;
	avs_viddec_stop_h *dec_stoph;
	avs_viddec_hold_h *dec_holdh;
	avs_viddec_rtp_h *dec_rtph;
	avs_viddec_rtcp_h *dec_rtcph;
	avs_viddec_debug_h *dec_debugh;
	avs_viddec_bwalloc_h *dec_bwalloch;
	
	struct avs_vidcodec *codec_ref;

	sdp_fmtp_enc_h *fmtp_ench;
	sdp_fmtp_cmp_h *fmtp_cmph;

	void *data;
};

void avs_vidcodec_register(struct list *vidcodecl, struct avs_vidcodec *vc);
void avs_vidcodec_unregister(struct avs_vidcodec *vc);
const struct avs_vidcodec *avs_vidcodec_find(const struct list *vidcodecl,
				     const char *name, const char *variant);
const struct avs_vidcodec *avs_videnc_get(struct avs_videnc_state *ves);
const struct avs_vidcodec *avs_viddec_get(struct avs_viddec_state *vds);

