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
/* Internal interface for VOE module */

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif

#include <pthread.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_file.h"
#include "webrtc/voice_engine/include/voe_audio_processing.h"
#include "webrtc/voice_engine/include/voe_volume_control.h"
#include "webrtc/voice_engine/include/voe_rtp_rtcp.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"
#include "webrtc/voice_engine/include/voe_errors.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "webrtc/voice_engine/include/voe_external_media.h"

#include "avs.h"
#include "avs_ztime.h"
#include "avs_audio_io.h"
#include "avs_flowmgr.h"
#include "avs_rtpdump.h"

#include "audio_effect_interface.h"

/* common */

#define MILLISECONDS_PER_SECOND 1000

/* device */
void voe_start_audio_proc(struct voe *voe);
void voe_stop_audio_proc(struct voe *voe);
void voe_update_agc_settings(struct voe *voe);
void voe_update_aec_settings(struct voe *voe);
int voe_update_mute(struct voe *voe);

/* channel settings */
void voe_multi_party_packet_rate_control(struct voe *voe);
void voe_update_channel_stats(struct voe *voe,
                              int channel_id,
                              int rtcp_rttMs,
                              int rtcp_loss_Q8);

/* encoder */

struct auenc_state {
	const struct aucodec *ac;  /* inheritance */

	struct voe_channel *ve;
	struct le le;
	bool started;
	auenc_rtp_h *rtph;
	auenc_rtcp_h *rtcph;
	auenc_err_h *errh;
	void *arg;
    
	uint32_t local_ssrc;
	uint32_t remote_ssrc;
	uint8_t  pt;
	uint32_t srate;
	uint8_t  ch;
	bool cbr;
};

int voe_enc_alloc(struct auenc_state **aesp,
		  const struct aucodec *ac, const char *fmtp,
		  struct aucodec_param *prm,
		  auenc_rtp_h *rtph,
		  auenc_rtcp_h *rtcph,
		  auenc_err_h *errh,
		  void *extcodec_arg,
		  void *arg);

int  voe_enc_start(struct auenc_state *aes, bool cbr,
		   const struct aucodec_param *prm,
		   struct media_ctx **mctxp);
void voe_enc_stop(struct auenc_state *aes);

/* decoder */

struct audec_state {
	const struct aucodec *ac;  /* inheritance */

	struct voe_channel *ve;
	struct le le;

	audec_err_h *errh;
    
	void *arg;
    
	uint8_t  pt;
	uint32_t srate;
};

int  voe_dec_alloc(struct audec_state **adsp,
		   const struct aucodec *ac,
		   const char *fmtp,
		   struct aucodec_param *prm,
		   audec_err_h *errh,
		   void *extcodec_arg,
		   void *arg);

int  voe_dec_start(struct audec_state *ads, struct media_ctx **mctxp);
int  voe_get_stats(struct audec_state *ads, struct aucodec_stats *new_stats);
void voe_dec_stop(struct audec_state *ads);
void voe_calculate_stats(int ch);
void voe_set_channel_load(struct voe *voe);

int voe_start_preproc_recording(const char fileNameUTF8[1024]);
void voe_stop_preproc_recording();
void voe_start_rtp_dump(struct voe_channel *ve);

/* Audio Testing */
struct audio_test_state {
	int test_score;
	bool is_running;
	struct audio_io *aio;
	class VoEAudioOutputAnalyzer *output_analyzer;
};

void voe_init_audio_test(struct audio_test_state *autest);
void voe_start_audio_test(struct voe *voe);
void voe_stop_audio_test(struct voe *voe);

/* shared state */

enum {
	VOE_MQ_ERR = 0,
};

class VoETransport;



struct voe_channel {
	const struct aucodec *ac;
    
	struct auenc_state *aes;  /* pointer */
	struct audec_state *ads;  /* pointer */
    
	int ch; /* VoiceEngine channel */

	uint32_t srate;
	int pt;

	VoETransport *transport;
    
	wire_avs::RtpDump* rtp_dump_in;
	wire_avs::RtpDump* rtp_dump_out;
};

int voe_ve_alloc(struct voe_channel **vep, const struct aucodec *ac,
		 uint32_t srate, int pt);
void voe_transportl_flush(void);

#if TARGET_OS_IPHONE
const char* voe_iosfilepath(void);
#endif


#define NUM_STATS 16
#define NW_STATS_DELTA 2

struct channel_stats{
    webrtc::NetworkStatistics neteq_nw_stats;
    int64_t Rtt_ms;
    unsigned int jitter_smpls;
    uint16_t uplink_loss_q8;
    unsigned int uplink_jitter_smpls;
    uint16_t in_vol;
    uint16_t out_vol;
};

struct channel_data {
	struct le le;
	int  channel_number;
	int  bitrate_bps;
	int  packet_size_ms;
	bool using_dtx;
	int  last_rtcp_rtt;
	int  last_rtcp_ploss;
	bool interrupted;
	struct channel_stats ch_stats[NUM_STATS];
	int stats_idx;
	int stats_cnt;
	float out_vol_smth;

	struct {
		int downloss;
		int uploss;
		int rtt;
	} quality;
};

int channel_data_add(struct list *ch_list, int ch, webrtc::CodecInst &c);
struct channel_data *find_channel_data(struct list *active_chs, int ch);

/* global data */
struct voe {
	webrtc::VoiceEngine* ve;
	webrtc::VoEBase* base;
	webrtc::VoENetwork *nw;
	webrtc::VoECodec *codec;
	webrtc::VoEVolumeControl *volume;
	webrtc::VoEFile *file;
	webrtc::VoEAudioProcessing *processing;
	webrtc::VoERTP_RTCP *rtp_rtcp;
	webrtc::VoENetEqStats *neteq_stats;
	webrtc::VoEHardware *hw;
	webrtc::VoEExternalMedia *external_media;
    
	webrtc::CodecInst *codecs;
	size_t ncodecs;

	int nch;
	struct list channel_data_list;
	int packet_size_ms;
	int min_packet_size_ms;
	int manual_packet_size_ms;
	int bitrate_bps;
	int manual_bitrate_bps;

	struct list encl;  /* struct auenc_state */
	struct list decl;  /* struct audec_state */

	bool is_playing;
	bool is_recording;
	bool is_rtp_recording;
    
	bool isMuted;
	bool isSilenced;
    
	bool cbr_enabled;
    
	char *path_to_files;
    
	struct tmr tmr_neteq_stats;
    
	struct mqueue *mq;
	struct list transportl;
    
	char *playout_device;
  
	float in_vol_smth;
	uint16_t in_vol_max;
	uint16_t out_vol_max;
    
	struct audio_test_state autest;
    
	struct audio_io *aio;

	class VoEAudioEffect *voe_audio_effect;
    
	struct {
		flowmgr_audio_state_change_h *chgh;
		void *arg;
	} state;
};

extern struct voe gvoe;

