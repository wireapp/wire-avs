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

#include <stdint.h>
#include "avs_ztime.h"
#include "avs_voe_stats.h"

#define INTERVAL_MS 10000
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
	float frame_cnt_buf[NBUF];
	struct max_min_avg bit_rate_stats;
	struct max_min_avg pkt_rate_stats;
	struct max_min_avg pkt_loss_stats;
	struct max_min_avg frame_rate_stats;
	int dropouts;
	uint16_t start_seq_nr;
	struct ztime start_time;
	struct ztime prev_time;
};

void rtp_stats_init(struct rtp_stats* rs, int pt, int dropout_thres_ms);

void rtp_stats_update(struct rtp_stats* rs, const uint8_t *pkt, size_t len);