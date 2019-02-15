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
extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_ztime.h"    
}
#include "avs_mediastats.h"
#include "voe.h"

#include <math.h>

static bool stats_available(int ch)
{
    bool ret = false;
    struct channel_data *cd = find_channel_data(&gvoe.channel_data_list, ch);
    if(cd){
        ret = true;
    }
    return ret;
}

int voe_get_stats(struct audec_state *ads, struct aucodec_stats *new_stats)
{
    struct voe_stats stats;
    int err;
    
    if (!ads || !ads->ve)
        return EINVAL;
    
    if(!stats_available(ads->ve->ch)){
        return EINVAL;
    }
    
    voe_stats_init(&stats);
    err = voe_stats_calc(ads->ve->ch, &stats);
    if (err)
	    return err;

    bool in_valid = false;

    if(stats.min_currentBufferSize == (uint16_t)1 << 15){
	    in_valid = true;
    }

    new_stats->in_vol.avg = in_valid ? -1 : stats.avg_InVol;
    new_stats->in_vol.max = in_valid ? -1 : stats.max_InVol;
    new_stats->in_vol.min = in_valid ? -1 : stats.min_InVol;
    new_stats->out_vol.avg = in_valid ? -1 : stats.avg_OutVol;
    new_stats->out_vol.max = in_valid ? -1 : stats.max_OutVol;
    new_stats->out_vol.min = in_valid ? -1 : stats.min_OutVol;
    new_stats->loss_d.avg = in_valid ? -1 : stats.avg_PacketLossRate/164;
    new_stats->loss_d.max = in_valid ? -1 : stats.max_PacketLossRate/164;
    new_stats->loss_d.min = in_valid ? -1 : stats.min_PacketLossRate/164;
    new_stats->loss_u.avg = in_valid ? -1 : stats.avg_UplinkPacketLossRate;
    new_stats->loss_u.max = in_valid ? -1 : stats.max_UplinkPacketLossRate;
    new_stats->loss_u.min = in_valid ? -1 : stats.min_UplinkPacketLossRate;
    new_stats->rtt.avg = in_valid ? -1 : stats.avg_RTT;
    new_stats->rtt.max = in_valid ? -1 : stats.max_RTT;
    new_stats->rtt.min = in_valid ? -1 : stats.min_RTT;
    new_stats->jb_size.avg = in_valid ? -1 : stats.avg_currentBufferSize;
    new_stats->jb_size.max = in_valid ? -1 : stats.max_currentBufferSize;
    new_stats->jb_size.min = in_valid ? -1 : stats.min_currentBufferSize;
    new_stats->test_score = gvoe.autest.test_score;
    strcpy(new_stats->audio_route, gvoe.playout_device);

    new_stats->quality.downloss = stats.quality.downloss;
    new_stats->quality.uploss = stats.quality.uploss;
    new_stats->quality.rtt = stats.quality.rtt;

    return 0;
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
    vst->min_InVol = (uint16_t)1 << 15;
    vst->min_OutVol = (uint16_t)1 << 15;
}

