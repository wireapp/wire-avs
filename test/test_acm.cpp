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

#include "webrtc/modules/audio_coding/include/audio_coding_module.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/base/logging.h"
#include "nw_simulator.h"

#include "gtest/gtest.h"
#include "complexity_check.h"

#define PRINT_NETEQ_STATS

/**********************************/
/* RTP help functions             */
/**********************************/

#define RTP_HEADER_IN_BYTES 12

static int ReadRTPheader( const uint8_t* rtpHeader,
                   uint8_t* payloadType,
                   uint16_t* seqNum,
                   uint32_t* timeStamp,
                   uint32_t* ssrc){

  *payloadType = (uint8_t)rtpHeader[1];
  *seqNum      = (uint16_t)((rtpHeader[2] << 8) | rtpHeader[3]);
  *timeStamp   = (uint32_t)((rtpHeader[4] << 24) | (rtpHeader[5] << 16)  |
                          (rtpHeader[6] << 8) | rtpHeader[7]);
  *ssrc        = (uint32_t)((rtpHeader[8] << 24) | (rtpHeader[9] << 16)  |
                          (rtpHeader[10] << 8) | rtpHeader[11]);
  return(0);
}

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

/**************************************************/
/* Transport callback writes to file              */
/**************************************************/
namespace webrtc {
  class TransportCallBack : public AudioPacketizationCallback {
  public:
    TransportCallBack( FILE *file, uint32_t ssrc){
      seqNo_ = 0;
      ssrc_ = ssrc;
      totBytes_ = 0;
      totPackets_ = 0;
      outFile_ = file;
	  timeMs_ = 0;
    }
        
    int32_t SendData(
                      FrameType     frame_type,
                      uint8_t       payload_type,
                      uint32_t      timestamp,
                      const uint8_t* payload_data,
                      size_t      payload_len_bytes,
                      const RTPFragmentationHeader* fragmentation){

      if( payload_len_bytes == 0){
        // Happens when DTX is used
        return -1;
      }
		
      uint8_t packetHeader[RTP_HEADER_IN_BYTES];
      int headerLenBytes;
      int packetLenBytes;
            
      totBytes_ += payload_len_bytes;
            
      packetLenBytes = (int)payload_len_bytes;

      headerLenBytes = MakeRTPheader(packetHeader,
                                     payload_type,
                                     seqNo_++,
                                     timestamp,
                                     ssrc_);
            
      packetLenBytes += headerLenBytes;
        
      if( outFile_ != NULL ){
        fwrite( &timeMs_, sizeof(int32_t), 1, outFile_);
        fwrite( &packetLenBytes, sizeof(int), 1, outFile_);
        fwrite( packetHeader, sizeof(uint8_t), headerLenBytes, outFile_);
        fwrite( payload_data, sizeof(uint8_t), payload_len_bytes, outFile_);
      }

      totPackets_++;
		
      return 0;
    }
    
    int32_t get_total_bytes(){
      return(totBytes_);
    }

    int32_t get_number_of_packets(){
      return totPackets_;
    }
	  
    void increment_time(int increment_ms){
      timeMs_ += increment_ms;
    }
            
  private:
   FILE     *outFile_;
   uint32_t ssrc_;
   uint32_t seqNo_;
   uint32_t totBytes_;
   uint32_t totPackets_;
   int32_t  timeMs_;
  };
    
};

using webrtc::AudioFrame;
using webrtc::AudioCodingModule;

struct ACMtestSetup {
    std::string codec;
    int sample_rate_hz;
    NW_type nw_type;
    int  bitrate_bps;
    int  packet_size_ms;
    int  plr;
    float mbl;
    bool useFec;
    bool useDtx;
    bool switchPacketsize;
};

static void initACMtestSetup(
    struct ACMtestSetup* setup
)
{
  rtc::LogMessage::SetLogToStderr(false);

  setup->codec = "opus";
  setup->sample_rate_hz = 16000;
  setup->nw_type = NW_type_clean;
  setup->bitrate_bps = 32000;
  setup->packet_size_ms = 20;
  setup->plr = 0;
  setup->mbl = 1.0f;
  setup->useFec = false;
  setup->useDtx = false;
  setup->switchPacketsize = false;
}

