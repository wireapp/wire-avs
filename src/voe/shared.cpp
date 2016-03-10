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
#include <re.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_file.h"
#include "webrtc/voice_engine/include/voe_audio_processing.h"
#include "webrtc/voice_engine/include/voe_volume_control.h"
#include "webrtc/voice_engine/include/voe_rtp_rtcp.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"
#include "webrtc/voice_engine/include/voe_errors.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "webrtc/voice_engine/include/voe_conf_control.h"
#include "voe_settings.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include <vector>

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "avs_voe.h"
#include "voe.h"


static void tmr_transport_handler(void *arg);


class VoETransport : public webrtc::Transport {
public:
	VoETransport(struct voe_channel *ve_) : ve(ve_), active(true),
					rtp_started(false) {
		tmr_init(&tmr);
		le = (struct le)LE_INIT;
		list_append(&gvoe.transportl, &le, this);
	};
	virtual ~VoETransport() {
		tmr_cancel(&tmr);
		list_unlink(&le);
	};

	virtual int SendPacket(int channel, const void *data, size_t len) {
		int err = 0;
		struct auenc_state *aes = NULL;
		if (!active) {
			err = ENOENT;
			goto out;
		}
		
		aes = ve->aes;
		if (aes->rtph) {
			err = aes->rtph((const uint8_t *)data, len, aes->arg);
			if (err) {
				warning("voe: rtp send failed (%m)\n", err);
				return -1;
			}
		}

	out:
		return err ? -1 : len;
	};

	virtual int SendRTCPPacket(int channel, const void *data, size_t len)
	{
		int err = 0;
		struct auenc_state *aes = NULL;
        
		debug("VoETransport::SendRTCP: ve=%p active=%d\n", ve, active);

		if (!active) {
			err = ENOENT;
			goto out;
		}

		aes = ve->aes;
		if (aes->rtcph) {
			err = aes->rtcph((const uint8_t *)data, len, aes->arg);
			if (err) {
				warning("voe: rtcp send failed (%m)\n", err);
				return -1;
			}
		}

	out:
		return err ? -1 : len;
	};

	void deregister()
	{
		active = false;
		tmr_start(&tmr, MILLISECONDS_PER_SECOND,
			  tmr_transport_handler, this);
	}

private:
	struct voe_channel *ve;
	bool active;
	bool rtp_started;
	struct tmr tmr;
	struct le le;
};


static void tmr_transport_handler(void *arg)
{
	VoETransport *tp = (VoETransport *)arg;

	delete tp;
}


static void voe_setup_opus(bool use_stereo, int32_t rate_bps,
			   webrtc::CodecInst *codec_params)
{
	if (strncmp(codec_params->plname, "opus", 4) == 0) {
		if (use_stereo){
			codec_params->channels = 2;
		}
		else{
			codec_params->channels = 1;
		}
		codec_params->rate = rate_bps;
	}
}


static void print_rtcp_info(const struct voe_channel *ve)
{
	webrtc::CallStatistics stats;

	if (!gvoe.rtp_rtcp)
		return;

	if (0 == gvoe.rtp_rtcp->GetRTCPStatistics(ve->ch, stats)) {
		info("voe: rttMs         = %d\n", stats.rttMs);
		info("voe: fraction lost = %d\n", stats.fractionLost);
	}

	unsigned int averageJitterMs;
	unsigned int maxJitterMs;
	unsigned int discardedPackets;

	if (0 == gvoe.rtp_rtcp->GetRTPStatistics(ve->ch,
						averageJitterMs,
						maxJitterMs,
						discardedPackets)) {

		info("voe: averageJitterMs:   %d ms\n", averageJitterMs);
		info("     maxJitterMs:       %d ms\n", maxJitterMs);
		info("     discardedPackets:  %d\n", discardedPackets);
	}
}


static void ve_destructor(void *arg)
{
	struct voe_channel *ve = (struct voe_channel *)arg;

	debug("ve_destructor: %p voe.nch = %d \n", ve, gvoe.nch);

	print_rtcp_info(ve);

	ve->transport->deregister();
	gvoe.nw->DeRegisterExternalTransport(ve->ch);

	gvoe.base->DeleteChannel(ve->ch);

	if (gvoe.nch > 0)
		--gvoe.nch;
	if (gvoe.nch == 0) {
		info("voe: Last Channel deleted call voe.base->Terminate()\n");

		voe_stop_audio_proc(&gvoe);

		voe_stop_silencing();

		gvoe.base->Terminate();
	}
}


int voe_ve_alloc(struct voe_channel **vep, const struct aucodec *ac,
                 uint32_t srate, int pt)
{
	struct voe_channel *ve;
	webrtc::CodecInst c;
	int err = 0;

	ve = (struct voe_channel *)mem_zalloc(sizeof(*ve), ve_destructor);
	if (!ve)
		return ENOMEM;

	debug("voe: ve_alloc: ve=%p(%d) voe.nch = %d \n",
	      ve, mem_nrefs(ve), gvoe.nch);

	ve->ac = ac;
	ve->srate = srate;
	ve->pt = pt;
    
	++gvoe.nch;
	if (gvoe.nch == 1) {
		info("voe: First Channel created call voe.base->Init() \n");
		gvoe.base->Init();

		voe_start_audio_proc(&gvoe);

		voe_get_mute(&gvoe.isMuted);

		voe_start_silencing();
        
		gvoe.packet_size_ms = 20;
	}

	int bitrate_bps = gvoe.manual_bitrate_bps ? gvoe.manual_bitrate_bps : gvoe.bitrate_bps;
	int packet_size_ms = gvoe.manual_packet_size_ms ? gvoe.manual_packet_size_ms :
        std::max( gvoe.packet_size_ms, gvoe.min_packet_size_ms );
    
	ve->ch = gvoe.base->CreateChannel();
	if (ve->ch == -1) {
		err = ENOMEM;
		goto out;
	}

	ve->transport = new VoETransport(ve);
	gvoe.nw->RegisterExternalTransport(ve->ch, *ve->transport);

	c = *(webrtc::CodecInst *)ac->data;
	c.pltype = pt;

	voe_setup_opus( ZETA_OPUS_USE_STEREO, ZETA_OPUS_BITRATE_HI_BPS, &c);
    
	c.pacsize = (c.plfreq * packet_size_ms) / 1000;
	c.rate = bitrate_bps;
    
	gvoe.codec->SetSendCodec(ve->ch, c);
	gvoe.codec->SetRecPayloadType(ve->ch, c);

#if USE_MEDIAENGINE
	gvoe.codec->SetFECStatus( ve->ch, ZETA_USE_INBAND_FEC );
#endif
    
	gvoe.codec->SetOpusDtx( ve->ch, ZETA_USE_DTX );
    
 out:
	if (err) {
		mem_deref(ve);
	}
	else {
		*vep = ve;
	}

	return err;
}


void voe_transportl_flush(void)
{
	struct le *le;

	le = gvoe.transportl.head;
	while (le) {
		VoETransport *vt = (VoETransport *)le->data;
		le = le->next;

		delete vt;
	}
}
