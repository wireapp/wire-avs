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

#include <pthread.h>
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/video_decoder.h"
#include "vie_render_view.h"
#include "vie.h"


static void vds_destructor(void *arg)
{
	struct viddec_state *vds = (struct viddec_state *)arg;

	vie_render_stop(vds);

	mem_deref(vds->sdpm);
	mem_deref(vds->vie);
}


int vie_dec_alloc(struct viddec_state **vdsp,
		  struct media_ctx **mctxp,
		  const struct vidcodec *vc,
		  const char *fmtp, int pt,
		  struct sdp_media *sdpm,
		  viddec_create_view_h *cvh,
		  viddec_release_view_h *rvh,
		  viddec_err_h *errh,
		  void *arg)
{
	struct viddec_state *vds;
	int err = 0;

	if (!vdsp || !vc || !mctxp)
		return EINVAL;

	info("%s: allocating codec:%s(%d)\n", __FUNCTION__, vc->name, pt);

	vds = (struct viddec_state *)mem_zalloc(sizeof(*vds), vds_destructor);
	if (!vds)
		return ENOMEM;	

	if (*mctxp) {
		vds->vie = (struct vie *)mem_ref(*mctxp);
	}
	else {
		err = vie_alloc(&vds->vie, vc, pt);
		if (err) {
			goto out;
		}

		*mctxp = (struct media_ctx *)vds->vie;
	}

	vds->sdpm = (struct sdp_media *)mem_ref(sdpm);
	vds->vie->vds = vds;

	vds->vc = vc;
	vds->pt = pt;
	vds->cvh = cvh;
	vds->rvh = rvh;	
	vds->errh = errh;
	vds->arg = arg;

 out:
	if (err) {
		mem_deref(vds);
	}
	else {
		*vdsp = vds;
	}

	return err;
}

static bool lssrc_handler(const char *name, const char *value, void *arg)
{
	struct viddec_state *vds = (struct viddec_state*)arg;
	struct pl pl;
	int err;

	if (vds->lssrc_count >= MAX_SSRCS)
		return true;

	err = re_regex(value, strlen(value), "[0-9]+", &pl);
	if (!err) {
		vie_update_ssrc_array(vds->lssrc_array, &vds->lssrc_count, pl_u32(&pl));
	}

	return false;
}

static bool rssrc_handler(const char *name, const char *value, void *arg)
{
	struct viddec_state *vds = (struct viddec_state*)arg;

	if (vds->rssrc_count < MAX_SSRCS) {
		uint32_t val;
		if (sscanf(value, "%u", &val) > 0) {
			vie_update_ssrc_array(vds->rssrc_array, &vds->rssrc_count, val);
		}
	}
	return false;
}

int vie_render_start(struct viddec_state *vds)
{
	webrtc::VideoReceiveStream::Config receive_config;
  	webrtc::VideoReceiveStream::Decoder decoder;
	struct vie *vie = vds ? vds->vie : NULL;
	int ret;
	

	debug("%s: vds=%p\n", __FUNCTION__, vds);

	if (!vie || !vie->call)
		return EINVAL;

	vds->lssrc_count = 0;
	sdp_media_lattr_apply(vds->sdpm, "ssrc", lssrc_handler, vds);
	vds->rssrc_count = 0;
	sdp_media_rattr_apply(vds->sdpm, "ssrc", rssrc_handler, vds);

	if (vds->lssrc_count < 1 || vds->rssrc_count < 1) {
		error("%s: No SSRCS to use for video local:%u remote %u\n",
			__FUNCTION__, vds->lssrc_count, vds->rssrc_count);
		return EINVAL;
	}

	receive_config.rtp.local_ssrc = vds->lssrc_array[0];
	receive_config.rtp.remote_ssrc = vds->rssrc_array[0];
	
	receive_config.rtp.nack.rtp_history_ms = 0;    
#if USE_RTX
	if (vds->rssrc_count > 1) {
		sdp_format *rtx;

		rtx = sdp_media_format(vds->sdpm, false, NULL,
				       -1, "rtx", -1, -1);

		if (!rtx) {
			warning("vie: %s: rtx_fmt not found\n", __func__);
		}
		else {
			debug("vie: %s: rtx ssrc=%u pt=%d\n",
			      __func__, vds->rssrc_array[1], rtx->pt);

			receive_config.rtp.nack.rtp_history_ms = 1000;
			receive_config.rtp.rtx[vds->pt].ssrc = vds->rssrc_array[1];
			receive_config.rtp.rtx[vds->pt].payload_type =
				rtx->pt;
		}
	}
#endif

#if USE_REMB
	receive_config.rtp.remb = true;
#else
	receive_config.rtp.remb = false;
#endif
	

#if USE_RTX
#endif

	receive_config.rtp.extensions.push_back(
		webrtc::RtpExtension(webrtc::RtpExtension::kAbsSendTime,
				     kAbsSendTimeExtensionId));
	vie->receive_renderer = new ViERenderer();
	receive_config.renderer = vie->receive_renderer;

	decoder.payload_type = vds->pt;
	decoder.payload_name = "VP8";
	vie->decoder = webrtc::VideoDecoder::Create(webrtc::VideoDecoder::kVp8);
	decoder.decoder = vie->decoder;
	receive_config.decoders.push_back(decoder);

	vie->receive_stream = vie->call->CreateVideoReceiveStream(receive_config);

	vie->receive_stream->Start();

	vds->started = true;
	debug("%s: %s\n",
	      __FUNCTION__, vds->started ? "started" : "stopped");

	debug("%s: completed successfully\n", __FUNCTION__, ret);

	return 0;
}