struct ACMtestStats {
    float avg_bitrate_bps;
    float max_packet_loss_rate;
    float avg_packet_loss_rate;
    float max_expand_rate;
    float avg_expand_rate;
    float max_buffer_size;
    float avg_buffer_size;
    float cpu_load_encoder;
    float cpu_load_decoder;
};

/**********************************/
/* Main Testing part              */
/**********************************/
static int ACM_unit_test(
  const char* in_file_name,
  const char* rtp_file_name,
  const char* out_file_name,
  const char* file_path,
  struct ACMtestStats* stats,
  struct ACMtestSetup* setup
)
{
  AudioFrame audioframe;
    
  FILE *in_file, *rtp_file, *out_file;
    
  int args;
  int32_t sample_rate_hz = setup->sample_rate_hz;
  
  bool encode = true, decode = true, useFec = setup->useFec, useDtx = setup->useDtx, switchPacketsize = setup->switchPacketsize;
  uint32_t ssrc = 0;
  int num_input_channels = 1, packet_size_ms = setup->packet_size_ms, bit_rate_bps = setup->bitrate_bps;
  int ret, num_frames = 0;
  size_t read_count;
  float ms_tot;
  struct timeval now, startTime, res, tmp, totTime;
    
  timerclear(&totTime);
    
  if(setup->codec.length() == 0){
    printf("No codec specified ! \n");
    return -1;
  }
    
  std::unique_ptr<AudioCodingModule> acm(AudioCodingModule::Create(0));
    
  // Setup Audio Buffers
  int samples_per_channel = sample_rate_hz / 100;
  audioframe.samples_per_channel_ = samples_per_channel;
  audioframe.num_channels_ = num_input_channels;
  audioframe.sample_rate_hz_ = sample_rate_hz;
    
  // Enable Tracing
  //webrtc::Trace::CreateTrace();
  //webrtc::Trace::SetTraceFile("ACM_trace.txt", true );
  //webrtc::Trace::set_level_filter(webrtc::kTraceAll);
    
  if( encode) {
    in_file = fopen(in_file_name,"rb");
    if(in_file == NULL){
      printf("Could not open %s for reading \n", in_file_name);
      return -1;
    }
    
    rtp_file = fopen(rtp_file_name,"wb");
    if( rtp_file == NULL ){
      printf("Could not open %s for writing \n", rtp_file_name);
      fclose( in_file );
      return -1;
    }
      
    // Initialize ACM sending
    bool codec_found = false;
    webrtc::CodecInst my_codec_param;
    int numberOfCodecs = acm->NumberOfCodecs();
    for( int i = 0; i < numberOfCodecs; i++ ){
      ret += acm->Codec( i, &my_codec_param);
      if(strcmp(my_codec_param.plname,setup->codec.c_str()) == 0){
        codec_found = true;
        break;
      }
    }
    if( !codec_found ){
      printf("Could not find codec %s \n", setup->codec.c_str());
      return -1;
    }
    if(strcmp(my_codec_param.plname,"opus") == 0){
      /* For opus set to mono and 32 kbps */
        my_codec_param.channels = 1;
        my_codec_param.rate     = bit_rate_bps;
        my_codec_param.pacsize  = (my_codec_param.plfreq/1000) * packet_size_ms;
    }
      
    acm->RegisterSendCodec( my_codec_param );
      
    acm->SetPacketLossRate( setup->plr );
      
    acm->SetCodecFEC( useFec );
	  
	if(useDtx){
		
      acm->EnableOpusDtx();
	} else {
      acm->DisableOpusDtx();
	}
	  
    webrtc::TransportCallBack tCB( rtp_file, ssrc );
  
    if(acm->RegisterTransportCallback((webrtc::AudioPacketizationCallback*) &tCB) < 0){
      printf("Register Transport Callback failed \n");
    }
      
    /********************************************/
    /* Test Encoder Side                        */
    /********************************************/
    size_t size = samples_per_channel * num_input_channels;
    num_frames = 0;
    while(1){
      read_count = fread(audioframe.data_,
                         sizeof(int16_t),
                         size,
                         in_file);
    
      if(read_count < size){
        break;
      }
        
      audioframe.timestamp_ = num_frames * audioframe.samples_per_channel_;
		
      tCB.increment_time(10);
		
      gettimeofday(&startTime, NULL);
      ret = acm->Add10MsData(audioframe);
      if( ret < 0 ){
        printf("acm->Add10msData returned %d \n", ret);
      }
		
      gettimeofday(&now, NULL);
      timersub(&now, &startTime, &res);
      memcpy( &tmp, &totTime, sizeof(struct timeval));
      timeradd(&res, &tmp, &totTime);
      
      num_frames++;

      if(switchPacketsize){
	    /* Every 20 sec switch packet size 20 <-> 60 ms */
	    if( num_frames % 2000 == 0 ){
          rtc::Optional<webrtc::CodecInst> current_codec;
          current_codec = acm->SendCodec();
          if((my_codec_param.plfreq/1000) * 20 == my_codec_param.pacsize){
            // switch to 60 ms
            my_codec_param.pacsize = (my_codec_param.plfreq/1000) * 60;
          } else {
            // switch to 20 ms
            my_codec_param.pacsize = (my_codec_param.plfreq/1000) * 20;
          }
          acm->RegisterSendCodec( my_codec_param );
	    }
      }
    }
   
    ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    stats->cpu_load_encoder = 100*ms_tot/(float)(10*num_frames);
    stats->avg_bitrate_bps = (float)(8*tCB.get_total_bytes())/(10*num_frames);
      
    fclose(in_file);
    fclose(rtp_file);
  }
    
  if( decode ) {
    rtp_file = fopen(rtp_file_name,"rb");
    if( rtp_file == NULL ){
      printf("Could not open file for reading \n");
      return -1;
    }
    
    out_file = fopen(out_file_name,"wb");
    if(out_file == NULL){
      printf("Could not open %s for writing \n", out_file_name);
      fclose(rtp_file);
      return -1;
    }
          
    // Initialize ACM recieving
    acm->InitializeReceiver();
    webrtc::CodecInst my_codec_param;
    int numberOfCodecs = acm->NumberOfCodecs();
    bool codec_found = false;
    for( int i = 0; i < numberOfCodecs; i++ ){
      ret = acm->Codec( i, &my_codec_param);
      if(strcmp(my_codec_param.plname, setup->codec.c_str()) == 0){
        codec_found = true;
        break;
      }
    }
    ret = acm->RegisterReceiveCodec(my_codec_param);
    if( ret < 0 ){
      return -1;
    }
      
    NwSimulator *nws = new NwSimulator();
    nws->Init(0, setup->plr, setup->mbl, setup->nw_type, file_path);
      
    /********************************************/
    /* Test Decoder Side                        */
    /********************************************/
    num_frames = 0;
    timerclear(&totTime);
    int bytesIn;
    int32_t sndTimeMs;
    uint8_t RTPpacketBuf[500];
    webrtc::WebRtcRTPHeader rtp_info;
    int maxBufferSize = 0;
    int maxPacketLossRate = 0;
    int maxExpandRate = 0;
    float avgBufferSize = 0.0f;
    float avgPacketLossRate = 0.0f;
    float avgExpandRate = 0.0f;
    int numStats = 0;
    int32_t timeMs = 0;
    /* Read the first packet snd time */
    fread(&sndTimeMs,sizeof(int32_t),1,rtp_file);
    do{
      if( timeMs == sndTimeMs ){
        fread(&bytesIn, sizeof(int), 1, rtp_file);
        if(bytesIn < 0){
          break;
        }
        if(bytesIn > 500){
          printf("Packet too large for temp buffer %d bytes \n", bytesIn);
          break;
		}
        fread(RTPpacketBuf, sizeof(unsigned char), bytesIn, rtp_file);
          
        // Put in Network Queue
        ret = nws->Add_Packet( RTPpacketBuf, bytesIn, timeMs);
		  
        // Get the next packet snd time
        fread(&sndTimeMs,sizeof(int32_t),1,rtp_file);
        if(sndTimeMs < 0){
          break;
        }
      }
      
      // Get arrived packets from Network Queue
      bytesIn = nws->Get_Packet( RTPpacketBuf, timeMs);
      while(bytesIn > 0){
        ReadRTPheader( RTPpacketBuf,
                       &rtp_info.header.payloadType,
                       &rtp_info.header.sequenceNumber,
                       &rtp_info.header.timestamp,
                       &rtp_info.header.ssrc);

        rtp_info.header.markerBit = 0;
        rtp_info.type.Audio.isCNG = false;
        rtp_info.type.Audio.channel = 1;
        
        gettimeofday(&startTime, NULL);
        ret = acm->IncomingPacket( RTPpacketBuf + RTP_HEADER_IN_BYTES,
                                   bytesIn - RTP_HEADER_IN_BYTES,
                                   rtp_info );
        gettimeofday(&now, NULL);
        timersub(&now, &startTime, &res);
        memcpy( &tmp, &totTime, sizeof(struct timeval));
        timeradd(&res, &tmp, &totTime);
        if( ret < 0 ){
          printf("acm->IncomingPacket returned %d \n", ret);
        }
        bytesIn = nws->Get_Packet( RTPpacketBuf, timeMs);
      }

      // write a frame every 10 ms
      if( timeMs % 10 == 0 ){
        gettimeofday(&startTime, NULL);
        ret = acm->PlayoutData10Ms(sample_rate_hz, &audioframe);
        gettimeofday(&now, NULL);
        timersub(&now, &startTime, &res);
        memcpy( &tmp, &totTime, sizeof(struct timeval));
        timeradd(&res, &tmp, &totTime);
        if( ret < 0 ){
          printf("acm->PlayoutData10Ms returned %d \n", ret);
        }
        
        if(audioframe.num_channels_ == 2 ){
          int16_t vec[ 48 * 10 ];
          for( int j = 0; j < audioframe.samples_per_channel_; j++){
            vec[ j ] =  audioframe.data_[ 2*j ];
          }
          fwrite( vec,
                  sizeof(int16_t),
                  audioframe.samples_per_channel_,
                  out_file);
        } else {
          fwrite( audioframe.data_,
                  sizeof(int16_t),
                  audioframe.samples_per_channel_ * audioframe.num_channels_,
                  out_file);
        }
        num_frames++;
      }
        
      if( timeMs % 10000 == 0 && timeMs > 0){
          webrtc::NetworkStatistics inCallStats;
          ret = acm->GetNetworkStatistics(&inCallStats);
			
          maxBufferSize = std::max(maxBufferSize, (int)inCallStats.currentBufferSize);
          maxPacketLossRate = std::max(maxPacketLossRate, (int)inCallStats.currentPacketLossRate);
          maxExpandRate = std::max(maxExpandRate, (int)inCallStats.currentExpandRate);
          avgBufferSize += (float)inCallStats.currentBufferSize;
          avgPacketLossRate += 100*(float)inCallStats.currentPacketLossRate/(float)(1 << 14);
          avgExpandRate += 100*(float)inCallStats.currentExpandRate/(float)(1 << 14);
          
          numStats++;
      }
      timeMs++;
    } while(!feof(rtp_file));
    	  
    ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    stats->cpu_load_decoder = 100*ms_tot/(float)(10*num_frames);
    stats->max_buffer_size = (float)maxBufferSize;
    stats->avg_buffer_size = avgBufferSize/(float)numStats;
    stats->max_packet_loss_rate = 100*(float)maxPacketLossRate/(float)(1 << 14);
    stats->avg_packet_loss_rate = avgPacketLossRate/(float)numStats;
    stats->max_expand_rate = 100*(float)maxExpandRate/(float)(1 << 14);
    stats->avg_expand_rate = avgExpandRate/(float)numStats;

    fclose(rtp_file);
    fclose(out_file);
  
    delete nws;
  }
    
  //webrtc::Trace::ReturnTrace();
    
  return 0;
}

