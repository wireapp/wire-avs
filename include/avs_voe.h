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

#ifndef AVS_VOE_H
#define AVS_VOE_H

#ifdef __cplusplus
extern "C" {
#endif
      
#include "avs_flowmgr.h"
#include "avs_audio_effect.h"
#include "avs_audio_io.h"
    
/*
 * VoE -- Voice Engine
 */
    
struct voe_stats {
	int avg_currentBufferSize;
	int num_measurements;
	    
	uint16_t max_currentBufferSize;
	uint16_t min_currentBufferSize;
	float std_currentBufferSize;
    
	int avg_PacketLossRate;
	uint16_t max_PacketLossRate;
	uint16_t min_PacketLossRate;
	float std_PacketLossRate;
    
	int avg_ExpandRate;
	uint16_t max_ExpandRate;
	uint16_t min_ExpandRate;
	float std_ExpandRate;

	int avg_AccelerateRate;
	uint16_t max_AccelerateRate;
	uint16_t min_AccelerateRate;
	float std_AccelerateRate;
    
	int avg_PreemptiveRate;
	uint16_t max_PreemptiveRate;
	uint16_t min_PreemptiveRate;
	float std_PreemptiveRate;
    
	int avg_SecondaryDecodedRate;
	uint16_t max_SecondaryDecodedRate;
	uint16_t min_SecondaryDecodedRate;
	float std_SecondaryDecodedRate;
    
	int32_t avg_RTT;
	int32_t max_RTT;
	int32_t min_RTT;
	float std_RTT;
    
	uint32_t avg_Jitter;
	uint32_t max_Jitter;
	uint32_t min_Jitter;
	float std_Jitter;
    
	int avg_UplinkPacketLossRate;
	uint16_t max_UplinkPacketLossRate;
	uint16_t min_UplinkPacketLossRate;
	float std_UplinkPacketLossRate;
    
	uint32_t avg_UplinkJitter;
	uint32_t max_UplinkJitter;
	uint32_t min_UplinkJitter;
	float std_UplinkJitter;

	uint16_t avg_InVol;
	uint16_t max_InVol;
	uint16_t min_InVol;
	float std_InVol;
    
	uint16_t avg_OutVol;
	uint16_t max_OutVol;
	uint16_t min_OutVol;
	float std_OutVol;
    
	float avg_jitterPeaksFound;

	struct {
		int downloss;
		int uploss;
		int rtt;
	} quality;
};

void voe_stats_init(struct voe_stats *vst);
int voe_stats_calc(int ch, struct voe_stats *vst);	
	
	
int  voe_init(struct list *aucodecl);
void voe_close(void);

int voe_set_auplay(const char *dev);

int voe_set_mute(bool mute);
int voe_get_mute(bool *muted);

int voe_start_silencing(void);
int voe_stop_silencing(void);
    
int voe_invol(struct auenc_state *aes, double *invol);
int voe_outvol(struct audec_state *ads, double *outvol);

int  voe_start_playing_PCM_file_as_microphone(const char fileNameUTF8[1024],
					      int fs);
void voe_stop_playing_PCM_file_as_microphone(void);

int  voe_start_recording_playout_PCM_file(const char fileNameUTF8[1024]);
void voe_stop_recording_playout_PCM_file(void);
    
int voe_set_bitrate(int rate_bps);
int voe_set_packet_size(int packet_size_ms);

void voe_register_adm(struct audio_io *aio);
void voe_deregister_adm(void);
    
int voe_debug(struct re_printf *pf, void *unused);

void voe_update_conf_parts(const struct audec_state *adsv[], size_t adsc);
    
int voe_set_audio_effect(enum audio_effect effect_type);
enum audio_effect voe_get_audio_effect(void);
    
void voe_set_audio_state_handler(
    flowmgr_audio_state_change_h *state_change_h,
    void *arg
);

void voe_set_file_path(const char pathUTF8[1024]);

#ifdef __cplusplus
}
#endif

#endif // AVS_VOE_H
