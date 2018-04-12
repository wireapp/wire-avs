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
}
#include "voe.h"
#include "voe_settings.h"

static void cd_destructor(void *arg)
{
    struct channel_data *cd = (struct channel_data *)arg;

    list_unlink(&cd->le);
}

int channel_data_add(struct list *ch_list, int ch, webrtc::CodecInst &c)
{
    struct channel_data *cd;
    int err;
    
    cd = (struct channel_data *)mem_zalloc(sizeof(*cd), cd_destructor);
    if (!cd)
        return ENOMEM;
    
    cd->channel_number = ch;
    cd->bitrate_bps = c.rate;
    cd->packet_size_ms = (c.pacsize*1000)/c.plfreq;
    cd->using_dtx = false;
    cd->last_rtcp_rtt = 0;
    cd->last_rtcp_ploss = 0;
    cd->interrupted = false;
    cd->out_vol_smth = -1.0f;
    
    list_append(ch_list, &cd->le, cd);
    
    return 0;
}


struct channel_data *find_channel_data(struct list *active_chs, int ch)
{
    struct le *le;
    
    if (!active_chs || ch < 0)
        return NULL;
    
    for (le = list_head(active_chs); le; le = le->next) {
        struct channel_data *cd = (struct channel_data *)le->data;
        
        if(cd->channel_number == ch)
            return cd;
    }
    
    return NULL;
}

void voe_set_channel_load(struct voe *voe)
{
    webrtc::CodecInst c;
    
    int bitrate_bps = voe->manual_bitrate_bps ? voe->manual_bitrate_bps : voe->bitrate_bps;
    int packet_size_ms = voe->manual_packet_size_ms ? voe->manual_packet_size_ms :
    std::max( voe->packet_size_ms, voe->min_packet_size_ms );
    
    struct le *le;
    for (le = gvoe.channel_data_list.head; le; le = le->next) {
        struct channel_data *cd = (struct channel_data *)le->data;
        
        gvoe.codec->GetSendCodec(cd->channel_number, c);
        int tgt_pacsize = (c.plfreq * packet_size_ms) / 1000;

        if (c.pacsize != tgt_pacsize || c.rate != bitrate_bps) {
            c.pacsize = (c.plfreq * packet_size_ms) / 1000;
            c.rate = bitrate_bps;
            gvoe.codec->SetSendCodec(cd->channel_number, c);
        
            info("voe: Changing codec settings parameters for channel %d\n", cd->channel_number);
            info("voe: pltype = %d \n", c.pltype);
            info("voe: plname = %s \n", c.plname);
            info("voe: plfreq = %d \n", c.plfreq);
            info("voe: pacsize = %d (%d ms)\n", c.pacsize, c.pacsize * 1000 / c.plfreq);
            info("voe: channels = %d \n", c.channels);
            info("voe: rate = %d \n", c.rate);
        }
    }
}

void voe_multi_party_packet_rate_control(struct voe *voe)
{
#define ACTIVE_FLOWS_FOR_40MS_PACKETS 2
#define ACTIVE_FLOWS_FOR_60MS_PACKETS 4
    
    webrtc::CodecInst c;
    
    /* Change Packet size based on amount of flows in use */
    int active_flows = list_count(&voe->channel_data_list);
    int min_packet_size_ms = 20;
    
    if ( active_flows >= ACTIVE_FLOWS_FOR_60MS_PACKETS ) {
        min_packet_size_ms = 60;
    }
    else if( active_flows >=  ACTIVE_FLOWS_FOR_40MS_PACKETS) {
        min_packet_size_ms = 40;
    }
    
    voe->min_packet_size_ms = min_packet_size_ms;
    voe_set_channel_load(voe);
}

#define SWITCH_TO_SHORTER_PACKETS_RTT_MS  500
#define SWITCH_TO_LONGER_PACKETS_RTT_MS   800

void voe_update_channel_stats(struct voe *voe, int ch_id, int rtcp_rttMs, int rtcp_loss_Q8)
{
    int rtt_ms = 0, frac_lost_Q8 = 0;
    struct le *le;
    for (le = voe->channel_data_list.head; le; le = le->next) {
        struct channel_data *cd = (struct channel_data *)le->data;
        if( cd->channel_number == ch_id ) {
            cd->last_rtcp_rtt = rtcp_rttMs;
            cd->last_rtcp_ploss = rtcp_loss_Q8;
        }
        rtt_ms = std::max(rtt_ms, cd->last_rtcp_rtt);
        frac_lost_Q8 = std::max(frac_lost_Q8, cd->last_rtcp_ploss);
    }
    int packet_size_ms = gvoe.packet_size_ms;
    if( rtt_ms < SWITCH_TO_SHORTER_PACKETS_RTT_MS && frac_lost_Q8 < (int)(0.03 * 255) ) {
        packet_size_ms -= 20;
    } else
    if( rtt_ms > SWITCH_TO_LONGER_PACKETS_RTT_MS || frac_lost_Q8 > (int)(0.10 * 255) ) {
        packet_size_ms += 20;
    }
    packet_size_ms = std::max( packet_size_ms, voe->min_packet_size_ms );
    packet_size_ms = std::min( packet_size_ms, 40 );
    if( packet_size_ms != voe->packet_size_ms ) {
        voe->packet_size_ms = packet_size_ms;
        voe->bitrate_bps = packet_size_ms == 20 ? ZETA_OPUS_BITRATE_HI_BPS : ZETA_OPUS_BITRATE_LO_BPS;
        voe_set_channel_load(voe);
    }
}
