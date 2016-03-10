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

/** Video Codec parameters */
struct videnc_param {
	unsigned bitrate;  /**< Encoder bitrate in [bit/s] */
	unsigned pktsize;  /**< RTP packetsize in [bytes]  */
	unsigned fps;      /**< Video framerate            */
	uint32_t max_fs;
};

struct videnc_capture_device {
	struct le list_elem;
	char dev_id[128];
	char dev_name[128];
};

struct media_ctx;
struct videnc_state;
struct viddec_state;
struct vidcodec;
struct vidframe;

typedef void (videnc_err_h)(int err, const char *msg, void *arg);

typedef int  (videnc_rtp_h)(const uint8_t *pkt, size_t len, void *arg);
typedef int  (videnc_rtcp_h)(const uint8_t *pkt, size_t len, void *arg);

typedef void (videnc_create_preview_h)(void *arg);
typedef void (videnc_release_preview_h)(void *view, void *arg);

typedef int (videnc_alloc_h)(struct videnc_state **vesp,
			     struct media_ctx **mctxp,
			     const struct vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     videnc_rtp_h *rtph,
			     videnc_rtcp_h *rtcph,
			     videnc_create_preview_h *cpvh,
			     videnc_release_preview_h *rpvh,
			     videnc_err_h *errh,
			     void *arg);


typedef int (videnc_packet_h)(bool marker, const uint8_t *hdr, size_t hdr_len,
			      const uint8_t *pld, size_t pld_len, void *arg);

typedef int (videnc_encode_h)(struct videnc_state *ves, bool update,
			      const struct vidframe *frame,
			      videnc_packet_h *pkth, void *arg);
typedef int  (videnc_start_h)(struct videnc_state *ves);
typedef void (videnc_stop_h)(struct videnc_state *ves);
typedef void (videnc_hold_h)(struct videnc_state *ves, bool hold);

typedef int  (videnc_set_preview_h)(struct videnc_state *ves,
				    void *view);
typedef int  (videnc_set_capture_device_h)(struct videnc_state *ves,
					   const char *dev_id);
typedef void (videnc_bg_h)(struct videnc_state *ves,
			   enum media_bg_state state);


typedef void (viddec_create_view_h)(void *arg);
typedef void (viddec_release_view_h)(void *view, void *arg);

typedef void (viddec_err_h)(int err, const char *msg, void *arg);

typedef int (viddec_alloc_h)(struct viddec_state **vdsp,
			     struct media_ctx **mctxp,
			     const struct vidcodec *vc,
			     const char *fmtp, int pt,
			     struct sdp_media *sdpm,
			     viddec_create_view_h *cvh,
			     viddec_release_view_h *rvh,
			     viddec_err_h *errh,
			     void *arg);

typedef int  (viddec_decode_h)(struct viddec_state *vds,
			       struct vidframe *frame,
			       bool marker, uint16_t seq, struct mbuf *mb);
typedef void (viddec_rtp_h)(struct viddec_state *vds,
			    const uint8_t *pkt, size_t len);
typedef void (viddec_rtcp_h)(struct viddec_state *vds,
			     const uint8_t *pkt, size_t len);
typedef int  (viddec_start_h)(struct viddec_state *vds);
typedef void (viddec_stop_h)(struct viddec_state *vds);
typedef void (viddec_hold_h)(struct viddec_state *vds, bool hold);
typedef int  (viddec_debug_h)(struct re_printf *pf,
			      const struct viddec_state *vds);

typedef int  (viddec_set_view_h)(struct viddec_state *vds,
				 void *view);

typedef void (viddec_bg_h)(struct viddec_state *vds,
			   enum media_bg_state state);

struct vidcodec {
	struct le le;
	const char *pt;
	const char *name;
	const char *variant;
	const char *fmtp;
	bool has_rtp;

	videnc_alloc_h *enc_alloch;
	videnc_encode_h *ench;
	videnc_start_h *enc_starth;
	videnc_stop_h *enc_stoph;
	videnc_hold_h *enc_holdh;
	videnc_set_preview_h *enc_previewh;
	videnc_set_capture_device_h *enc_deviceh;
	videnc_bg_h *enc_bgh;

	viddec_alloc_h *dec_alloch;
	viddec_decode_h *dech;
	viddec_start_h *dec_starth;
	viddec_stop_h *dec_stoph;
	viddec_hold_h *dec_holdh;
	viddec_rtp_h *dec_rtph;
	viddec_rtcp_h *dec_rtcph;
	viddec_debug_h *dec_debugh;
	viddec_set_view_h *dec_viewh;
	viddec_bg_h *dec_bgh;

	sdp_fmtp_enc_h *fmtp_ench;
	sdp_fmtp_cmp_h *fmtp_cmph;

	void *data;
};

void vidcodec_register(struct list *vidcodecl, struct vidcodec *vc);
void vidcodec_unregister(struct vidcodec *vc);
const struct vidcodec *vidcodec_find(const struct list *vidcodecl,
				     const char *name, const char *variant);
const struct vidcodec *vidcodec_find_encoder(const struct list *vidcodecl,
					     const char *name);
const struct vidcodec *vidcodec_find_decoder(const struct list *vidcodecl,
					     const char *name);
const struct vidcodec *videnc_get(struct videnc_state *ves);
const struct vidcodec *viddec_get(struct viddec_state *vds);

