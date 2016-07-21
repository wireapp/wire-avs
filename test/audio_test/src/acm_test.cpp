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

/**********************************/
/* Main Testing part              */
/**********************************/
#if TARGET_OS_IPHONE
int acm_test(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
  AudioFrame audioframe;
  
  std::string codec, in_file_name, out_file_name, rtp_file_name, jb_log_file_name, file_path;
    
  FILE *in_file, *rtp_file, *out_file, *nw_file, *jb_log_file;
    
  int args;
  int32_t sample_rate_hz = 16000;
  
  NW_type nw_type = NW_type_clean;
  bool encode = true, decode = true, useFec = false, useDtx = false, changePackets = false;
  uint32_t ssrc = 0;
  int num_input_channels = 1, packet_loss_rate = 0, packet_size_ms = 20, bit_rate_bps = 32000;
  int ret, num_frames = 0;
  size_t read_count;
  float ms_tot, mean_burst_length = 1.0f;
  struct timeval now, startTime, res, tmp, totTime;
    
  timerclear(&totTime);

  // Handle Command Line inputs
  args = 0;
  nw_file = NULL;
  in_file = NULL;
  out_file = NULL;
  rtp_file = NULL;
  while(args < argc){
    if (strcmp(argv[args], "-in")==0){
      args++;
        in_file_name.insert(0,argv[args]);
    } else if (strcmp(argv[args], "-out")==0){
        args++;
        out_file_name.insert(0,argv[args]);
    } else if (strcmp(argv[args], "-rtp")==0){
        args++;
        rtp_file_name.insert(0,argv[args]);
    }else if (strcmp(argv[args], "-jblog")==0){
        args++;
        jb_log_file_name.insert(0,argv[args]);
    } else if (strcmp(argv[args], "-nw_type")==0){
        args++;
        if(strcmp(argv[args],"wifi") == 0){
            nw_type = NW_type_wifi;
        } else if(strcmp(argv[args],"sawtooth") == 0){
           nw_type = NW_type_sawtooth;
        } else {
		   nw_type = NW_type_clean;
        }
    } else if (strcmp(argv[args], "-fs")==0){
      args++;
      sample_rate_hz = atol(argv[args]);
	}else if (strcmp(argv[args], "-ps")==0){
		args++;
		packet_size_ms = atol(argv[args]);
	}else if (strcmp(argv[args], "-br")==0){
		args++;
		bit_rate_bps = atol(argv[args]);
	}
	else if (strcmp(argv[args], "-lr")==0){
      args++;
      packet_loss_rate = atol(argv[args]);
    } else if (strcmp(argv[args], "-mbl")==0){
      args++;
      mean_burst_length = atol(argv[args]);
    } else if (strcmp(argv[args], "-skip_enc")==0){
      encode = false;
    } else if (strcmp(argv[args], "-skip_dec")==0){
      decode = false;
    } else if (strcmp(argv[args], "-fec")==0){
      useFec = true;
	}  else if (strcmp(argv[args], "-dtx")==0){
		useDtx = true;
    }  else if (strcmp(argv[args], "-change_packets")==0){
        changePackets = true;
    } else if (strcmp(argv[args], "-ssrc")==0){
      args++;
      ssrc = atol(argv[args]);
    }else if (strcmp(argv[args], "-codec")==0){
      args++;
      codec.insert(0,argv[args]);
    }
    else if (strcmp(argv[args], "-file_path")==0){
        args++;
        file_path.insert(0,argv[args]);
    }
    args++;
  }

  if(file_path.length()){
      std::string slash = "/";
      if(file_path[file_path.length()-1] != slash[slash.length()-1]){
          file_path = file_path + slash;
      }
  }
  if(in_file_name.length()){
     in_file_name = file_path + in_file_name;
  }
  if(rtp_file_name.length()){
      rtp_file_name = file_path + rtp_file_name;
  }
  if(out_file_name.length()){
      out_file_name = file_path + out_file_name;
  }
  if(jb_log_file_name.length()){
      jb_log_file_name = file_path + jb_log_file_name;
  }
    
  if(codec.length() == 0){
    printf("No codec specified ! \n");
    return -1;
  }
    
  std::unique_ptr<AudioCodingModule> acm(AudioCodingModule::Create(0));
    
  printf("\n--- Running ACM test:  --- \n");
  printf("Fs                          : %d Hz \n", sample_rate_hz);
  if( encode ) {
    printf("Input File                  : %s \n", in_file_name.c_str());
  }
  printf("Rtp File                    : %s \n", rtp_file_name.c_str());
  if( decode ) {
    printf("Output File                 : %s \n", out_file_name.c_str());
    printf("NW type                     : %s \n", NwSimulator::NWtype2Str(nw_type));
    printf("Additional packet loss      : %d %% \n", packet_loss_rate);
  }
    
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
    in_file = fopen(in_file_name.c_str(),"rb");
    if(in_file == NULL){
      printf("Could not open %s for reading \n", in_file_name.c_str());
      return -1;
    }
    
    rtp_file = fopen(rtp_file_name.c_str(),"wb");
    if( rtp_file == NULL ){
      printf("Could not open %s for writing \n", rtp_file_name.c_str());
      fclose( in_file );
      return -1;
    }
      
    // Initialize ACM sending
    bool codec_found = false;
    webrtc::CodecInst my_codec_param;
    int numberOfCodecs = acm->NumberOfCodecs();
    for( int i = 0; i < numberOfCodecs; i++ ){
      ret += acm->Codec( i, &my_codec_param);
      //printf("plname = %s \n", my_codec_param.plname);
      if(strcmp(my_codec_param.plname,codec.c_str()) == 0){
        codec_found = true;
        break;
      }
    }
    if( !codec_found ){
      printf("Could not find codec %s \n", codec.c_str());
      return -1;
    } else {
      printf("Codec                       : %s \n", codec.c_str());
    }
     
    if(strcmp(my_codec_param.plname,"opus") == 0){
      /* For opus set to mono and 32 kbps */
        my_codec_param.channels = 1;
        my_codec_param.rate     = bit_rate_bps;
        my_codec_param.pacsize  = (my_codec_param.plfreq/1000) * packet_size_ms;
    }
      
    acm->RegisterSendCodec( my_codec_param );
      
    acm->SetPacketLossRate( packet_loss_rate );
      
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
      if(changePackets){
	    /* Every 20 sec switch packet size 20 <-> 60 ms */
	    if( num_frames % 2000 == 0 ){
          rtc::Optional<webrtc::CodecInst> current_codec;
          current_codec = acm->SendCodec();
		    if((current_codec->plfreq/1000) * 20 == current_codec->pacsize){
			  // switch to 60 ms
			  current_codec->pacsize = (current_codec->plfreq/1000) * 60;
			  printf("Switch to 60 ms packets !! \n");
		    } else {
			  // switch to 20 ms
		      current_codec->pacsize = (current_codec->plfreq/1000) * 20;
              printf("Switch to 20 ms packets !! \n");
		  }
		  acm->RegisterSendCodec( *current_codec );
	    }
      }
    }
   
    ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    
    rtc::Optional<webrtc::CodecInst> current_codec;
    current_codec = acm->SendCodec();
    
    printf("ACM encoding test finished processed %d frames \n", num_frames);
    printf("%d ms audio took %f ms %.1f %% of real time \n", 10*num_frames, ms_tot, 100*ms_tot/(float)(10*num_frames));
    printf("ACM API Fs                  = %d Hz \n", sample_rate_hz);
    printf("codec API Fs                = %d Hz \n", current_codec->plfreq);
    printf("codec channels              = %zu \n", current_codec->channels);
    printf("Average bitrate             = %.1f kbps \n\n", (float)(8*tCB.  get_total_bytes())/(10*num_frames));
  
    fclose(in_file);
    fclose(rtp_file);
  }
    
  if( decode ) {
    rtp_file = fopen(rtp_file_name.c_str(),"rb");
    if( rtp_file == NULL ){
      printf("Could not open file for reading \n");
      return -1;
    }
    
    out_file = fopen(out_file_name.c_str(),"wb");
    if(out_file == NULL){
      printf("Could not open %s for writing \n", argv[args]);
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
      if(strcmp(my_codec_param.plname,codec.c_str()) == 0){
        codec_found = true;
        break;
      }
    }
    ret = acm->RegisterReceiveCodec(my_codec_param);
    if( ret < 0 ){
      printf("acm->RegisterReceiveCodec returned %d \n", ret);
    }
      
    if(jb_log_file_name.length() > 0){
      jb_log_file = fopen(jb_log_file_name.c_str(),"wt");
    } else {
      jb_log_file = NULL;
    }
	  
    NwSimulator *nws = new NwSimulator();
    nws->Init(0, packet_loss_rate, mean_burst_length, nw_type, file_path);
      
    /********************************************/
    /* Test Decoder Side                        */
    /********************************************/
    num_frames = 0;
    timerclear(&totTime);
    int bytesIn;
    int32_t sndTimeMs;
    uint8_t RTPpacketBuf[500];
    webrtc::WebRtcRTPHeader rtp_info;
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
        
        if(jb_log_file != NULL){
          webrtc::NetworkStatistics inCallStats;
          ret = acm->GetNetworkStatistics(&inCallStats);
			
          /* Write Statics to file */
          fprintf( jb_log_file,"%d ", timeMs);
          fprintf( jb_log_file,"%d %d %d ", 0, 0, 0);
          fprintf( jb_log_file,"%d %d %d %d ", inCallStats.currentBufferSize, inCallStats.preferredBufferSize,
                                             inCallStats.jitterPeaksFound, inCallStats.currentPacketLossRate );
          fprintf( jb_log_file,"%d %d %d %d ", inCallStats.currentDiscardRate, inCallStats.currentExpandRate,
                                             inCallStats.currentPreemptiveRate, inCallStats.currentAccelerateRate );
          fprintf( jb_log_file,"%d %zu \n", inCallStats.clockDriftPPM, inCallStats.addedSamples );
        }
      }
      timeMs++;
    } while(!feof(rtp_file));
    	  
    ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    printf("ACM decoding test finished processed %d frames \n", num_frames);
    printf("%d ms audio took %f ms %.1f %% of real time \n", 10*num_frames, ms_tot, 100*ms_tot/(float)(10*num_frames));
    printf("ACM decoding test lost %d packets of %d \n", nws->GetLostPacketCount(), nws->GetPacketCount());
    printf("ACM Playout channels        = %zu \n", audioframe.num_channels_);
