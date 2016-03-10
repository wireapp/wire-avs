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

#include <math.h>
#include <time.h>

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "avs_voe.h"
#include "voe.h"


static void ads_destructor(void *arg)
{
	struct audec_state *ads = (struct audec_state *)arg;

	debug("voe: ads_destructor: %p ve=%p(%d)\n",
	      ads, ads->ve, mem_nrefs(ads->ve));

	voe_dec_stop(ads);

	list_unlink(&ads->le);

	mem_deref(ads->ve);
}


int voe_dec_alloc(struct audec_state **adsp,
		  struct media_ctx **mctxp,
		  const struct aucodec *ac,
		  const char *fmtp, int pt, uint32_t srate, uint8_t ch,
		  audec_recv_h *recvh,
		  audec_err_h *errh,
		  void *arg)
{
	struct audec_state *ads;
	int err = 0;

	if (!adsp || !ac || !mctxp) {
		return EINVAL;
	}

	info("voe: dec_alloc: allocating codec:%s(%d)\n", ac->name, pt);

	ads = (struct audec_state *)mem_zalloc(sizeof(*ads), ads_destructor);
	if (!ads)
		return ENOMEM;

	if (*mctxp) {
		ads->ve = (struct voe_channel *)mem_ref(*mctxp);
	}
	else {
		err = voe_ve_alloc(&ads->ve, ac, srate, pt);
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

int voe_dec_stats(struct audec_state *ads, struct mbuf **mbp)
{
	struct voe_stats stats;
	struct voe_stats *vst = &stats;
	struct mbuf *mb;

	debug("voe_dec_stats: ads=%p mbp=%p\n", ads, mbp);
	
	if (!ads)
		return EINVAL;
	
	voe_stats_init(vst);
	voe_stats_calc(ads->ve->ch, vst);

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	mbuf_printf(mb,
		    "Receive Quality Statistics for ch[%d] last %d seconds\n",
		    ads->ve->ch, vst->num_measurements*NW_STATS_DELTA);
	
	mbuf_printf(mb,
		    "packet size stats 20-40-60 ms:"
		    "| %.2f  | %.2f  | %.2f | \n",
		    100.0f*(float)vst->packet_size[0]/(float)vst->num_measurements,
		    100.0f*(float)vst->packet_size[1]/(float)vst->num_measurements,
		    100.0f*(float)vst->packet_size[2]/(float)vst->num_measurements);
    
	mbuf_printf(mb,
		    "                    "
		    "              |  Avg  | Max   | Min  | Std   | \n");
	mbuf_printf(mb,
		    "RTT (ms)           :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_RTT, vst->max_RTT, vst->min_RTT, vst->std_RTT);
    mbuf_printf(mb,
		    "Jitter (samples)   :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_Jitter, vst->max_Jitter, vst->min_Jitter, vst->std_Jitter);
    mbuf_printf(mb,
		    "Uplink Packet Loss :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_UplinkPacketLossRate, vst->max_UplinkPacketLossRate, vst->min_UplinkPacketLossRate, vst->std_UplinkPacketLossRate);
    mbuf_printf(mb,
		    "Uplink Jitter      :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_UplinkJitter, vst->max_UplinkJitter, vst->min_UplinkJitter, vst->std_UplinkJitter);
	mbuf_printf(mb,
		    "Bitrate (kbps)     :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_bitrate, vst->max_bitrate, vst->min_bitrate, vst->std_bitrate);
    
	mbuf_printf(mb,
		    "Buffer Size (ms)   :"
		    "              |  %d   | %d    | %d   | %.2f   | \n",
		    vst->avg_currentBufferSize,
		    vst->max_currentBufferSize,
		    vst->min_currentBufferSize,
		    vst->std_currentBufferSize);

    mbuf_printf(mb,
		    "Jitter peak rate   :"
		    "              |  %d   | na    | na   | na   | \n",
		    (int)(vst->avg_jitterPeaksFound * 100.0f));
    
	mbuf_printf(mb,
		    "Packet Loss Rate   :"
		    "              |  %.2f | %.2f  | %.2f | %.2f   | \n",
		    (float)vst->avg_PacketLossRate/163.84f,
		    (float)vst->max_PacketLossRate/163.84f,
		    (float)vst->min_PacketLossRate/163.84f,
		    vst->std_PacketLossRate/163.84f);
    
	mbuf_printf(mb,
		    "FEC Corrected Rate :"
		    "              |  %.2f | %.2f  | %.2f | %.2f   | \n",
		    (float)vst->avg_SecondaryDecodedRate/163.84f,
		    (float)vst->max_SecondaryDecodedRate/163.84f,
		    (float)vst->min_SecondaryDecodedRate/163.84f,
		    vst->std_SecondaryDecodedRate/163.84f);
    
	mbuf_printf(mb,
		    "Expand Rate        :"
		    "              |  %.2f | %.2f  | %.2f | %.2f   | \n",
		    (float)vst->avg_ExpandRate/163.84f,
		    (float)vst->max_ExpandRate/163.84f,
		    (float)vst->min_ExpandRate/163.84f,
		    vst->std_ExpandRate/163.84f);
    
	mbuf_printf(mb,
		    "Accelerate Rate    :"
		    "              |  %.2f | %.2f  | %.2f | %.2f   | \n",
		    (float)vst->avg_AccelerateRate/163.84f,
		    (float)vst->max_AccelerateRate/163.84f,
		    (float)vst->min_AccelerateRate/163.84f,
		    vst->std_AccelerateRate/163.84f);
    
	mbuf_printf(mb,
		    "Preemptive Rate    :"
		    "              |  %.2f | %.2f  | %.2f | %.2f   | \n",
		    (float)vst->avg_PreemptiveRate/163.84f,
		    (float)vst->max_PreemptiveRate/163.84f,
		    (float)vst->min_PreemptiveRate/163.84f,
		    vst->std_PreemptiveRate/163.84f);

	info("----------------------------------------------------------\n");
	info("%b", mb->buf, mb->end); 
	info("-----------------------------------------------------------\n");

	if (!mbp) {
		mem_deref(mb);
		
		return EINVAL;
	}
	else {
		mb->pos = 0;
		*mbp = mb;
		
		return 0;
	}
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

void voe_stats_init(struct voe_stats *vst)
{
    if (!vst)
        return;
    
    memset(vst,0,sizeof(voe_stats));
    
    vst->min_currentBufferSize = (uint16_t)1 << 15;
    vst->min_PacketLossRate = (uint16_t)1 << 15;
    vst->min_ExpandRate = (uint16_t)1 << 15;
    vst->min_AccelerateRate = (uint16_t)1 << 15;
    vst->min_PreemptiveRate = (uint16_t)1 << 15;
    vst->min_SecondaryDecodedRate = (uint16_t)1 << 15;
    vst->min_RTT = (int64_t)1 << 15;
    vst->min_Jitter = (uint32_t)1 << 31;
    vst->min_UplinkPacketLossRate = (uint16_t)1 << 15;
    vst->min_UplinkJitter = (uint32_t)1 << 31;
}

void voe_stats_calc(int ch, struct voe_stats *vst)
{
    uint16_t tmpu16;
    for ( auto it = gvoe.nws.begin();
         it < gvoe.nws.end(); it++) {
        
        if ( it->channel_number_ == ch) {
            for(int i = 0; i < NUM_STATS; i++){
                if(it->nw_stats[i].neteq_nw_stats.packetSizeMs == 0){
                    break;
                }
                vst->num_measurements++;

                if(it->nw_stats[i].neteq_nw_stats.packetSizeMs == 20){
                    vst->packet_size[0]++;
                } else if(it->nw_stats[i].neteq_nw_stats.packetSizeMs == 40){
                    vst->packet_size[1]++;
                } else {
                    vst->packet_size[2]++;
                }

                vst->avg_bitrate += it->nw_stats[i].neteq_nw_stats.rcvBitRateKbps;
                vst->max_bitrate = std::max(it->nw_stats[i].neteq_nw_stats.rcvBitRateKbps, vst->max_bitrate);
                vst->min_bitrate = std::min(it->nw_stats[i].neteq_nw_stats.rcvBitRateKbps, vst->min_bitrate);

                vst->avg_currentBufferSize += it->nw_stats[i].neteq_nw_stats.currentBufferSize;
                vst->max_currentBufferSize = std::max(it->nw_stats[i].neteq_nw_stats.currentBufferSize, vst->max_currentBufferSize);
                vst->min_currentBufferSize = std::min(it->nw_stats[i].neteq_nw_stats.currentBufferSize, vst->min_currentBufferSize);

                vst->avg_PacketLossRate += it->nw_stats[i].neteq_nw_stats.currentPacketLossRate;
                vst->max_PacketLossRate = std::max(it->nw_stats[i].neteq_nw_stats.currentPacketLossRate, vst->max_PacketLossRate);
                vst->min_PacketLossRate = std::min(it->nw_stats[i].neteq_nw_stats.currentPacketLossRate, vst->min_PacketLossRate);

                vst->avg_ExpandRate += it->nw_stats[i].neteq_nw_stats.currentExpandRate;
                vst->max_ExpandRate = std::max(it->nw_stats[i].neteq_nw_stats.currentExpandRate, vst->max_ExpandRate);
                vst->min_ExpandRate = std::min(it->nw_stats[i].neteq_nw_stats.currentExpandRate, vst->min_ExpandRate);

                vst->avg_AccelerateRate += it->nw_stats[i].neteq_nw_stats.currentAccelerateRate;
                vst->max_AccelerateRate = std::max(it->nw_stats[i].neteq_nw_stats.currentAccelerateRate, vst->max_AccelerateRate);
                vst->min_AccelerateRate = std::min(it->nw_stats[i].neteq_nw_stats.currentAccelerateRate, vst->min_AccelerateRate);
                
                vst->avg_PreemptiveRate += it->nw_stats[i].neteq_nw_stats.currentPreemptiveRate;
                vst->max_PreemptiveRate = std::max(it->nw_stats[i].neteq_nw_stats.currentPreemptiveRate, vst->max_PreemptiveRate);
                vst->min_PreemptiveRate = std::min(it->nw_stats[i].neteq_nw_stats.currentPreemptiveRate, vst->min_PreemptiveRate);

                vst->avg_SecondaryDecodedRate += it->nw_stats[i].neteq_nw_stats.currentSecondaryDecodedRate;
                vst->max_SecondaryDecodedRate = std::max(it->nw_stats[i].neteq_nw_stats.currentSecondaryDecodedRate, vst->max_SecondaryDecodedRate);
                vst->min_SecondaryDecodedRate = std::min(it->nw_stats[i].neteq_nw_stats.currentSecondaryDecodedRate, vst->min_SecondaryDecodedRate);

                vst->avg_RTT += it->nw_stats[i].Rtt_ms;
                vst->max_RTT = std::max((int32_t)it->nw_stats[i].Rtt_ms, vst->max_RTT);
                vst->min_RTT = std::min((int32_t)it->nw_stats[i].Rtt_ms, vst->min_RTT);
                
                vst->avg_Jitter += it->nw_stats[i].jitter_smpls;
                vst->max_Jitter = std::max((uint32_t)it->nw_stats[i].jitter_smpls, vst->max_Jitter);
                vst->min_Jitter = std::min((uint32_t)it->nw_stats[i].jitter_smpls, vst->min_Jitter);
                
                tmpu16 = (it->nw_stats[i].uplink_loss_q8 * 100) >> 8; /* q8 fraction to pct */
                vst->avg_UplinkPacketLossRate += it->nw_stats[i].uplink_loss_q8;
                vst->max_UplinkPacketLossRate = std::max(tmpu16, vst->max_UplinkPacketLossRate);
                vst->min_UplinkPacketLossRate = std::min(tmpu16, vst->min_UplinkPacketLossRate);
                
                vst->avg_UplinkJitter += it->nw_stats[i].uplink_jitter_smpls;
                vst->max_UplinkJitter = std::max((uint32_t)it->nw_stats[i].uplink_jitter_smpls, vst->max_UplinkJitter);
                vst->min_UplinkJitter = std::min((uint32_t)it->nw_stats[i].uplink_jitter_smpls, vst->min_UplinkJitter);
                
                if(it->nw_stats[i].neteq_nw_stats.jitterPeaksFound){
                    vst->avg_jitterPeaksFound += 1.0f;
                }
            }
            if(vst->num_measurements > 0) {
                vst->avg_bitrate /= vst->num_measurements; // maybe calculate 1/num_measurements up front
                vst->avg_currentBufferSize /= vst->num_measurements;
                vst->avg_PacketLossRate /= vst->num_measurements;
                vst->avg_ExpandRate /= vst->num_measurements;
                vst->avg_AccelerateRate /= vst->num_measurements;
                vst->avg_PreemptiveRate /= vst->num_measurements;
                vst->avg_SecondaryDecodedRate /= vst->num_measurements;
                vst->avg_RTT /= vst->num_measurements;
                vst->avg_Jitter /= vst->num_measurements;
                vst->avg_UplinkPacketLossRate /= vst->num_measurements;
                vst->avg_UplinkJitter /= vst->num_measurements;
                vst->avg_jitterPeaksFound /= vst->num_measurements;
                // Calculate the variance
                float tmp;
                vst->std_bitrate = 0.0;
                vst->std_currentBufferSize = 0.0;
                vst->std_PacketLossRate = 0.0f;
                vst->std_ExpandRate = 0.0;
                vst->std_AccelerateRate = 0.0f;
                vst->std_PreemptiveRate = 0.0f;
                vst->std_SecondaryDecodedRate = 0.0f;
                vst->std_RTT = 0.0f;
                vst->std_Jitter = 0.0f;
                vst->std_UplinkPacketLossRate = 0.0f;
                vst->std_UplinkJitter = 0.0f;
                for(int i = 0; i < vst->num_measurements; i++){
                    tmp = (float)(vst->avg_bitrate - it->nw_stats[i].neteq_nw_stats.rcvBitRateKbps);
                    vst->std_bitrate += (tmp*tmp);
                    tmp = (float)(vst->avg_currentBufferSize - it->nw_stats[i].neteq_nw_stats.currentBufferSize);
                    vst->std_currentBufferSize += (tmp*tmp);
                    tmp = (float)(vst->avg_PacketLossRate - it->nw_stats[i].neteq_nw_stats.currentPacketLossRate);
                    vst->std_PacketLossRate += (tmp*tmp);
                    tmp = (float)(vst->avg_ExpandRate - it->nw_stats[i].neteq_nw_stats.currentExpandRate);
                    vst->std_ExpandRate += (tmp*tmp);
                    tmp = (float)(vst->avg_AccelerateRate - it->nw_stats[i].neteq_nw_stats.currentAccelerateRate);
                    vst->std_AccelerateRate += (tmp*tmp);
                    tmp = (float)(vst->avg_PreemptiveRate - it->nw_stats[i].neteq_nw_stats.currentPreemptiveRate);
                    vst->std_PreemptiveRate += (tmp*tmp);
                    tmp = (float)(vst->avg_SecondaryDecodedRate - it->nw_stats[i].neteq_nw_stats.currentSecondaryDecodedRate);
                    vst->std_SecondaryDecodedRate += (tmp*tmp);
                    tmp = (float)(vst->avg_RTT - it->nw_stats[i].Rtt_ms);
                    vst->std_RTT += (tmp*tmp);
                    tmp = ((float)vst->avg_Jitter - (float)it->nw_stats[i].jitter_smpls);
                    vst->std_Jitter += (tmp*tmp);
                    tmp = ((float)vst->avg_UplinkPacketLossRate - (float)it->nw_stats[i].uplink_loss_q8);
                    vst->std_UplinkPacketLossRate += (tmp*tmp);
                    tmp = ((float)vst->avg_UplinkJitter - (float)it->nw_stats[i].uplink_jitter_smpls);
                    vst->std_UplinkJitter += (tmp*tmp);
                }
                vst->std_bitrate = sqrt(vst->std_bitrate/vst->num_measurements);
                vst->std_currentBufferSize = sqrt(vst->std_currentBufferSize/vst->num_measurements);
                vst->std_PacketLossRate = sqrt(vst->std_PacketLossRate/vst->num_measurements);
                vst->std_ExpandRate = sqrt(vst->std_ExpandRate/vst->num_measurements);
                vst->std_AccelerateRate = sqrt(vst->std_AccelerateRate/vst->num_measurements);
                vst->std_PreemptiveRate = sqrt(vst->std_PreemptiveRate/vst->num_measurements);
                vst->std_SecondaryDecodedRate = sqrt(vst->std_SecondaryDecodedRate/vst->num_measurements);
                vst->std_RTT = sqrt(vst->std_RTT/vst->num_measurements);
                vst->std_Jitter = sqrt(vst->std_Jitter/vst->num_measurements);
                vst->avg_UplinkPacketLossRate = (100 * vst->avg_UplinkPacketLossRate) >> 8; /* q8 fraction to pct */
                vst->std_UplinkPacketLossRate = sqrt(vst->std_UplinkPacketLossRate/vst->num_measurements);
                vst->std_UplinkPacketLossRate = vst->std_UplinkPacketLossRate * 0.3906f; /* q8 fraction to pct */
                vst->std_UplinkJitter = sqrt(vst->std_UplinkJitter/vst->num_measurements);
            }
	    
            gvoe.nws.erase(it);
            break;
        }
    }
}
