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

extern "C" {
    #include "avs_log.h"
}

enum interleaving_mode{
    INTERLEAVING_MODE_OFF = 0,
    INTERLEAVING_MODE_MIN,
    INTERLEAVING_MODE_MAX,
};

#define MAX_BYTES_PER_PACKET 616 /* 64 kbps @ 60 ms packets + header */
#define NUM_PACKETS_2          5
#define NUM_PACKETS_3          8
//#define DEBUG

struct packet_element {
  uint8_t data[ MAX_BYTES_PER_PACKET ];
  size_t data_length;
  int out_count;
};

int offset_2[NUM_PACKETS_2] = {2, 4, 1, 3, 0}; // Opus FEC repairs 2 consecutive losses
int offset_3[NUM_PACKETS_3] = {4, 6, 8, 2, 4, 6, 0, 2}; // Opus Fec repairs 3 consecutive losses

class interleaver{
public:    
    interleaver()
    {
        memset(packets_,0, sizeof(packets_));
        for(int i = 0; i < NUM_PACKETS_3; i++){
            packets_[i].data_length = -1;
            packets_[i].out_count = -1;
        }
        mode_ = INTERLEAVING_MODE_OFF;
        cycle_len_ = NUM_PACKETS_3;
        cnt_ = 0;
        offset_ = offset_3;
        should_flush_ = false;
    }    

    ~interleaver(){}

    void set_mode(enum interleaving_mode mode)
    {
        if(mode_ == INTERLEAVING_MODE_MIN){
            cycle_len_ = NUM_PACKETS_2;
            offset_ = offset_2;
        } else if (mode_ == INTERLEAVING_MODE_MAX){
            cycle_len_ = NUM_PACKETS_3;
            offset_ = offset_3;
        } else {
            if(mode_ != INTERLEAVING_MODE_OFF){
                should_flush_ = true;
            }
        }
        mode_ = mode;
    }
    
    size_t flush(uint8_t** packet_out)
    {
        if(!should_flush_){
            return 0;
        }
        for(int i = 0; i < cycle_len_; i++){
            if(packets_[i].data_length == -1){
                *packet_out = (uint8_t*)packets_[i].data;
                size_t packet_out_len = packets_[i].data_length;
                packets_[i].data_length = -1;
                return packet_out_len;
            }
        }
        should_flush_ = false;
        return 0;
    }
    
	size_t update(
		const uint8_t* packet_in,     /* (I) RTP packet */
		size_t            packet_in_len,   /* (I) length of RTP packet */
		uint8_t** packet_out    /* (O) RTP packet */
	)
    {
        size_t packet_out_len = 0;
        if(mode_ == INTERLEAVING_MODE_OFF){
            *packet_out = (uint8_t*)packet_in;
            packet_out_len = packet_in_len;
            cnt_++;
        } else {
            int idx = cnt_ % cycle_len_;
#ifdef DEBUG
            uint16_t seqNum      = (uint16_t)((packet_in[2] << 8) | packet_in[3]);
            info("idx = %d in seq = %d ", idx, seqNum);
#endif
            for(int i = 0; i < cycle_len_; i++){
                if(packets_[i].data_length == -1){
                    memcpy(packets_[i].data, packet_in, packet_in_len);
                    packets_[i].data_length = packet_in_len;
                    packets_[i].out_count = cnt_ + offset_[idx];
                    break;
                }
            }
            packet_out_len = 0;
            for(int i = 0; i < cycle_len_; i++){
                if(packets_[i].out_count == cnt_){
                    *packet_out = (uint8_t*)packets_[i].data;
                    packet_out_len = packets_[i].data_length;
                    packets_[i].data_length = -1;
                    break;
                }
            }
#ifdef DEBUG
            if(packet_out_len == 0){
                info("\n");
            } else {
                seqNum      = (uint16_t)(((*packet_out)[2] << 8) | (*packet_out)[3]);
                info("out seq = %d \n", seqNum);
            }
#endif
            cnt_++;
        }
        return packet_out_len;
    }
	
private:
	
    struct packet_element packets_[ NUM_PACKETS_3 ];
	int cnt_;
    int cycle_len_;
    int *offset_;
    enum interleaving_mode mode_;
    bool should_flush_;
};