void vie_render_stop(struct viddec_state *vds)
{
	struct vie *vie = vds ? vds->vie : NULL;
	if (!vie || !vds)
		return;

	if (!vds->started)
		return;


	if (vie->receive_stream) {
		vie->receive_stream->Stop();
		vie->call->DestroyVideoReceiveStream(vie->receive_stream);
	}


	if (vie->receive_renderer) {
		vie->receive_renderer->Stop();
		vie->receive_renderer->DetachFromView();
		if (vds->rvh) {
			vds->rvh(vie->receive_renderer->View(), vds->arg);
		}
		delete vie->receive_renderer;
		vie->receive_renderer = NULL;
	}

	vds->started = false;
}


void vie_render_hold(struct viddec_state *vds, bool hold)
{
	struct vie *vie;
	
	if (!vds)
		return;

	vie = vds->vie;
	
	if (hold) {
		if (vds->started) {
			if (vie->receive_stream) {
				vie->receive_stream->Stop();
			}
		}
	}
	else {
		if (vds->started) {
			if (vie->receive_stream) {
				vie->receive_stream->Start();
			}
		}
	}
}


void vie_dec_rtp_handler(struct viddec_state *vds,
			 const uint8_t *pkt, size_t len)
{
	webrtc::PacketReceiver::DeliveryStatus delstat;
	webrtc::PacketTime pt(tmr_jiffies(), 0ULL);
	struct vie *vie = vds ? vds->vie : NULL;
	struct rtp_header rtph;
	

	if (!vie || !vds)
		return;

	if (!vds->started)
		return;

	if (!vds->packet_received) {
		debug("vie: %s: first RTP packet received\n", __func__);
		vds->packet_received = true;
		if (vds->cvh) {
			vds->cvh(vds->arg);
		}		
	}
	
	stats_rtp_add_packet(&vie->stats_rx, pkt, len);

#if FORCE_VIDEO_RECORDING
    if(!vie->rtpDump->IsActive()){
        char  buf[80];
        time_t     now = time(0);
        struct tm  tstruct;
        
        tstruct = *localtime(&now);
        strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
        
        std::string name = "VidIn";
        name.insert(name.size(),buf);
        name.insert(name.size(),".rtp");
        
        printf("opening file %s \n", name.c_str());
        
        vie->rtpDump->Start(name.c_str());
    }
    vie->rtpDump->DumpPacket(pkt, len);
#endif
    
	delstat = vie->call->Receiver()->DeliverPacket(webrtc::MediaType::VIDEO, pkt, len);
}


void vie_dec_rtcp_handler(struct viddec_state *vds,
			  const uint8_t *pkt, size_t len)
{
	struct vie *vie = vds ? vds->vie : NULL;
	webrtc::PacketReceiver::DeliveryStatus delstat;

	if (!vds || !vds->vie)
		return;

	if (!vds->started)
		return;
	stats_rtcp_add_packet(&vds->vie->stats_rx, pkt, len);

	delstat = vie->call->Receiver()->DeliverPacket(webrtc::MediaType::VIDEO, pkt, len);
}


int vie_set_view(struct viddec_state *vds, void *view)
{
	webrtc::VideoRender *render;
	struct vie *vie = vds ? vds->vie : NULL;
	int ret;

	if (!vds)
		return EINVAL;

	debug("%s: vds=%p view=%p\n", __FUNCTION__, vds, view);

	if (!vie) {
		warning("%s: something not inited vie: %p\n",
			__FUNCTION__, vie);
		return EINVAL;
	}

	if (!vds->started) {
		warning("%s: decoder not started\n", __FUNCTION__);
		return 0;
	}

	if (!view) {
		debug("%s: called with NULL view\n", __FUNCTION__);
		return 0;
	}

	if (vie->receive_renderer) {
		vie->receive_renderer->AttachToView(view);
		if (!vds->backgrounded) {
			vie->receive_renderer->Start();
		}
	}

	return 0;
}

void vie_render_background(struct viddec_state *vds, enum media_bg_state state)
{
	struct vie *vie = vds ? vds->vie : NULL;

	debug("%s: %s vds %p vie %p recr %p\n", __FUNCTION__,
		state == MEDIA_BG_STATE_ENTER ? "enter" : "exit",
		vds, vie, vie ? vie->receive_renderer : NULL);
	if (vds && vds->started && vie && vie->receive_renderer) {
		switch (state) {
			case MEDIA_BG_STATE_ENTER:
				vds->backgrounded = true;
				vie->receive_renderer->Stop();
				break;

			case MEDIA_BG_STATE_EXIT:
				vds->backgrounded = false;
				vie->receive_renderer->Start();
				break;
		}
	}
}

