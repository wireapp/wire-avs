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


struct vidcodec_param {
	uint32_t local_ssrcv[2];
	size_t local_ssrcc;

	uint32_t remote_ssrcv[4];
	size_t remote_ssrcc;
};

struct media_ctx;
struct videnc_state;
struct viddec_state;
struct vidcodec;
struct vidframe;

typedef void (videnc_err_h)(int err, const char *msg, void *arg);

typedef int  (videnc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (videnc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef int (videnc_alloc_h)(struct videnc_state **vesp,
			     struct media_ctx **mctxp,
			     const struct vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     struct vidcodec_param *prm,
			     videnc_rtp_h *rtph,
			     videnc_rtcp_h *rtcph,
			     videnc_err_h *errh,
			     void *extcodec_arg,
			     void *arg);


typedef int (videnc_packet_h)(bool marker, const uint8_t *hdr, size_t hdr_len,
			      const uint8_t *pld, size_t pld_len, void *arg);

typedef int  (videnc_start_h)(struct videnc_state *ves, bool group_mode);
typedef void (videnc_stop_h)(struct videnc_state *ves);
typedef void (videnc_hold_h)(struct videnc_state *ves, bool hold);
typedef uint32_t (videnc_bwalloc_h)(struct videnc_state *ves);


typedef void (viddec_err_h)(int err, const char *msg, void *arg);

typedef int (viddec_alloc_h)(struct viddec_state **vdsp,
			     struct media_ctx **mctxp,
			     const struct vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     struct vidcodec_param *prm,
			     viddec_err_h *errh,
			     void *extcodec_arg,
			     void *arg);

typedef void (viddec_rtp_h)(struct viddec_state *vds,
			    const uint8_t *pkt, size_t len);
typedef void (viddec_rtcp_h)(struct viddec_state *vds,
			     const uint8_t *pkt, size_t len);
typedef int  (viddec_start_h)(struct viddec_state *vds, const char *userid_remote);
typedef void (viddec_stop_h)(struct viddec_state *vds);
typedef void (viddec_hold_h)(struct viddec_state *vds, bool hold);
typedef int  (viddec_debug_h)(struct re_printf *pf,
			      const struct viddec_state *vds);
typedef uint32_t (viddec_bwalloc_h)(struct viddec_state *vds);


struct vidcodec {
	struct le le;
	struct le ext_le; /* member of external codec list */
	const char *pt;
	const char *name;
	const char *variant;
	const char *fmtp;

	videnc_alloc_h *enc_alloch;
	videnc_start_h *enc_starth;
	videnc_stop_h *enc_stoph;
	videnc_hold_h *enc_holdh;
	videnc_bwalloc_h *enc_bwalloch;

	viddec_alloc_h *dec_alloch;
	viddec_start_h *dec_starth;
	viddec_stop_h *dec_stoph;
	viddec_hold_h *dec_holdh;
	viddec_rtp_h *dec_rtph;
	viddec_rtcp_h *dec_rtcph;
	viddec_debug_h *dec_debugh;
	viddec_bwalloc_h *dec_bwalloch;
	
	struct vidcodec *codec_ref;

	sdp_fmtp_enc_h *fmtp_ench;
	sdp_fmtp_cmp_h *fmtp_cmph;

	void *data;
};

void vidcodec_register(struct list *vidcodecl, struct vidcodec *vc);
void vidcodec_unregister(struct vidcodec *vc);
const struct vidcodec *vidcodec_find(const struct list *vidcodecl,
				     const char *name, const char *variant);
const struct vidcodec *videnc_get(struct videnc_state *ves);
const struct vidcodec *viddec_get(struct viddec_state *vds);

