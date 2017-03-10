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

#include "avs_mediastats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>

static int cmpfunc (const void * a, const void * b)
{
	return ( *(int*)a - *(int*)b );
}

static uint8_t get_pt(const uint8_t *pkt, size_t len)
{
	uint16_t pt = pkt[1];
	return pt;
}


static uint16_t get_seqnr(const uint8_t *pkt, size_t len)
{
	uint16_t seq_nr = pkt[3] + (((uint16_t)pkt[2]) << 8);
	return seq_nr;
}


static void calc_max_min_avg(float buf[], int n, struct max_min_avg *out )
{
	float avg_ = 0.0f;
	float max_ = -1e6;
	float min_ = 1e6;
	int i;
	if (n > NBUF){
		n = NBUF;
	}
	for ( i = 0; i < n; i++) {
		avg_ += buf[i];
		if (buf[i] > max_){
			max_ = buf[i];
		}
		if (buf[i] < min_){
			min_ = buf[i];
		}
	}
	avg_ = avg_ / n;
	out->max = max_;
	out->min = min_;
	out->avg = avg_;
}


void mediastats_rtp_stats_init(struct rtp_stats* rs, int pt, int dropout_thres_ms)
{
	memset(rs,0,sizeof(struct rtp_stats));
	rs->bit_rate_stats.min = -1;
	rs->bit_rate_stats.max = -1;
	rs->bit_rate_stats.avg = -1;
	rs->pkt_rate_stats.min = -1;
	rs->pkt_rate_stats.max = -1;
	rs->pkt_rate_stats.avg = -1;
	rs->pkt_loss_stats.min = -1;
	rs->pkt_loss_stats.max = -1;
	rs->pkt_loss_stats.avg = -1;
	rs->frame_rate_stats.min = -1;
	rs->frame_rate_stats.max = -1;
	rs->frame_rate_stats.avg = -1;
	rs->bw_alloc_stats.min = -1;
	rs->bw_alloc_stats.max = -1;
	rs->bw_alloc_stats.avg = -1;
	rs->idx = 0;
	rs->n = 0;
	rs->dropouts = 0;
	rs->pt = pt;
	rs->dropout_thres_ms = dropout_thres_ms;
}

static void calculate_loss_and_mbl(struct rtp_stats* rs, float *loss, float *mbl, int *seq_diff){
	int packet_cnt = rs->packet_cnt;
	if(packet_cnt > MAX_PACKETS){
		packet_cnt = MAX_PACKETS;
	}
	qsort(rs->seq_nr_buf, packet_cnt, sizeof(int), cmpfunc);
	int loss_cnt = 0;
	int loss_period_cnt = 0;
	int16_t diff, tmp;
	for(int i = 0; i < packet_cnt - 1; i++){
		diff = rs->seq_nr_buf[i+1] - rs->seq_nr_buf[i] - 1;
		if(diff > 0){
			loss_cnt += diff;
			loss_period_cnt++;
		}
	}
	tmp = packet_cnt + loss_cnt;
	if(loss_period_cnt > 0){
		*mbl = (float)loss_cnt/(float)loss_period_cnt;
		*loss = (float)100.0f*loss_cnt/(float)tmp;
	} else {
		*mbl = 1.0;
		*loss = 0.0;
	}
	*seq_diff = tmp;
}

void mediastats_rtp_stats_update(struct rtp_stats* rs, const uint8_t *pkt, size_t len,
	uint32_t bw_alloc_bps)
{
	// lock ??
	if ((get_pt(pkt, len) & 0x7f) != rs->pt) {
		return;
	}

	int seq_nr = get_seqnr(pkt, len);
	if (rs->packet_cnt == 0) {
		ztime_get(&rs->start_time);
		if (rs->n == 0){
			memcpy(&rs->prev_time, &rs->start_time,
			       sizeof(struct ztime));
		}
	}
	if(rs->packet_cnt < MAX_PACKETS){
		rs->seq_nr_buf[rs->packet_cnt] = seq_nr;
	}
        
	rs->byte_cnt += len;
	rs->packet_cnt++;
	if ( get_pt(pkt, len) == (rs->pt + (1 << 7)) ) {
		rs->frame_cnt++;
	}

	struct ztime now;
	ztime_get(&now);
	int64_t diff_ms = ztime_diff(&now, &rs->prev_time);
	if (diff_ms > rs->dropout_thres_ms){
		rs->dropouts++;
	}
	memcpy(&rs->prev_time, &now, sizeof(struct ztime));
	diff_ms = ztime_diff(&now, &rs->start_time);
	if (diff_ms > INTERVAL_MS) {
		float loss_rate, mbl;
		int expected_packets;
		calculate_loss_and_mbl(rs, &loss_rate, &mbl, &expected_packets);
        
		float bit_rate = (float)((8*rs->byte_cnt)/diff_ms);
		float frame_rate = (float)((rs->frame_cnt*1000)/diff_ms);
		float packet_rate = (float)((expected_packets*1000)/diff_ms);

		rs->bit_rate_buf[rs->idx] = bit_rate;
		rs->pkt_rate_buf[rs->idx] = packet_rate;
		rs->pkt_loss_buf[rs->idx] = loss_rate;
		rs->pkt_mbl_buf[rs->idx] = mbl;
		rs->frame_cnt_buf[rs->idx] = frame_rate;
		rs->bw_alloc_buf[rs->idx] = ((float)bw_alloc_bps) / 1000;

		rs->idx++;
		rs->idx &= CNT_MASK;
		rs->n++;

		calc_max_min_avg(rs->bit_rate_buf, rs->n, &rs->bit_rate_stats);
		calc_max_min_avg(rs->pkt_rate_buf, rs->n, &rs->pkt_rate_stats);
		calc_max_min_avg(rs->pkt_loss_buf, rs->n, &rs->pkt_loss_stats);
		calc_max_min_avg(rs->pkt_mbl_buf, rs->n, &rs->pkt_mbl_stats);
		calc_max_min_avg(rs->frame_cnt_buf, rs->n,
				 &rs->frame_rate_stats);
		calc_max_min_avg(rs->bw_alloc_buf, rs->n, &rs->bw_alloc_stats);

		rs->byte_cnt = 0;
		rs->packet_cnt = 0;
		rs->frame_cnt = 0;
	}
	//unlock
}

