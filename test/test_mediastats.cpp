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
#include <avs.h>
#include <avs_mediastats.h>
#include <gtest/gtest.h>

#define RTP_HEADER_IN_BYTES 12

static int MakeRTPheader( uint8_t* rtpHeader,
                         const uint8_t payloadType,
                         const uint16_t seqNum,
                         const uint32_t timeStamp,
                         const uint32_t ssrc){
	rtpHeader[0] = (uint8_t) 0x80;
	rtpHeader[1] = (uint8_t) (payloadType & 0xFF);
	rtpHeader[2] = (uint8_t) ((seqNum >> 8) & 0xFF);
	rtpHeader[3] = (uint8_t) (seqNum & 0xFF);
	rtpHeader[4] = (uint8_t) ((timeStamp >> 24) & 0xFF);
	rtpHeader[5] = (uint8_t) ((timeStamp >> 16) & 0xFF);
	rtpHeader[6] = (uint8_t) ((timeStamp >> 8) & 0xFF);
	rtpHeader[7] = (uint8_t) (timeStamp & 0xFF);
	rtpHeader[8] = (uint8_t) ((ssrc >> 24) & 0xFF);
	rtpHeader[9] = (uint8_t) ((ssrc >> 16) & 0xFF);
	rtpHeader[10] = (uint8_t) ((ssrc >> 8) & 0xFF);
	rtpHeader[11] = (uint8_t) (ssrc & 0xFF);
    
	return(RTP_HEADER_IN_BYTES);
}

static bool gilbert(float loss_rate, float mbl, bool prev_lost)
{
	bool ret = false;
	float ploss = loss_rate / 100.0f;
	if( ( !prev_lost  && ((float)rand()/RAND_MAX) < ploss / (1.0f - ploss) / mbl ) ||
		(  prev_lost  && ((float)rand()/RAND_MAX) < 1.0f - 1.0f / mbl ) ) {
		ret = true;
	}
	return ret;
}

TEST(mediastats, clean_channel)
{
	struct rtp_stats stats = {0};
	int pt = 55;
    
	mediastats_rtp_stats_init(&stats, pt, 1000);
    
	uint8_t packet[RTP_HEADER_IN_BYTES];
    
	uint16_t seq_nr = 32000;
	for( int i = 0; i < 1000; i++){
		MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // Not used
		seq_nr++;
        
		mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
	}
	stats.start_time.sec = 0;
	MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // Not used
    
	mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
    
	ASSERT_EQ(stats.pkt_loss_stats.min, 0);
	ASSERT_EQ(stats.pkt_loss_stats.avg, 0);
	ASSERT_EQ(stats.pkt_loss_stats.max, 0);
}

TEST(mediastats, clean_channel_wrap)
{
	struct rtp_stats stats = {0};
	int pt = 55;
    
	mediastats_rtp_stats_init(&stats, pt, 1000);
    
	uint8_t packet[RTP_HEADER_IN_BYTES];
    
	uint16_t seq_nr = (1 << 16) - 10;
	for( int i = 0; i < 1000; i++){
		MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // Not used
		seq_nr++;
        
		mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
	}
	stats.start_time.sec = 0;
	MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // Not used
    
	mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
    
	ASSERT_EQ(stats.pkt_loss_stats.min, 0);
	ASSERT_EQ(stats.pkt_loss_stats.avg, 0);
	ASSERT_EQ(stats.pkt_loss_stats.max, 0);
}


TEST(mediastats, lossy_channel)
{
	struct rtp_stats stats = {0};
	int pt = 55, lost_cnt = 0;
    
	mediastats_rtp_stats_init(&stats, pt, 1000);
    
	uint8_t packet[RTP_HEADER_IN_BYTES];

	bool lost = false;
	uint16_t seq_nr = 32000;
	for( int i = 0; i < 1000; i++){
		MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // Not used
		seq_nr++;
        
		lost = gilbert(10.0, 1.5f, lost);
		if(!lost){
			mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
		} else {
			lost_cnt++;
		}
	}
	stats.start_time.sec = 0;
	MakeRTPheader(packet,
		pt,
		seq_nr,
		0,    // Not used
		0);   // Not used
    
	mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
    
	ASSERT_GT(stats.pkt_loss_stats.avg, (float)lost_cnt/(float)10 - 1.0);
	ASSERT_LT(stats.pkt_loss_stats.avg, (float)lost_cnt/(float)10 + 1.0);
    
	ASSERT_GT(stats.pkt_mbl_stats.avg, 1.2);
	ASSERT_LT(stats.pkt_mbl_stats.avg, 2.0);
}

TEST(mediastats, lossy_channel_wrap)
{
	struct rtp_stats stats = {0};
	int pt = 55, lost_cnt = 0;
    
	mediastats_rtp_stats_init(&stats, pt, 1000);
    
	uint8_t packet[RTP_HEADER_IN_BYTES];
    
	bool lost = false;
	uint16_t seq_nr = (1 << 16) - 200;
	for( int i = 0; i < 1000; i++){
		MakeRTPheader(packet,
			pt,
			seq_nr,
			0,    // Not used
			0);   // not used

		seq_nr++;
        
		lost = gilbert(10.0, 1.5f, lost);
		if(!lost){
			mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
		} else {
			lost_cnt++;
		}
	}
	stats.start_time.sec = 0;
	MakeRTPheader(packet,
		pt,
		seq_nr,
		0,    // Not used
		0);   // not used
    
	mediastats_rtp_stats_update(&stats, packet, RTP_HEADER_IN_BYTES, 0);
    
	ASSERT_GT(stats.pkt_loss_stats.avg, (float)lost_cnt/(float)10 - 1.0);
	ASSERT_LT(stats.pkt_loss_stats.avg, (float)lost_cnt/(float)10 + 1.0);
    
	ASSERT_GT(stats.pkt_mbl_stats.avg, 1.2);
	ASSERT_LT(stats.pkt_mbl_stats.avg, 2.0);
}
