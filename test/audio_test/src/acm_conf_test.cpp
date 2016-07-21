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

#include <sys/time.h>

#include "webrtc/modules/audio_coding/include/audio_coding_module.h"
//#include "webrtc/system_wrappers/interface/scoped_ptr.h"
#include "webrtc/system_wrappers/include/trace.h"

#include "NwSimulator.h"

#if TARGET_OS_IPHONE
#include "AudioTest.h"
#endif

#define PRINT_NETEQ_STATS

/**********************************/
/* RTP help functions             */
/**********************************/

#define RTP_HEADER_IN_BYTES 12
#define MAX_PACKET_SIZE_BYTES 1000

int ReadRTPheader( const uint8_t* rtpHeader,
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

int MakeRTPheader( uint8_t* rtpHeader,
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
    TransportCallBack( uint32_t ssrc){
      seqNo_ = 0;
      ssrc_ = ssrc;
      totBytes_ = 0;
      timestamps_per_packet_ = 0;
      prev_timestamp_ = 0;
      packetLenBytes_ = 0;
      timestamp_offset = rand();
    }
        
    int32_t SendData(
                      FrameType     frame_type,
                      uint8_t       payload_type,
                      uint32_t      timestamp,
                      const uint8_t* payload_data,
                      size_t      payload_len_bytes,
                      const RTPFragmentationHeader* fragmentation){
        
      int headerLenBytes;
        
      totBytes_ += payload_len_bytes;
        
      timestamp = timestamp + timestamp_offset;
        
      headerLenBytes = MakeRTPheader(packet_,
                                     payload_type,
                                     seqNo_++,
                                     timestamp,
                                     ssrc_);
        
      if( payload_len_bytes < MAX_PACKET_SIZE_BYTES ){
          memcpy(&packet_[headerLenBytes], payload_data, payload_len_bytes * sizeof(uint8_t));
          packetLenBytes_ = (uint32_t)payload_len_bytes + (uint32_t)headerLenBytes;
      } else {
          packetLenBytes_ = 0;
      }
        
      if((prev_timestamp_ != 0) && (timestamps_per_packet_ == 0)){
          printf("1. timestamp = %d \n", timestamp);
          timestamps_per_packet_ = timestamp - prev_timestamp_;
      }
      prev_timestamp_ = timestamp;
        
      return(0);
    }
    
    int32_t get_total_bytes(){
      return(totBytes_);
    }

    int32_t get_timestamps_per_packet(){
      return(timestamps_per_packet_);
    }
      
    uint32_t get_packet( uint8_t** packet){
      uint32_t tmp = packetLenBytes_;
      packetLenBytes_ = 0;
      if(tmp > 0){
          *packet = packet_;
      }else{
          packet = NULL;
      }
      return(tmp);
    }
            
  private:
   uint32_t ssrc_;
   uint32_t seqNo_;
   uint32_t totBytes_;
   uint16_t timestamps_per_packet_;
   uint32_t prev_timestamp_;
   uint32_t timestamp_offset;
   uint8_t  packet_[RTP_HEADER_IN_BYTES + MAX_PACKET_SIZE_BYTES];
   uint32_t packetLenBytes_;
  };
    
};

using webrtc::AudioFrame;
using webrtc::AudioCodingModule;
//using webrtc::scoped_ptr;

