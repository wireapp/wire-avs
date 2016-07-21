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
#include <avs_voe.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/video_decoder.h"
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
		  struct vidcodec_param *prm,
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
	vds->errh = errh;
	vds->arg = arg;
	if (prm)
		vds->prm = *prm;

 out:
	if (err) {
		mem_deref(vds);
	}
	else {
		*vdsp = vds;
	}

	return err;
}

static bool check_remb_attr(const char *name, const char *value, void *arg){
	if (0 == re_regex(value, strlen(value), "goog-remb")){
		return true;
		} else{
		return false;
	}
}

static bool sdp_has_remb(struct viddec_state *vds){
	const char *goog_remb;
	goog_remb = sdp_media_rattr_apply(vds->sdpm, "rtcp-fb", check_remb_attr, NULL);

	return goog_remb ? true : false;
}

int vie_render_start(struct viddec_state *vds)
{
  	webrtc::VideoReceiveStream::Decoder decoder;
	struct vie *vie = vds ? vds->vie : NULL;
	int ret;
	

	debug("%s: vds=%p\n", __FUNCTION__, vds);

	if (!vie || !vie->call)
		return EINVAL;

	webrtc::VideoReceiveStream::Config receive_config(vie->transport);

	if (vds->prm.local_ssrcc < 1 || vds->prm.remote_ssrcc < 1) {
		error("%s: No SSRCS to use for video local:%u remote %u\n",
			__FUNCTION__,
		      vds->prm.local_ssrcc, vds->prm.remote_ssrcc);
		return EINVAL;
	}

	receive_config.rtp.nack.rtp_history_ms = 0;    
	receive_config.rtp.local_ssrc = vds->prm.local_ssrcv[0];
	receive_config.rtp.remote_ssrc = vds->prm.remote_ssrcv[0];
    
#if USE_RTX
	if (vds->prm.remote_ssrcc > 1) {

		sdp_format *rtx;

		rtx = sdp_media_format(vds->sdpm, true, NULL,
				       -1, "rtx", -1, -1);

		if (!rtx) {
			warning("vie: %s: rtx_fmt not found\n", __func__);
		}
		else {
			debug("vie: %s: rtx ssrc=%u pt=%d\n",
			      __func__, vds->prm.remote_ssrcv[1], rtx->pt);

			receive_config.rtp.nack.rtp_history_ms = 1000;

			receive_config.rtp.rtx[vds->pt].ssrc =
				vds->prm.remote_ssrcv[1];
			receive_config.rtp.rtx[vds->pt].payload_type =
				rtx->pt;
		}
	}
#endif

#if USE_REMB
	receive_config.rtp.remb = sdp_has_remb(vds);
#else
	receive_config.rtp.remb = false;
#endif
	

#if USE_RTX
#endif

	receive_config.rtp.extensions.push_back(
		webrtc::RtpExtension(webrtc::RtpExtension::kAbsSendTime,
		kAbsSendTimeExtensionId));
	receive_config.rtp.extensions.push_back(
		webrtc::RtpExtension(webrtc::RtpExtension::kVideoRotation,
		kVideoRotationRtpExtensionId));
	vie->receive_renderer = new ViERenderer();
	receive_config.renderer = vie->receive_renderer;

	decoder.payload_type = vds->pt;
	decoder.payload_name = "VP8";
	vie->decoder = webrtc::VideoDecoder::Create(webrtc::VideoDecoder::kVp8);
	decoder.decoder = vie->decoder;
	receive_config.decoders.push_back(decoder);

	// TODO: find the new version of this flag
	//receive_config.enable_vqi = (avs_get_flags() & AVS_FLAG_EXPERIMENTAL);
    
	vie->receive_stream = vie->call->CreateVideoReceiveStream(receive_config);

	vie->receive_stream->Start();

	vds->started = true;
	debug("%s: %s lssrc=%u rssrc=%u\n",
	      __FUNCTION__, vds->started ? "started" : "stopped",
	      vds->prm.local_ssrcv[0], vds->prm.remote_ssrcv[0]);

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

    if(vie->decoder){
        delete vie->decoder;
    }
    

	if (vie->receive_renderer) {
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
	webrtc::PacketTime pt(-1, 0ULL);
	struct vie *vie = vds ? vds->vie : NULL;

	if (!vie || !vds)
		return;

	if (!vds->started)
		return;

	if (!vds->packet_received) {
		debug("vie: %s: first RTP packet received\n", __func__);
		vds->packet_received = true;
	}


	stats_rtp_add_packet(&vie->stats_rx, pkt, len);

#if FORCE_VIDEO_RTP_RECORDING
	// Only Save RTP header and length;
	uint32_t len32 = (int32_t)len;
	uint8_t buf[VIDEO_RTP_RECORDING_LENGTH + sizeof(uint32_t)]; // RTP header is 12 bytes but we also need header extension
    
	memcpy(buf, &len32, sizeof(uint32_t));
	memcpy(&buf[sizeof(uint32_t)], pkt, VIDEO_RTP_RECORDING_LENGTH*sizeof(uint8_t));

	vie->rtp_dump_in->DumpPacket(buf, sizeof(buf));
#endif

	delstat = vie->call->Receiver()->DeliverPacket(webrtc::MediaType::VIDEO, pkt, len, pt);


	if (delstat != webrtc::PacketReceiver::DELIVERY_OK) {
		warning("vie: DeliverPacket error %d\n", delstat);
	}
}


void vie_dec_rtcp_handler(struct viddec_state *vds,
			  const uint8_t *pkt, size_t len)
{
	struct vie *vie = vds ? vds->vie : NULL;
	webrtc::PacketReceiver::DeliveryStatus delstat;
	webrtc::PacketTime pt(tmr_jiffies(), 0ULL);

	if (!vds || !vds->vie)
		return;

	if (!vds->started)
		return;

	uint32_t old_limit = vie->stats_rx.rtcp.bitrate_limit;
	stats_rtcp_add_packet(&vds->vie->stats_rx, pkt, len);

	if (old_limit != vie->stats_rx.rtcp.bitrate_limit) {
		vie_bandwidth_allocation_changed(vie, vie->stats_rx.rtcp.ssrc,
			vie->stats_rx.rtcp.bitrate_limit);
	}

	delstat = vie->call->Receiver()->DeliverPacket(webrtc::MediaType::VIDEO, pkt, len, pt);
}
