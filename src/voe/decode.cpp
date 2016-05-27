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
#include <algorithm>

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "voe.h"
#include "avs_voe.h"

static void ads_destructor(void *arg)
{
	struct audec_state *ads = (struct audec_state *)arg;

	debug("voe: ads_destructor: %p ve=%p(%d)\n",
	      ads, ads->ve, mem_nrefs(ads->ve));

	tmr_cancel(&ads->tmr_rtp_timeout);
    
	voe_dec_stop(ads);

	list_unlink(&ads->le);
    
	mem_deref(ads->ve);
}

int voe_dec_alloc(struct audec_state **adsp,
		  struct media_ctx **mctxp,
		  const struct aucodec *ac,
		  const char *fmtp,
		  struct aucodec_param *prm,
		  audec_recv_h *recvh,
		  audec_err_h *errh,
		  void *arg)
{
	struct audec_state *ads;
	int err = 0;

	if (!adsp || !ac || !mctxp) {
		return EINVAL;
	}

	info("voe: dec_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	ads = (struct audec_state *)mem_zalloc(sizeof(*ads), ads_destructor);
	if (!ads)
		return ENOMEM;

	if (*mctxp) {
		ads->ve = (struct voe_channel *)mem_ref(*mctxp);
	}
	else {
		err = voe_ve_alloc(&ads->ve, ac, prm->srate, prm->pt);
		if (err) {
			goto out;
		}

		*mctxp = (struct media_ctx *)ads->ve;
	}

	list_append(&gvoe.decl, &ads->le, ads);

	ads->ac = ac;
	ads->recvh = recvh;
	ads->errh = errh;
	ads->arg = arg;
    
	tmr_init(&ads->tmr_rtp_timeout);

 out:
	if (err) {
		mem_deref(ads);
	}
	else {
		*adsp = ads;
	}

	return err;
}


int voe_dec_start(struct audec_state *ads)
{
	if (!ads)
		return EINVAL;

	info("voe: starting decoder\n");

	webrtc::CodecInst c;
	channel_settings chs;
    nw_stats nws;
    
	gvoe.base->StartReceive(ads->ve->ch);
	gvoe.base->StartPlayout(ads->ve->ch);

	gvoe.codec->GetSendCodec(ads->ve->ch, c);

	chs.channel_number_ = ads->ve->ch;
	chs.bitrate_bps_ = c.rate;
	chs.packet_size_ms_ = (c.pacsize*1000)/c.plfreq;
	chs.using_dtx_ = false;
	chs.last_rtcp_rtt = 0;
	chs.last_rtcp_ploss = 0;
	gvoe.active_channel_settings.push_back(chs);

	memset(&nws,0,sizeof(nw_stats));
	nws.channel_number_ = ads->ve->ch;
	nws.out_vol_smth_ = -1.0f;
	gvoe.nws.push_back(nws);
        
	voe_multi_party_packet_rate_control(&gvoe);
    
    voe_update_agc_settings(&gvoe);
    
#if ZETA_USE_RCV_NS
	info("voe: Enabling rcv ns in mode = %d \n", (int)ZETA_RCV_NS_MODE);

	gvoe.processing->SetRxNsStatus(ads->ve->ch, true,  ZETA_RCV_NS_MODE);
#endif

#if FORCE_RECORDING
	if ( gvoe.path_to_files ) {
#if TARGET_OS_IPHONE
		std::string prefix = "/Ios_";
#elif defined(ANDROID)
		std::string prefix = "/Android_";
#else
		std::string prefix = "Osx_";
#endif
		std::string file_in, file_out;

		file_in.insert(0, gvoe.path_to_files);
		file_in.insert(file_in.size(),prefix);
		file_in.insert(file_in.size(),"packets_in_");
		file_out.insert(0, gvoe.path_to_files);
		file_out.insert(file_out.size(),prefix);
		file_out.insert(file_out.size(),"packets_out_");

		char  buf[80];
		sprintf(buf,"ch%d_", ads->ve->ch);
		file_in.insert(file_in.size(),buf);
		file_out.insert(file_out.size(),buf);

		time_t     now = time(0);
		struct tm  tstruct;

		tstruct = *localtime(&now);
		strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

		file_in.insert(file_in.size(),buf);
		file_in.insert(file_in.size(),".rtpdump");
		file_out.insert(file_out.size(),buf);
		file_out.insert(file_out.size(),".rtpdump");

		gvoe.rtp_rtcp->StartRTPDump(ads->ve->ch, file_in.c_str(),
					   webrtc::kRtpIncoming);
		gvoe.rtp_rtcp->StartRTPDump(ads->ve->ch, file_out.c_str(),
					   webrtc::kRtpOutgoing);
	}
#endif

	return 0;
}

void voe_dec_stop(struct audec_state *ads)
{
	if (!ads)
		return;

	info("voe: stopping decoder\n");

	if (!gvoe.base)
		return;

	gvoe.base->StopPlayout(ads->ve->ch);
	gvoe.base->StopReceive(ads->ve->ch);

	for ( auto it = gvoe.active_channel_settings.begin();
	      it < gvoe.active_channel_settings.end(); it++) {
        
		if ( it->channel_number_ == ads->ve->ch) {
			gvoe.active_channel_settings.erase(it);
			break;
		}
	}

	for ( auto it = gvoe.nws.begin();
	      it != gvoe.nws.end(); it++) {
        
		if ( it->channel_number_ == ads->ve->ch) {
			gvoe.nws.erase(it);
			break;
		}
	}
	
	voe_multi_party_packet_rate_control(&gvoe);
    
	voe_update_agc_settings(&gvoe);

#if ZETA_USE_RCV_NS
	bool enabled;
	webrtc::NsModes NSmode;

	gvoe.processing->GetRxNsStatus(ads->ve->ch, enabled, NSmode);
	if (enabled) {
		info("voe: turning rcv ns off\n");
		gvoe.processing->SetRxNsStatus(ads->ve->ch, false,
					      NSmode);
	}
#endif

#if FORCE_RECORDING
	if (gvoe.rtp_rtcp) {
		gvoe.rtp_rtcp->StopRTPDump(ads->ve->ch, webrtc::kRtpIncoming);
		gvoe.rtp_rtcp->StopRTPDump(ads->ve->ch, webrtc::kRtpOutgoing);
	}
#endif
}