/**********************************/
/* Main Testing part              */
/**********************************/
int main(int argc, char *argv[])
{
  AudioFrame audioframe;
    
  std::string in_file_name;
    
  FILE *in_file, *out_file, *out_file2;
    
  int32_t sample_rate_hz = 48000;
    
  bool useFec = false;
  uint32_t ssrc = 0;
  int num_input_channels = 1, packet_loss_rate = 0;
  int ret, num_frames = 0;
  size_t read_count;
  float ms_tot;
  struct timeval now, startTime, res, tmp, totTime;
    
  timerclear(&totTime);

  std::unique_ptr<AudioCodingModule> acm1(AudioCodingModule::Create(0));
  std::unique_ptr<AudioCodingModule> acm2(AudioCodingModule::Create(2));
    
  printf("\n--- Running ACM test: --- \n");
  printf("Fs                          : %d Hz \n", sample_rate_hz);
  printf("Input File                  : %s \n", in_file_name.c_str());
  printf("Additional packet loss      : %d %% \n", packet_loss_rate);
    
  // Setup Audio Buffers
  int samples_per_channel = sample_rate_hz / 100;
  audioframe.samples_per_channel_ = samples_per_channel;
  audioframe.num_channels_ = num_input_channels;
  audioframe.sample_rate_hz_ = sample_rate_hz;
    
  // Enable Tracing
  //webrtc::Trace::CreateTrace();
  //webrtc::Trace::SetTraceFile("ACM_trace.txt", true );
  //webrtc::Trace::set_level_filter(webrtc::kTraceAll);
    
  in_file_name.insert(0,"../../../../../../test/audio_test/files/far48.pcm");
  in_file = fopen(in_file_name.c_str(),"rb");
  if(in_file == NULL){
    printf("Could not open %s for reading \n", in_file_name.c_str());
    return -1;
  }
    
  out_file = fopen("out.pcm","wb");
  out_file2 = fopen("out2.pcm","wb");
      
  // Initialize ACM sending
  bool codec_found = false;
  webrtc::CodecInst codec_param;
  int numberOfCodecs = acm1->NumberOfCodecs();
  for( int i = 0; i < numberOfCodecs; i++ ){
    ret += acm1->Codec( i, &codec_param);
    if(strcmp(codec_param.plname,"opus") == 0){
      codec_found = true;
      break;
    }
  }
  if( !codec_found ){
    printf("Could not find codec %s \n", "opus");
    return -1;
  }
     
  /* For opus set to mono and 32 kbps */
  codec_param.channels = 1;
  codec_param.rate     = 40000;
    
  acm1->RegisterSendCodec( codec_param );
  acm2->RegisterSendCodec( codec_param );
      
  acm1->SetPacketLossRate( packet_loss_rate );
  acm2->SetPacketLossRate( packet_loss_rate );
    
  acm1->SetCodecFEC( useFec );
  acm2->SetCodecFEC( useFec );
    
  webrtc::TransportCallBack tCB( ssrc );
  webrtc::TransportCallBack tCB2( ssrc );
      
  if(acm1->RegisterTransportCallback((webrtc::AudioPacketizationCallback*) &tCB) < 0){
    printf("Register Transport Callback failed \n");
  }

  if(acm2->RegisterTransportCallback((webrtc::AudioPacketizationCallback*) &tCB2) < 0){
    printf("Register Transport Callback failed \n");
  }
      
  // Initialize ACM recieving
  acm1->InitializeReceiver();
  ret = acm1->RegisterReceiveCodec(codec_param);
  if( ret < 0 ){
    printf("acm1->RegisterReceiveCodec returned %d \n", ret);
  }

  // Initialize ACM2 recieving
  acm2->InitializeReceiver();
  ret = acm2->RegisterReceiveCodec(codec_param);
  if( ret < 0 ){
      printf("acm2->RegisterReceiveCodec returned %d \n", ret);
  }
  
  FILE *fid = fopen("ts_log.txt","wt");
    
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
      
    audioframe.timestamp_ = num_frames * audioframe.samples_per_channel_ ;

    bool isEmpty1, isEmpty2;
    int id1, re_id1, id2, re_id2;
      
    acm1->GetStatus( isEmpty1, id1, re_id1);
    //printf("acm1 status: %d %d %d \n", isEmpty1, id1, re_id1);
      
    /* ACM1 encoding */
    gettimeofday(&startTime, NULL);
    ret = acm1->Add10MsData(audioframe);
    if( ret < 0 ){
      printf("acm1->Add10msData returned %d \n", ret);
    }
    ret = acm1->Process();
    if( ret < 0 ){
      printf("acm1->Process returned %d \n", ret);
    }
    gettimeofday(&now, NULL);
    timersub(&now, &startTime, &res);
    memcpy( &tmp, &totTime, sizeof(struct timeval));
    timeradd(&res, &tmp, &totTime);
      
    if(num_frames > 501){
      /* ACM2 encoding */
      gettimeofday(&startTime, NULL);
        
      /* Check wether we can reuse the output from acm1 */
      webrtc::CodecInst codec1, codec2;
        
      acm1->SendCodec(&codec1);
      acm2->SendCodec(&codec2);
        
      acm2->GetStatus( isEmpty2, id2, re_id2);
      printf("acm2 status: %d %d %d \n", isEmpty2, id2, re_id2);
      //printf("acm1->InternalBufferEmpty() = %d \n", acm1->InternalBufferEmpty());
      //printf("acm2->InternalBufferEmpty() = %d \n", acm2->InternalBufferEmpty());
        
      bool reuse_encoding = false;
      if( strcmp(codec2.plname,codec1.plname) == 0 &&
        codec2.plfreq   == codec1.plfreq         &&
        codec2.pacsize  == codec1.pacsize        &&
        codec2.channels == codec1.channels       &&
        codec2.rate     == codec1.rate ) {
          
        reuse_encoding = true;
      }
        
      // 1: handle start reusing
      if( reuse_encoding && re_id2 == -1){
          printf("Start reusing ! \n");
          if(!(isEmpty1 && isEmpty2)){
              printf("Dont start reusing isEmpty1 = %d isEmpty2 = %d \n", isEmpty1, isEmpty2);
              reuse_encoding = true;
          }
      }
       
      if( !reuse_encoding && re_id2 != -1){
          printf("Stop reusing codec output !! \n");
          if(!(isEmpty1 && isEmpty2)){
              reuse_encoding = false;
          }
      }
        
      if(reuse_encoding){
        uint8_t *stream;
        int16_t length_bytes;
        uint32_t rtp_timestamp;
        int      encoding_type;
        int      status;
        void*    enc_state;
        int      id;
          
          
        // Get the encoder output for the channel with the same codec setting
        acm1->GetEncoderOutput(&stream,
                              &length_bytes,
                              &rtp_timestamp,
                              &encoding_type,
                              &status,
                              &enc_state);
        
        acm2->ProcessEncoderOutput(stream,
                                  length_bytes,
                                  rtp_timestamp,
                                  encoding_type,
                                  status,
                                  enc_state);
        
      } else {
        ret = acm2->Add10MsData(audioframe);
        if( ret < 0 ){
          printf("acm2->Add10msData returned %d \n", ret);
        }
          
        ret = acm2->Process();
        if( ret < 0 ){
          printf("acm2->Process returned %d \n", ret);
        }
      }
      gettimeofday(&now, NULL);
      timersub(&now, &startTime, &res);
      memcpy( &tmp, &totTime, sizeof(struct timeval));
      timeradd(&res, &tmp, &totTime);
    }
    uint8_t *RTPpacket;
    /* ACM1 decoding */
    int bytesIn = tCB.get_packet(&RTPpacket);
    if(bytesIn > 0){
      /* Put Packet in NetEQ */
      webrtc::WebRtcRTPHeader rtp_info;
      ReadRTPheader( RTPpacket,
                     &rtp_info.header.payloadType,
                     &rtp_info.header.sequenceNumber,
                     &rtp_info.header.timestamp,
                     &rtp_info.header.ssrc);
          
      rtp_info.header.markerBit = 0;
      rtp_info.type.Audio.isCNG = false;
      rtp_info.type.Audio.channel = 1;
          
      gettimeofday(&startTime, NULL);
      ret = acm1->IncomingPacket( RTPpacket + RTP_HEADER_IN_BYTES,
                                 bytesIn - RTP_HEADER_IN_BYTES,
                                 rtp_info );
      gettimeofday(&now, NULL);
      timersub(&now, &startTime, &res);
      memcpy( &tmp, &totTime, sizeof(struct timeval));
      timeradd(&res, &tmp, &totTime);
      if( ret < 0 ){
        printf("acm1->IncomingPacket returned %d \n", ret);
      }
    }
        
    // Get a frame from NetEQ every 10 ms
    gettimeofday(&startTime, NULL);
    ret = acm1->PlayoutData10Ms(sample_rate_hz, &audioframe);
    gettimeofday(&now, NULL);
    timersub(&now, &startTime, &res);
    memcpy( &tmp, &totTime, sizeof(struct timeval));
    timeradd(&res, &tmp, &totTime);
    if( ret < 0 ){
      printf("acm1->PlayoutData10Ms returned %d \n", ret);
    }
        
    if(audioframe.num_channels_ == 2 ){
      printf("Error Stereo Output not expected !!! \n");
    } else {
      fwrite( audioframe.data_,
              sizeof(int16_t),
              audioframe.samples_per_channel_ * audioframe.num_channels_,
              out_file);
    }
      
    /* ACM2 decoding */
    bytesIn = tCB2.get_packet(&RTPpacket);
    if(bytesIn > 0){
        /* Put Packet in NetEQ */
        webrtc::WebRtcRTPHeader rtp_info;
        ReadRTPheader( RTPpacket,
                       &rtp_info.header.payloadType,
                       &rtp_info.header.sequenceNumber,
                       &rtp_info.header.timestamp,
                       &rtp_info.header.ssrc);
          
        rtp_info.header.markerBit = 0;
        rtp_info.type.Audio.isCNG = false;
        rtp_info.type.Audio.channel = 1;
        
        fprintf(fid,"%d %d \n", rtp_info.header.sequenceNumber, rtp_info.header.timestamp);
        
        gettimeofday(&startTime, NULL);
        ret = acm2->IncomingPacket( RTPpacket + RTP_HEADER_IN_BYTES,
                                  bytesIn - RTP_HEADER_IN_BYTES,
                                  rtp_info );
        gettimeofday(&now, NULL);
        timersub(&now, &startTime, &res);
        memcpy( &tmp, &totTime, sizeof(struct timeval));
        timeradd(&res, &tmp, &totTime);
        if( ret < 0 ){
            printf("acm2->IncomingPacket returned %d \n", ret);
        }
    }
      
    // Get a frame from NetEQ every 10 ms
    gettimeofday(&startTime, NULL);
    ret = acm2->PlayoutData10Ms(sample_rate_hz, &audioframe);
    gettimeofday(&now, NULL);
    timersub(&now, &startTime, &res);
    memcpy( &tmp, &totTime, sizeof(struct timeval));
    timeradd(&res, &tmp, &totTime);
    if( ret < 0 ){
        printf("acm2->PlayoutData10Ms returned %d \n", ret);
    }
      
    if(audioframe.num_channels_ == 2 ){
        printf("Error Stereo Output not expected !!! \n");
    } else {
        fwrite( audioframe.data_,
                sizeof(int16_t),
                audioframe.samples_per_channel_ * audioframe.num_channels_,
                out_file2);
    }
      
    num_frames++;

#if 1
    if(num_frames == 1501){
       codec_param.rate = 10000;
          
       acm2->RegisterSendCodec( codec_param );        
    }

    if(num_frames == 2900){
       codec_param.rate = 40000;
          
       acm2->RegisterSendCodec( codec_param );
    }

    if(num_frames == 3500){
      codec_param.rate = 10000;
          
      acm2->RegisterSendCodec( codec_param );
    }
      
      if(num_frames == 3510){
        break;
    }
#endif
  }
   
  ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    
  webrtc::CodecInst current_codec;
  acm1->SendCodec(&current_codec);
    
  printf("ACM encoding test finished processed %d frames \n", num_frames);
  printf("%d ms audio took %f ms %.1f %% of real time \n", 10*num_frames, ms_tot, 100*ms_tot/(float)(10*num_frames));
  printf("ACM API Fs                  = %d Hz \n", sample_rate_hz);
  printf("codec API Fs                = %d Hz \n", current_codec.plfreq);
  printf("codec channels              = %d \n", current_codec.channels);
  printf("Average bitrate ACM1            = %.1f kbps \n\n", (float)(8*tCB.  get_total_bytes())/(10*num_frames));
  printf("Average bitrate ACM2            = %.1f kbps \n\n", (float)(8*tCB2.  get_total_bytes())/(10*num_frames));
    
  fclose(in_file);
  fclose(out_file);
  fclose(out_file2);
    
  fclose(fid);
}
    