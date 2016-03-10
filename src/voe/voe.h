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

#include <ogg/ogg.h>
#include <pthread.h>

/* common */

#define MILLISECONDS_PER_SECOND 1000


/* main file */
void voe_multi_party_packet_rate_control(struct voe *voe);
void voe_start_audio_proc(struct voe *voe);
void voe_stop_audio_proc(struct voe *voe);
void voe_update_agc_settings(struct voe *voe);
void voe_update_aec_settings(struct voe *voe);
int voe_update_mute(struct voe *voe);

/* encoder */

struct auenc_state {
	const struct aucodec *ac;  /* inheritance */

	struct voe_channel *ve;
	struct le le;
	auenc_rtp_h *rtph;
	auenc_rtcp_h *rtcph;
	auenc_packet_h *pkth;
	auenc_err_h *errh;
	void *arg;
};

int voe_enc_alloc(struct auenc_state **aesp,
                  struct media_ctx **mctxp,
                  const struct aucodec *ac, const char *fmtp, int pt,
                  uint32_t srate, uint8_t ch,
                  auenc_rtp_h *rtph,
                  auenc_rtcp_h *rtcph,
                  auenc_packet_h *pkth,
                  auenc_err_h *errh,
                  void *arg);

int  voe_enc_start(struct auenc_state *aes);
void voe_enc_stop(struct auenc_state *aes);

struct znw_stats{
    webrtc::NetworkStatistics neteq_nw_stats;
    int64_t Rtt_ms;
    unsigned int jitter_smpls;
    uint16_t uplink_loss_q8;
    unsigned int uplink_jitter_smpls;
};

#define NUM_STATS 32
#define NW_STATS_DELTA 10
struct nw_stats {
    struct znw_stats nw_stats[NUM_STATS];
    int idx_;
    int channel_number_;
};

/* decoder */

struct audec_state {
	const struct aucodec *ac;  /* inheritance */

	struct voe_channel *ve;
	struct le le;

	audec_recv_h *recvh;
	audec_err_h *errh;
	void *arg;
};

int  voe_dec_alloc(struct audec_state **adsp,
		   struct media_ctx **mctxp,
		   const struct aucodec *ac,
		   const char *fmtp, int pt, uint32_t srate, uint8_t ch,
		   audec_recv_h *recvh,
		   audec_err_h *errh,
		   void *arg);

int  voe_dec_start(struct audec_state *ads);
int  voe_dec_stats(struct audec_state *ads, struct mbuf **mbp);
void voe_dec_stop(struct audec_state *ads);
void voe_calculate_stats(int ch);
void voe_set_channel_load(struct voe *voe);

/* Voice Messaging */
class VmTransport;

struct vm_state {
    int ch;
    VmTransport *transport;
    FILE *fp;
    pthread_mutex_t mutex;
    struct tmr tmr_vm_player;
    struct timeval next_event;
    webrtc::CodecInst c;
    uint16_t seqNum;
    uint32_t timeStamp;
    uint32_t ssrc;
    ogg_sync_state   oy;
    ogg_page         og;
    ogg_stream_state os;
    int start_time_ms;
    int finished;
    uint32_t samplepos;
    uint32_t samplestot;
    int stream_init;
    vm_play_status_h *play_statush;
    void             *play_statush_arg;
    unsigned int file_length_ms;
};

void voe_vm_init(struct vm_state *vm);

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
};

int voe_ve_alloc(struct voe_channel **vep, const struct aucodec *ac,
		 uint32_t srate, int pt);
void voe_transportl_flush(void);


/* global data */

struct channel_settings {
	int  channel_number_;
	int  bitrate_bps_;
	int  packet_size_ms_;
	bool using_dtx_;
	int  last_rtcp_rtt;
	int  last_rtcp_ploss;
};

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
	webrtc::VoEConfControl *conferencing;

	webrtc::CodecInst *codecs;
	size_t ncodecs;

	int nch;
	std::vector<channel_settings> active_channel_settings;
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
    
	const char *path_to_files;
    
	struct tmr tmr_neteq_stats;
    
	struct mqueue *mq;
	struct list transportl;
    
	std::string playout_device;
    
    std::vector<nw_stats> nws;
    
    struct vm_state vm;
};

extern struct voe gvoe;

