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

#include "webrtc/call.h"
#include "vie_renderer.h"
#include "webrtc/transport.h"
#include "capture_router.h"

#include "avs_rtpdump.h"

#define USE_RTX  1
#define USE_REMB 1
#define USE_RTP_ROTATION 1

#define FORCE_VIDEO_RTP_RECORDING 0
#define VIDEO_RTP_RECORDING_LENGTH 30

//#define VIE_DEBUG_RTX 1

#define EXTMAP_ABS_SEND_TIME "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
#define EXTMAP_VIDEO_ORIENTATION "urn:3gpp:video-orientation"

class ViERenderer;
class ViECaptureRouter;
class ViELoadObserver;

class ViETransport : public webrtc::Transport
{
public:
	ViETransport(struct vie *vie_);
	virtual ~ViETransport();

	bool SendRtp(const uint8_t* packet, size_t length,
		const webrtc::PacketOptions& options);

	bool SendRtcp(const uint8_t* packet, size_t length);

private:
	struct vie *vie;
	bool active;
};

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
		size_t bye;
		size_t unknown;
		uint32_t ssrc;
		uint32_t bitrate_limit;
	} rtcp;
};


void stats_rtp_add_packet(struct transp_stats *stats,
			  const uint8_t *data, size_t len);
int  stats_rtcp_add_packet(struct transp_stats *stats,
			   const uint8_t *data, size_t len);
int  stats_print(struct re_printf *pf, const struct transp_stats *stats);


/* encode */

struct resolution_info {
	uint32_t width;
	uint32_t height;
	uint32_t max_fps;
	uint32_t min_br;
	uint32_t max_br;
};

struct videnc_state {
	const struct vidcodec *vc;  /* base class (inheritance) */

	struct vie *vie;
	struct sdp_media *sdpm;

	int pt;
	int extmap_abstime;
	int extmap_rotation;
	
	const struct resolution_info *curr_res;
	uint64_t ts_res_changed;
	bool rtp_rotation;
	size_t max_bandwidth;
	bool group_mode;

	enum flowmgr_video_send_state send_state;
	videnc_rtp_h *rtph;
	videnc_rtcp_h *rtcph;
	videnc_err_h *errh;
	void *arg;

	struct vidcodec_param prm;
};

int  vie_enc_alloc(struct videnc_state **vesp,
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
int  vie_capture_start(struct videnc_state *ves, bool group_mode);
void vie_capture_stop(struct videnc_state *ves);
void vie_capture_hold(struct videnc_state *ves, bool hold);
uint32_t vie_capture_getbw(struct videnc_state *ves);

void vie_frame_handler(webrtc::VideoFrame *frame, void *arg);

/* decode */

struct viddec_state {
	const struct vidcodec *vc;  /* base class (inheritance) */

	struct vie *vie;
	struct sdp_media *sdpm;

	bool started;
	bool packet_received;

	int pt;
	int extmap_abstime;
	int extmap_rotation;
	
	viddec_err_h *errh;
	void *arg;

	struct vidcodec_param prm;
};

int  vie_dec_alloc(struct viddec_state **vdsp,
		   struct media_ctx **mctxp,
		   const struct vidcodec *vc,
		   const char *fmtp, int pt,
		   struct sdp_media *sdpm,
		   struct vidcodec_param *prm,
		   viddec_err_h *errh,
		   void *extcodec_arg,
		   void *arg);
int  vie_render_start(struct viddec_state *vds, const char* userid_remote);
void vie_render_stop(struct viddec_state *vds);
void vie_render_hold(struct viddec_state *vds, bool hold);
void vie_dec_rtp_handler(struct viddec_state *vds,
			 const uint8_t *pkt, size_t len);
void vie_dec_rtcp_handler(struct viddec_state *vds,
			  const uint8_t *pkt, size_t len);
uint32_t vie_dec_getbw(struct viddec_state *vds);

void vie_update_ssrc_array( uint32_t array[], size_t *count, uint32_t val);

/* shared */

struct vie {
	struct viddec_state *vds;  /* pointer */
	struct videnc_state *ves;  /* pointer */
	const webrtc::VideoCodec *codec;
	webrtc::Call *call;
	ViETransport *transport;
	ViELoadObserver *load_observer;
    
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
    
    wire_avs::RtpDump* rtp_dump_in;
    wire_avs::RtpDump* rtp_dump_out;
    wire_avs::RtpDump* rtcp_dump_in;
    wire_avs::RtpDump* rtcp_dump_out;
    
    int rtx_pt;
};

int vie_alloc(struct vie **viep, const struct vidcodec *vc, int pt);
void vie_bandwidth_allocation_changed(struct vie *vie, uint32_t ssrc, uint32_t allocation);

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
	ViELogCallback log_handler;

	size_t ncodecs;
	webrtc::VideoCodec *codecs;

	struct list chl;

	bool renderer_reset;
	bool capture_reset;

	vie_video_state_change_h *state_change_h;
	vie_render_frame_h *render_frame_h;
	vie_video_size_h *size_h;
	void *cb_arg;
};

extern struct vid_eng vid_eng;

const webrtc::VideoCodec *vie_find_codec(const struct vidcodec *vc);


/* sdp */

int vie_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *data);
int vie_rtx_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		     bool offer, void *data);


#endif

