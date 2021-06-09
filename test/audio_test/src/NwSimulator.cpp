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
#include "NwSimulator.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

NwSimulator::NwSimulator()
{
    memset(packets_,0, sizeof(packets_));
    read_idx_ = 0;
    write_idx_ = 0;
    packets_in_queue_ = 0;
    xtra_loss_rate_ = 0;
    avg_burst_length_ = 1.0f;
    packet_size_ms_ = 0;
    lost_packets_ = 0;
    num_packets_ = 0;
    start_time_offset_ = -1;
    network_ = NW_type_clean;
    jitter_file_ = NULL;
    log_file_ = NULL;
    Jitter_from_file_ = 0;
    time_from_file_ = -1;;
}

NwSimulator::~NwSimulator()
{
#ifdef NWQ_DBG
  fclose(log_file);
#endif
}

int NwSimulator::Setup_Jitter_File(
  bool add_offset
)
{
    int ret = 0;
    if(jitter_file_){
        fclose(jitter_file_);
    }
    if(network_ == NW_type_clean){
        jitter_file_ = NULL;
    } else if(network_ == NW_type_wifi){
        std::string tmp = file_path_ + "Jitter_wifi.dat";
        jitter_file_ = fopen(tmp.c_str(),"rb");
    } else if(network_ == NW_type_2G){
        std::string tmp = file_path_ + "Jitter_2G.dat";
        jitter_file_ = fopen(tmp.c_str(),"rb");
    } else if(network_ == NW_type_sawtooth){
        std::string tmp = file_path_ + "Jitter_sawtooth.dat";
        jitter_file_ = fopen(tmp.c_str(),"rb");
    } else {
        jitter_file_ = NULL;
        ret = -1;
    }
    
    return ret;
}

int NwSimulator::Init(
  int packet_size_ms,
  int xtra_loss_rate,
  float avg_burst_length,
  NW_type network,
  std::string file_path
)
{
  int i, ret;
  
  read_idx_ = 0;
  write_idx_ = 0;
  packets_in_queue_ = 0;
  xtra_loss_rate_ = xtra_loss_rate;
  xtra_prev_lost_ = false;
  if(avg_burst_length > 1.0f){
    avg_burst_length_ = avg_burst_length;
  }else {
    avg_burst_length = 1.0f;
  }
  packet_size_ms_ = packet_size_ms;
  network_ = network;
    
  file_path_ = file_path;//.insert(0,"John bon jovi");//file_path;
    
  ret = Setup_Jitter_File( true);
  if(ret < 0){
      printf("No Jitter file found for chosen nw and packet size");
  }
    
  for( i = 0; i < NUM_PACKETS; i++ ){
    packets_[i].data_length = -1;
  }
#ifdef NWQ_DBG
  if( log_file_ == NULL ) {
    log_file_ = fopen("NetworkQueueLog.txt","wt");
  }
  fprintf(log_file_,"NetworkQueueLog started \n");
#endif
  return(0);
}

int NwSimulator::Add_Packet( /* returns -1 if Queue is full otherwise 0 */
  unsigned char* packet,     /* (I) RTP packet */
  int            numBytes,   /* (I) length of RTP packet */
  int            sndTime_ms  /* (I) Time when send in ms */
)
{
  return Add_Packet_Internal(packet, numBytes, sndTime_ms, false);
}