TEST(acm, clean_channel_20ms)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    
    int ret = ACM_unit_test(
                        "./test/data/near16.pcm",
                        "./test/data/rtp.dat",
                        "./test/data/out.pcm",
                        "./test/data/",
                        &stats,
                        &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 50.0 );
    EXPECT_LT( stats.avg_buffer_size, 50.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.avg_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.max_expand_rate, 1.0 );
    EXPECT_LT( stats.avg_expand_rate, 0.1 );
}

TEST(acm, clean_channel_60ms)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.packet_size_ms = 60;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 150.0 );
    EXPECT_LT( stats.avg_buffer_size, 90.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.avg_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.max_expand_rate, 1.0 );
    EXPECT_LT( stats.avg_expand_rate, 0.1 );
}

TEST(acm, wifi_channel_20ms)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.nw_type = NW_type_wifi;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 200.0 );
    EXPECT_LT( stats.avg_buffer_size, 120.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.avg_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.max_expand_rate, 2.0 );
    EXPECT_LT( stats.avg_expand_rate, 1.0 );
}

TEST(acm, wifi_channel_change_packet_size)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.nw_type = NW_type_wifi;
    setup.switchPacketsize = true;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 160.0 );
    EXPECT_LT( stats.avg_buffer_size, 110.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.avg_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.max_expand_rate, 2.0 );
    EXPECT_LT( stats.avg_expand_rate, 1.0 );
}
#if 0 // NetEq latency very high with DTX after updating webrtc - why ??
TEST(acm, wifi_channel_20ms_dtx)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.nw_type = NW_type_wifi;
    setup.useDtx = true;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 200.0 );
    EXPECT_LT( stats.avg_buffer_size, 120.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.avg_packet_loss_rate, 0.01 );
    EXPECT_LT( stats.max_expand_rate, 2.0 );
    EXPECT_LT( stats.avg_expand_rate, 1.0 );
}
#endif
TEST(acm, wifi_channel_20ms_20pct_loss)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.nw_type = NW_type_wifi;
    setup.plr = 20;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 5.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 200.0 );
    EXPECT_LT( stats.avg_buffer_size, 120.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 30.0 );
    EXPECT_LT( stats.avg_packet_loss_rate, 25.0 );
    EXPECT_LT( stats.max_expand_rate, 30.0 );
    EXPECT_LT( stats.avg_expand_rate, 25.0 );
}

