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
