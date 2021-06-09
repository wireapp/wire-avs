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
#include <cerrno>
#include <cstddef>
#include <stdio.h>
#include <string>
#include <math.h>

#include <sys/time.h>

#include "webrtc/modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "webrtc/modules/remote_bitrate_estimator/remote_bitrate_estimator_abs_send_time.h"
#include "webrtc/modules/rtp_rtcp/include/rtp_header_parser.h"
#include "webrtc/modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/system_wrappers/include/clock.h"

#include "gtest/gtest.h"

struct bwe_stats{
    uint16_t min_kbps;
    uint16_t avg_kbps;
    uint16_t max_kbps;
};

/* Bandwith Observer */
namespace webrtc {
    class BwObserver : public RemoteBitrateObserver {
    public:
        BwObserver(){
            cnt_ = 0;
            max_ = 0;
            min_ = 32000;
            tot_ = 0;
        }
            
        ~BwObserver(){
        }
            
        void OnReceiveBitrateChanged(const std::vector<uint32_t>& ssrcs,
                                         uint32_t bitrate)
        {
            uint16_t rate_kbps = bitrate/1000;
            cnt_++;
            tot_ += rate_kbps;
            if (rate_kbps > max_){
                max_ = rate_kbps;
            }
            if (rate_kbps < min_) {
                min_ = rate_kbps;
            }
            return;
        }
            
        void GetStats(struct bwe_stats *stats)
        {
            stats->min_kbps = min_;
            stats->max_kbps = max_;
            stats->avg_kbps = tot_/cnt_;
        }
            
    private:
        uint16_t cnt_;
        uint16_t max_;
        uint16_t min_;
        uint32_t tot_;
    };
};

static int read_uint16(FILE *fp, uint16_t *out)
{
    uint8_t buf[2];
    uint16_t ret;
        
    if(fread(buf, sizeof(buf), 1, fp) != 1){
        return -1;
    }
        
    *out = (uint16_t)buf[1];
    *out += ((uint16_t)buf[0]) << 8;
        
    return 0;
}
    
static uint32_t read_uint32(FILE *fp)
{
    uint8_t buf[4];
    uint32_t ret;
        
    fread(buf, 1, sizeof(buf), fp);
        
    ret = (uint16_t)buf[3];
    ret += ((uint16_t)buf[2]) << 8;
    ret += ((uint16_t)buf[1]) << 16;
    ret += ((uint16_t)buf[0]) << 24;
    
    return ret;
}
    
static int read_one_packet(uint8_t header[], uint32_t *packetLen, uint32_t *time, FILE *fp)
{
    uint16_t length, plen;
        
    if(read_uint16(fp, &length) == -1){
        return -1;
    }
    read_uint16(fp, &plen);
    *time = read_uint32(fp);
    fread(packetLen, 1, sizeof(uint32_t), fp);
        
    fread(header, (plen - sizeof(uint32_t)), sizeof(uint8_t), fp);
        
    return (plen - sizeof(uint32_t));
}

/* Main Testing part              */
static int BWE_unit_test(
  const char* in_file_name,
  struct bwe_stats *stats
)
{
  FILE * in_file = fopen(in_file_name,"rb");
  if(in_file == NULL){
    printf("Could not open %s for reading \n", in_file_name);
    return -1;
  }
  
    uint32_t packetLen, time;
    
    webrtc::BwObserver bwo;
    webrtc::SimulatedClock clock(0);
    webrtc::RemoteBitrateEstimatorAbsSendTime bwe(&bwo);
    std::unique_ptr<webrtc::RtpHeaderParser> rtpp(webrtc::RtpHeaderParser::Create());
    
    rtpp->RegisterRtpHeaderExtension(webrtc::kRtpExtensionVideoRotation, 4);
    rtpp->RegisterRtpHeaderExtension(webrtc::kRtpExtensionAbsoluteSendTime, 7);
    
    uint8_t buf[30];
    fread(buf, 1, sizeof(buf), in_file);
    
    webrtc::RTPHeader header;
    while(1){
        int ret = read_one_packet(buf, &packetLen, &time, in_file);
        if(ret == -1){
            break;
        }
        bool valid = rtpp->Parse(buf, ret, &header);
        
        size_t payload_length = packetLen - header.headerLength;
        
        bwe.IncomingPacket( time, payload_length, header);
    }
    
    bwo.GetStats(stats);
    
    fclose(in_file);
    
    return 0;
}

TEST(bwe, file_1)
{
    struct bwe_stats stats;
    int ret = BWE_unit_test("./test/data/bwe_test_data1.rtp", &stats);
    
    ASSERT_EQ(0, ret);
    ASSERT_GT(stats.min_kbps, 400);
    ASSERT_GT(stats.avg_kbps, 500);
}