#if 0
TEST(acm, wifi_channel_20ms_20pct_loss_fec)
{
    float cpu_load;
    ACMtestSetup setup;
    ACMtestStats stats;
    
    initACMtestSetup(&setup);
    setup.bitrate_bps = 32000;
    setup.sample_rate_hz = 16000;
    setup.nw_type = NW_type_wifi;
    setup.plr = 20;
    setup.useFec = true;
    
    int ret = ACM_unit_test(
                            "./test/data/near16.pcm",
                            "./test/data/rtp.dat",
                            "./test/data/out.pcm",
                            "./test/data/",
                            &stats,
                            &setup);
    
    ASSERT_EQ(0, ret);
    EXPECT_LT( stats.avg_bitrate_bps, 35000.0 );
    COMPLEXITY_CHECK( stats.cpu_load_encoder, 8.0 );
    COMPLEXITY_CHECK( stats.cpu_load_decoder, 2.0 );
    EXPECT_LT( stats.max_buffer_size, 150.0 );
    EXPECT_LT( stats.avg_buffer_size, 90.0 );
    EXPECT_LT( stats.max_packet_loss_rate, 10.0 );
    EXPECT_LT( stats.avg_packet_loss_rate, 5.0 );
    EXPECT_LT( stats.max_expand_rate, 10.0 );
    EXPECT_LT( stats.avg_expand_rate, 6.0 );
}
#endif
