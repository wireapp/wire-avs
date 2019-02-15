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

#ifndef AVS_MEDIASTATS_H
#define AVS_MEDIASTATS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "avs_ztime.h"
    
struct max_min_avg{
	float max;
	float min;
	float avg;
};
        
struct aucodec_stats {
	struct max_min_avg out_vol;
	struct max_min_avg in_vol;
	struct max_min_avg loss_d;
	struct max_min_avg loss_u;
	struct max_min_avg rtt;
	struct max_min_avg jb_size;
	int16_t test_score;
	char audio_route[1024];

	struct {
		int downloss;
		int uploss;
		int rtt;
	} quality;
};
    
#define MAX_PACKETS_PER_SEC 100
#define INTERVAL_MS 10000
#define MAX_PACKETS (INTERVAL_MS/1000)*MAX_PACKETS_PER_SEC
#define LOG2_NBUF 5
#define NBUF (1 << LOG2_NBUF)
#define CNT_MASK (NBUF-1)
    
// We calculate statistics for last 320 seconds ie ~ 5 minutes
    
struct rtp_stats {
    int byte_cnt;
    int packet_cnt;
    int frame_cnt;
    int idx;
    int n;
    int pt;
    int dropout_thres_ms;
    float bit_rate_buf[NBUF];
    float pkt_rate_buf[NBUF];
    float pkt_loss_buf[NBUF];
    float pkt_mbl_buf[NBUF];
    float frame_cnt_buf[NBUF];
    float bw_alloc_buf[NBUF];
    struct max_min_avg bit_rate_stats;
    struct max_min_avg pkt_rate_stats;
    struct max_min_avg pkt_loss_stats;
    struct max_min_avg pkt_mbl_stats;
    struct max_min_avg frame_rate_stats;
    struct max_min_avg bw_alloc_stats;
    int dropouts;
    struct ztime start_time;
    struct ztime prev_time;
    int seq_nr_buf[MAX_PACKETS];
};
    
void mediastats_rtp_stats_init(struct rtp_stats* rs, int pt, int dropout_thres_ms);
    
void mediastats_rtp_stats_update(struct rtp_stats* rs, const uint8_t *pkt, size_t len,
	uint32_t bw_alloc_bps);
    
#ifdef __cplusplus
}
#endif

#endif // AVS_VOE_STATS_H
