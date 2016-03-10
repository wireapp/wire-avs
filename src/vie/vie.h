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

#ifndef VIE_H
#define VIE_H

#include "webrtc/modules/video_capture/include/video_capture.h"
#include "webrtc/call.h"
#include "vie_renderer.h"
#include "webrtc/modules/utility/interface/rtp_dump.h"

#define USE_RTX  1
#define USE_REMB 1

#define FORCE_VIDEO_RECORDING 0

static const int kAbsSendTimeExtensionId = 7;

static const uint8_t kRtxVideoPayloadType = 101;
static const uint8_t kVideoPayloadType = 100;

class ViETransport;
class ViERenderer;
class ViECaptureRouter;

/* rtcp stats */

struct transp_stats {
	struct {
		size_t pkt;
		size_t bytes;
	} rtp;

	struct {
		size_t pkt;

		size_t sr;
		size_t rr;
		size_t sdes;
		size_t rtpfb;
		size_t psfb;
		size_t unknown;
	} rtcp;
};


void stats_rtp_add_packet(struct transp_stats *stats,
			  const uint8_t *data, size_t len);
int  stats_rtcp_add_packet(struct transp_stats *stats,
			   const uint8_t *data, size_t len);
int  stats_print(struct re_printf *pf, const struct transp_stats *stats);


/* encode */

#define MAX_SSRCS 16

struct videnc_state {
	const struct vidcodec *vc;  /* base class (inheritance) */

	struct vie *vie;
	struct sdp_media *sdpm;

	int pt;
	
	uint32_t capture_width;
	uint32_t capture_height;
	uint32_t capture_fps;

	videnc_rtp_h *rtph;
	videnc_rtcp_h *rtcph;
	videnc_create_preview_h *cpvh;
	videnc_release_preview_h *rpvh;
	videnc_err_h *errh;
	void *arg;

	uint32_t ssrc_array[MAX_SSRCS];
	size_t ssrc_count;
};

int  vie_enc_alloc(struct videnc_state **vesp,
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
int  vie_capture_start(struct videnc_state *ves);
void vie_capture_stop(struct videnc_state *ves);
void vie_capture_hold(struct videnc_state *ves, bool hold);
void vie_capture_background(struct videnc_state *ves, enum media_bg_state state);

int vie_set_capture_device(struct videnc_state *ves, const char *dev_id);

/* decode */

struct viddec_state {
	const struct vidcodec *vc;  /* base class (inheritance) */

	struct vie *vie;
	struct sdp_media *sdpm;

	bool started;
	bool packet_received;
	bool backgrounded;

	int pt;
	
	viddec_create_view_h *cvh;
	viddec_release_view_h *rvh;
	viddec_err_h *errh;
	void *arg;

	uint32_t lssrc_array[MAX_SSRCS];
	size_t lssrc_count;
	uint32_t rssrc_array[MAX_SSRCS];
	size_t rssrc_count;
};

int  vie_dec_alloc(struct viddec_state **vdsp,
		   struct media_ctx **mctxp,
		   const struct vidcodec *vc,
		   const char *fmtp, int pt,
		   struct sdp_media *sdpm,
		   viddec_create_view_h *cvh,
		   viddec_release_view_h *rvh,
		   viddec_err_h *errh,
		   void *arg);
int  vie_render_start(struct viddec_state *vds);
void vie_render_stop(struct viddec_state *vds);
void vie_render_hold(struct viddec_state *vds, bool hold);
void vie_render_background(struct viddec_state *vds, enum media_bg_state state);
void vie_dec_rtp_handler(struct viddec_state *vds,
			 const uint8_t *pkt, size_t len);
void vie_dec_rtcp_handler(struct viddec_state *vds,
			  const uint8_t *pkt, size_t len);

int vie_set_view(viddec_state* vds, void *view);

void vie_update_ssrc_array( uint32_t array[], size_t *count, uint32_t val);

/* shared */

struct vie {
	struct viddec_state *vds;  /* pointer */
	struct videnc_state *ves;  /* pointer */
	const webrtc::VideoCodec *codec;
	webrtc::Call *call;
	ViETransport *transport;

	/* Sender side */
	webrtc::VideoEncoder* encoder;
	webrtc::VideoSendStream *send_stream;

	/* Receiver side */
	webrtc::VideoDecoder* decoder;
	webrtc::VideoReceiveStream* receive_stream;
	ViERenderer *receive_renderer;

	int ch;
	struct le le;

	struct transp_stats stats_tx;
	struct transp_stats stats_rx;
    
    webrtc::RtpDump* rtpDump;
};

int vie_alloc(struct vie **viep, const struct vidcodec *vc, int pt);


/* global */

class ViELogCallback : public webrtc::TraceCallback {
public:
	ViELogCallback() {};
	virtual ~ViELogCallback() {};

	virtual void Print(webrtc::TraceLevel lvl, const char* message,
			   int length) override
	{
		if (lvl & (webrtc::kTraceCritical | webrtc::kTraceError))
			error("%s\n", message);
		else if (lvl & webrtc::kTraceWarning)
			warning("%s\n", message);
		else if (lvl & (webrtc::kTraceStateInfo | webrtc::kTraceInfo))
			info("%s\n", message);
		else
			debug("%s\n", message);
	};
};

struct vid_eng {
	webrtc::VideoCaptureModule::DeviceInfo* devinfo;

	ViELogCallback log_handler;

	size_t ncodecs;
	webrtc::VideoCodec *codecs;

	struct list chl;

	bool renderer_reset;
	bool capture_reset;
};

extern struct vid_eng vid_eng;

const webrtc::VideoCodec *vie_find_codec(const struct vidcodec *vc);


/* sdp */

int vie_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *data);
int vie_rtx_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		     bool offer, void *data);


#endif