int NwSimulator::Add_Packet_Internal( /* returns -1 if Queue is full otherwise 0 */
  unsigned char* packet,     /* (I) RTP packet */
  int            numBytes,   /* (I) length of RTP packet */
  int            sndTime_ms,  /* (I) Time when send in ms */
  bool           is_rtcp
)
{
  int prev_write_idx, src_idx, dst_idx, num_packets_to_move, i, rcvTime_ms;
  int32_t Jitter, jit;

  if(start_time_offset_ == -1){
    start_time_offset_ = sndTime_ms;
  }
	
  if( jitter_file_ ){
    bool jitter_found = false;
    Jitter = 0;
    if( (time_from_file_ + start_time_offset_ + 10) > sndTime_ms ){
        jitter_found = true;
        Jitter = Jitter_from_file_;
    }
    while(!jitter_found){
      size_t read = fread(&time_from_file_, sizeof(int32_t), 1, jitter_file_);
      read = fread(&jit, sizeof(int32_t), 1, jitter_file_);
      Jitter_from_file_ = std::max(0,jit);
      if(read <= 0){
        Setup_Jitter_File(false);
        start_time_offset_ = sndTime_ms;
      }
      if( (time_from_file_ + start_time_offset_ + 10) > sndTime_ms ){
        jitter_found = true;
        Jitter = Jitter_from_file_;
      }
    }
  } else {
    Jitter = 0;
  }
	
  num_packets_++;
	
  float ploss = xtra_loss_rate_ / 100.0f;
  if( ( !xtra_prev_lost_  && ((float)rand()/RAND_MAX) < ploss / (1.0f - ploss) / avg_burst_length_ ) ||
      (  xtra_prev_lost_  && ((float)rand()/RAND_MAX) < 1.0f - 1.0f / avg_burst_length_ ) ) {
      xtra_prev_lost_ = true;
      lost_packets_++;
      return(0);
  }
  xtra_prev_lost_ = false;
    
  if( Jitter < 0 ){
      lost_packets_++;
      return(0);
  }
    
  rcvTime_ms = sndTime_ms + Jitter;
	
#ifdef NWQ_DBG
  fprintf(log_file,"Time = %d ms : Add_Packet_to_NW_Queue called rcvTime_ms = %d numBytes = %d \n", sndTime_ms, rcvTime_ms, numBytes);
  fprintf(log_file,"packets_in_queue = %d write_idx = %d read_idx = %d \n", packets_in_queue_, write_idx_, read_idx_);
#endif
	
  if( numBytes <= 0 ){
    /* DTX mode or packet loss */
    return(0);
  }
  if (packets_in_queue_ == NUM_PACKETS) {
#ifdef NWQ_DBG
    fprintf(log_file_,"NW Queue full \n");
#endif
    return(-1);
  }
  if(packets_[write_idx_].data_length != -1){
#ifdef NWQ_DBG
    fprintf(log_file,"NW Queue error \n");
#endif
    return(-1);
  }
    
  /* Check if packet reordering happened */
  num_packets_to_move = 0;
  prev_write_idx = write_idx_;
  for( i = 0; i < packets_in_queue_; i++ ){
    prev_write_idx = (prev_write_idx - 1) & (NUM_PACKETS -1);
    if(packets_[prev_write_idx].rcv_time_ms <= rcvTime_ms){
      break;
    }
#ifdef NWQ_DBG
    fprintf(log_file,"Packet reordering !! \n");
#endif
    num_packets_to_move++;
  }
  /* Move the packets that were overtaken by the newly arrived */
  src_idx = (write_idx_ -1) & (NUM_PACKETS-1);
  dst_idx = (src_idx + 1) & (NUM_PACKETS-1);
  for( i = 0; i < num_packets_to_move; i++){
      memcpy( &packets_[dst_idx],&packets_[src_idx], sizeof(struct NW_Queue_element));
      src_idx = (src_idx-1) & (NUM_PACKETS-1);
      dst_idx = (dst_idx-1) & (NUM_PACKETS-1);
  }
  /* Copy Packet into Queue */
  dst_idx = (write_idx_ - num_packets_to_move) & (NUM_PACKETS-1);
  memcpy( packets_[dst_idx].data, packet, numBytes*sizeof(unsigned char));
  packets_[dst_idx].data_length = numBytes;
  packets_[dst_idx].rcv_time_ms = rcvTime_ms;
  packets_[dst_idx].is_rtcp = is_rtcp;
  write_idx_ = (write_idx_+1) & (NUM_PACKETS-1);
  packets_in_queue_++;
    
#ifdef NWQ_DBG
  fprintf(log_file_,"Next arrival = %d ms \n", packets_[read_idx_].rcv_time_ms);
#endif
  return(0);
}

int NwSimulator::Get_Packet( /* (O) returns -1 if no more packets otherwise length of packet */
  unsigned char* packet,   /* (O) Pointer to RTP packet                    */
  int            time_ms   /* (I) Get Packets recieved up untill this time */
)
{
  bool is_rtcp;
	
  return Get_Packet_Internal(packet, &is_rtcp, time_ms);
}

int NwSimulator::Get_Packet_Internal( /* (O) returns -1 if no more packets otherwise length of packet */
  unsigned char* packet,   /* (O) Pointer to RTP packet                    */
  bool           *is_rtcp,
  int            time_ms   /* (I) Get Packets recieved up untill this time */
)
{
  int numBytes;

#ifdef NWQ_DBG
    fprintf(log_file_,"Time = %d ms : Get_Packet_from_NW_Queue called \n", time_ms);
    fprintf(log_file_,"packets_in_queue = %d write_idx = %d read_idx = %d \n", packets_in_queue_, write_idx_, read_idx_);
#endif
    
  if( packets_in_queue_ == 0){
#ifdef NWQ_DBG
    fprintf(log_file_,"NW Queue empty !! \n");
#endif
    return(-1);
  }
  if( packets_[read_idx_].rcv_time_ms <= time_ms ){
    numBytes = packets_[read_idx_].data_length;
    if(numBytes < 0 || numBytes > MAX_BYTES_PER_PACKET){
#ifdef NWQ_DBG
      fprintf(log_file_,"Error invalid number of Bytes !! %d \n", numBytes);
#endif
      return(-1);
    }

    *is_rtcp = packets_[read_idx_].is_rtcp;
    memcpy( packet, packets_[read_idx_].data, numBytes*sizeof(unsigned char));
    packets_[read_idx_].data_length = -1;
    packets_[read_idx_].rcv_time_ms = -1;
    read_idx_ = (read_idx_ + 1) & (NUM_PACKETS -1);
    packets_in_queue_--;
#ifdef NWQ_DBG
    fprintf(log_file_,"Packet with %d bytes ready !! \n", numBytes);
#endif
    return(numBytes);
  } else{
    return(-1);
  }
}

int NwSimulator::GetLostPacketCount()
{
    return(lost_packets_);
}

int NwSimulator::GetPacketCount()
{
	return(num_packets_);
}

const char *NwSimulator::NWtype2Str(NW_type nw_type)
{
    switch (nw_type) {
            
        case NW_type_clean:         return "clean";
        case NW_type_sawtooth:      return "sawtooth";
        case NW_type_wifi:          return "wifi";
        case NW_type_2G:            return "2G";
        case NW_type_3G:            return "3G";
        case NW_type_4G:            return "4G";
        default: return "?";
    }
}