int voe_stats_calc(int ch, struct voe_stats *vst)
{
    uint16_t tmpu16;
    int cnt;
    struct channel_data *cd = find_channel_data(&gvoe.channel_data_list, ch);
    
    if (!cd)
	    return ENOENT;    

    vst->quality.downloss = cd->quality.downloss;
    vst->quality.uploss = cd->quality.uploss;
    vst->quality.rtt = cd->quality.rtt;
    
    cnt = (cd->stats_cnt > NUM_STATS) ? NUM_STATS : cd->stats_cnt;
    for(int i = 0; i < cnt; i++){
            vst->num_measurements++;
                
            vst->avg_currentBufferSize +=
		    cd->ch_stats[i].neteq_nw_stats.currentBufferSize;
            vst->max_currentBufferSize =
		    std::max(cd->ch_stats[i].neteq_nw_stats.currentBufferSize,
			     vst->max_currentBufferSize);
            vst->min_currentBufferSize =
		    std::min(cd->ch_stats[i].neteq_nw_stats.currentBufferSize,
			     vst->min_currentBufferSize);                
            vst->avg_PacketLossRate +=
		    cd->ch_stats[i].neteq_nw_stats.currentPacketLossRate;
            vst->max_PacketLossRate =
		  std::max(cd->ch_stats[i].neteq_nw_stats.currentPacketLossRate,
			   vst->max_PacketLossRate);
            vst->min_PacketLossRate =
		  std::min(cd->ch_stats[i].neteq_nw_stats.currentPacketLossRate,
			   vst->min_PacketLossRate);                
            vst->avg_ExpandRate +=
		    cd->ch_stats[i].neteq_nw_stats.currentExpandRate;
            vst->max_ExpandRate =
		    std::max(cd->ch_stats[i].neteq_nw_stats.currentExpandRate,
			     vst->max_ExpandRate);
            vst->min_ExpandRate =
		    std::min(cd->ch_stats[i].neteq_nw_stats.currentExpandRate,
			     vst->min_ExpandRate);                
            vst->avg_AccelerateRate +=
		    cd->ch_stats[i].neteq_nw_stats.currentAccelerateRate;
            vst->max_AccelerateRate =
		  std::max(cd->ch_stats[i].neteq_nw_stats.currentAccelerateRate,
			   vst->max_AccelerateRate);
            vst->min_AccelerateRate =
		  std::min(cd->ch_stats[i].neteq_nw_stats.currentAccelerateRate,
			   vst->min_AccelerateRate);
            vst->avg_PreemptiveRate +=
		    cd->ch_stats[i].neteq_nw_stats.currentPreemptiveRate;
            vst->max_PreemptiveRate =
		  std::max(cd->ch_stats[i].neteq_nw_stats.currentPreemptiveRate,
			   vst->max_PreemptiveRate);
            vst->min_PreemptiveRate =
		  std::min(cd->ch_stats[i].neteq_nw_stats.currentPreemptiveRate,
			   vst->min_PreemptiveRate);                
            vst->avg_SecondaryDecodedRate +=
		    cd->ch_stats[i].neteq_nw_stats.currentSecondaryDecodedRate;
            vst->max_SecondaryDecodedRate =
		    std::max(cd->ch_stats[i].neteq_nw_stats.currentSecondaryDecodedRate,
			     vst->max_SecondaryDecodedRate);
            vst->min_SecondaryDecodedRate =
		    std::min(cd->ch_stats[i].neteq_nw_stats.currentSecondaryDecodedRate,
			     vst->min_SecondaryDecodedRate);
                
            vst->avg_RTT += cd->ch_stats[i].Rtt_ms;
            vst->max_RTT = std::max((int32_t)cd->ch_stats[i].Rtt_ms, vst->max_RTT);
            vst->min_RTT = std::min((int32_t)cd->ch_stats[i].Rtt_ms, vst->min_RTT);
                
            vst->avg_Jitter += cd->ch_stats[i].jitter_smpls;
            vst->max_Jitter = std::max((uint32_t)cd->ch_stats[i].jitter_smpls, vst->max_Jitter);
            vst->min_Jitter = std::min((uint32_t)cd->ch_stats[i].jitter_smpls, vst->min_Jitter);
                
            tmpu16 = (cd->ch_stats[i].uplink_loss_q8 * 100) >> 8; /* q8 fraction to pct */
            vst->avg_UplinkPacketLossRate += tmpu16;
            vst->max_UplinkPacketLossRate = std::max(tmpu16, vst->max_UplinkPacketLossRate);
            vst->min_UplinkPacketLossRate = std::min(tmpu16, vst->min_UplinkPacketLossRate);
                
            vst->avg_UplinkJitter += cd->ch_stats[i].uplink_jitter_smpls;
            vst->max_UplinkJitter = std::max((uint32_t)cd->ch_stats[i].uplink_jitter_smpls, vst->max_UplinkJitter);
            vst->min_UplinkJitter = std::min((uint32_t)cd->ch_stats[i].uplink_jitter_smpls, vst->min_UplinkJitter);
                
            vst->avg_InVol += cd->ch_stats[i].in_vol;
            vst->max_InVol = std::max((uint16_t)cd->ch_stats[i].in_vol, vst->max_InVol);
            vst->min_InVol = std::min((uint16_t)cd->ch_stats[i].in_vol, vst->min_InVol);
                
            vst->avg_OutVol += cd->ch_stats[i].out_vol;
            vst->max_OutVol = std::max((uint16_t)cd->ch_stats[i].out_vol, vst->max_OutVol);
            vst->min_OutVol = std::min((uint16_t)cd->ch_stats[i].out_vol, vst->min_OutVol);
                
            if(cd->ch_stats[i].neteq_nw_stats.jitterPeaksFound){
                vst->avg_jitterPeaksFound += 1.0f;
            }
        }
        if(vst->num_measurements > 0) {
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
            vst->avg_InVol /= vst->num_measurements;
            vst->avg_OutVol /= vst->num_measurements;
            vst->avg_jitterPeaksFound /= vst->num_measurements;
            // Calculate the variance
            float tmp;
            vst->std_currentBufferSize = 0.0;
            vst->std_PacketLossRate = 0.0f;
            vst->std_ExpandRate = 0.0;
            vst->std_AccelerateRate = 0.0f;
            vst->std_PreemptiveRate = 0.0f;
            vst->std_SecondaryDecodedRate = 0.0f;
            vst->std_RTT = 0.0f;
            vst->std_Jitter = 0.0f;
            vst->std_UplinkPacketLossRate = 0.0f;
            vst->std_InVol = 0.0f;
            vst->std_OutVol = 0.0f;
            vst->std_UplinkJitter = 0.0f;
            for(int i = 0; i < vst->num_measurements; i++){
                tmp = (float)(vst->avg_currentBufferSize - cd->ch_stats[i].neteq_nw_stats.currentBufferSize);
                vst->std_currentBufferSize += (tmp*tmp);
                tmp = (float)(vst->avg_PacketLossRate - cd->ch_stats[i].neteq_nw_stats.currentPacketLossRate);
                vst->std_PacketLossRate += (tmp*tmp);
                tmp = (float)(vst->avg_ExpandRate - cd->ch_stats[i].neteq_nw_stats.currentExpandRate);
                vst->std_ExpandRate += (tmp*tmp);
                tmp = (float)(vst->avg_AccelerateRate - cd->ch_stats[i].neteq_nw_stats.currentAccelerateRate);
                vst->std_AccelerateRate += (tmp*tmp);
                tmp = (float)(vst->avg_PreemptiveRate - cd->ch_stats[i].neteq_nw_stats.currentPreemptiveRate);
                vst->std_PreemptiveRate += (tmp*tmp);
                tmp = (float)(vst->avg_SecondaryDecodedRate - cd->ch_stats[i].neteq_nw_stats.currentSecondaryDecodedRate);
                vst->std_SecondaryDecodedRate += (tmp*tmp);
                tmp = (float)(vst->avg_RTT - cd->ch_stats[i].Rtt_ms);
                vst->std_RTT += (tmp*tmp);
                tmp = ((float)vst->avg_Jitter - (float)cd->ch_stats[i].jitter_smpls);
                vst->std_Jitter += (tmp*tmp);
                tmp = ((float)vst->avg_UplinkPacketLossRate - (float)cd->ch_stats[i].uplink_loss_q8);
                vst->std_UplinkPacketLossRate += (tmp*tmp);
                tmp = ((float)vst->avg_UplinkJitter - (float)cd->ch_stats[i].uplink_jitter_smpls);
                vst->std_UplinkJitter += (tmp*tmp);
                tmp = ((float)vst->avg_InVol - (float)cd->ch_stats[i].in_vol);
                vst->std_InVol += (tmp*tmp);
                tmp = ((float)vst->avg_OutVol - (float)cd->ch_stats[i].out_vol);
                vst->std_OutVol += (tmp*tmp);
            }
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
            vst->std_InVol = sqrt(vst->std_InVol/vst->num_measurements);
            vst->std_OutVol = sqrt(vst->std_OutVol/vst->num_measurements);
        }

	return 0;
}
