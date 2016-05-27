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
#include <stdio.h>
#include <stdio.h>
#include <string>

#define MAX_BYTES_PER_PACKET 616 /* 64 kbps @ 60 ms packets + header */
#define NUM_PACKETS          (1 << 10)
//#define NWQ_DBG

enum NW_type {
    NW_type_clean,
    NW_type_wifi,
    NW_type_4G,
    NW_type_3G,
    NW_type_2G,
    NW_type_sawtooth
};

struct NW_Queue_element {
  unsigned char data[ MAX_BYTES_PER_PACKET ];
  int data_length;
  int rcv_time_ms;
  bool is_rtcp;
};

class NwSimulator{
public:    
    NwSimulator();

    ~NwSimulator();
   
    int Init(
         int packet_size_ms,
         int xtra_loss_rate,
         float avg_burst_length,
         NW_type network,
         std::string file_path
    );
    
    int Add_Packet( /* returns -1 if Queue is full otherwise 0 */
        unsigned char* packet,     /* (I) RTP packet */
        int            numBytes,   /* (I) length of RTP packet */
        int            sndTime_ms  /* (I) Time when send in ms */
    );
    
    int Add_Packet_Internal( /* returns -1 if Queue is full otherwise 0 */
        unsigned char* packet,     /* (I) RTP packet */
        int            numBytes,   /* (I) length of RTP packet */
        int            sndTime_ms,  /* (I) Time when send in ms */
        bool           is_rtcp
    );
    
    int Get_Packet( /* (O) returns -1 if no more packets otherwise length of packet */
        unsigned char* packet,   /* (O) Pointer to RTP packet                    */
        int            time_ms   /* (I) Get Packets recieved up untill this time */
    );
    
    int Get_Packet_Internal( /* (O) returns -1 if no more packets otherwise length of packet */
        unsigned char* packet,   /* (O) Pointer to RTP packet                    */
        bool           *is_rtcp,
        int            time_ms   /* (I) Get Packets recieved up untill this time */
    );
    
    int GetLostPacketCount();
    
    int GetPacketCount();
    
    static const char *NWtype2Str(NW_type nw_type);
    
private:
    int Setup_Jitter_File(bool add_offset);
    
    struct NW_Queue_element packets_[ NUM_PACKETS ];
    int read_idx_;
    int write_idx_;
    int packets_in_queue_;
    int xtra_loss_rate_;
    float avg_burst_length_;
    bool xtra_prev_lost_;
    int packet_size_ms_;
    int lost_packets_;
    int num_packets_;
    int start_time_offset_;
    int32_t Jitter_from_file_;
    int32_t time_from_file_;
    NW_type network_;
    std::string file_path_;
    FILE *jitter_file_;
    FILE *log_file_;
};