#ifdef PRINT_NETEQ_STATS
#if 0
    webrtc::ACMNetworkStatistics inCallStats;
    
    ret = acm->NetworkStatistics(&inCallStats);
#else
    webrtc::NetworkStatistics inCallStats;
      
    ret = acm->GetNetworkStatistics(&inCallStats);
#endif
    printf("NetEQ currentBufferSize     = %d ms \n", inCallStats.currentBufferSize);
    printf("NetEQ preferredBufferSize   = %d ms \n", inCallStats.preferredBufferSize);
    printf("NetEQ currentPacketLossRate = %.2f %% \n", 100*(float)inCallStats.currentPacketLossRate/(float)(1 << 14));
    printf("NetEQ currentDiscardRate    = %.2f %% \n", 100*(float)inCallStats.currentDiscardRate/(float)(1 << 14));
    printf("NetEQ currentPreemptiveRate = %.2f %% \n", 100*(float)inCallStats.currentPreemptiveRate/(float)(1 << 14));
    printf("NetEQ currentAccelerateRate = %.2f %% \n", 100*(float)inCallStats.currentAccelerateRate/(float)(1 << 14));
    printf("NetEQ clockDriftPPM         = %d \n", inCallStats.clockDriftPPM);
#endif
    printf("-------------------\n\n");
      
    if(nw_file) {
      fclose(nw_file);
    }
    if(jb_log_file){
      fclose(jb_log_file);
    }
    fclose(rtp_file);
    fclose(out_file);
  
    delete nws;
  }
  //webrtc::Trace::ReturnTrace();
    
  return 0;
}
