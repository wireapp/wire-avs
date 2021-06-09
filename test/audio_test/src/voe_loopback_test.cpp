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
#include <unistd.h>

#include "webrtc/modules/audio_coding/include/audio_coding_module.h"
#include "webrtc/system_wrappers/include/trace.h"

#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_file.h"
#include "webrtc/voice_engine/include/voe_volume_control.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"

#include <pthread.h>

#if defined(WEBRTC_ANDROID)
#include <android/log.h>
#endif

#if defined(WEBRTC_ANDROID)
#define LOG(...) ((void)__android_log_print(ANDROID_LOG_INFO, "audiotest : voe_loopback_dec", __VA_ARGS__))
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

/**********************************/
/* RTP help functions             */
/**********************************/

#define RTP_HEADER_IN_BYTES 12
#define MAX_PACKET_SIZE_BYTES 1000

static int GetRTPheader( const uint8_t* rtpHeader,
                  bool* markerbit,
                  uint8_t* payloadType,
                  uint16_t* seqNum,
                  uint32_t* timeStamp,
                  uint32_t* ssrc){
    
    *markerbit   = (bool)rtpHeader[1] >> 7;
    *payloadType = (uint8_t)rtpHeader[1] & 0x7f;
    *seqNum      = (uint16_t)((rtpHeader[2] << 8) | rtpHeader[3]);
    *timeStamp   = (uint32_t)((rtpHeader[4] << 24) | (rtpHeader[5] << 16)  |
                              (rtpHeader[6] << 8) | rtpHeader[7]);
    *ssrc        = (uint32_t)((rtpHeader[8] << 24) | (rtpHeader[9] << 16)  |
                              (rtpHeader[10] << 8) | rtpHeader[11]);
    return(0);
}

struct loop_back_state {
    uint8_t rtcp_packet_[100];
    size_t rtcp_bytes_;
    uint8_t rtp_packet_[1000];
    size_t rtp_bytes_;
	int channel_id_;
    bool is_running_;
    webrtc::VoENetwork *nw_;
    pthread_mutex_t mutex_;
};

static void init_loop_back_state(struct loop_back_state * st){
    st->rtcp_bytes_ = 0;
    st->rtp_bytes_ = 0;
    pthread_mutex_init(&st->mutex_,NULL);
    pthread_mutex_unlock(&st->mutex_);
}
static void free_loop_back_state(struct loop_back_state * st){
    pthread_mutex_destroy(&st->mutex_);
}

struct timeval gStartTime;
static bool is_running;

class VoETransport : public webrtc::Transport {
public:
    VoETransport(struct loop_back_state *loop_back){
        _first_packet_time_ms = -1;
        _last_packet_time_ms = -1;
        _tot_bytes = 0;
        _loss_rate = 0;
        _avg_burst_length = 1.0f;
        _xtra_prev_lost = false;
        _loop_back_state = NULL;
    };
	
    virtual ~VoETransport() {
        float avgBitRate = (float)_tot_bytes * 8.0f / (float)( _last_packet_time_ms - _first_packet_time_ms);
        LOG("channel ? average bitrate(incl RTP header) = %.2f kbps \n", avgBitRate);
    };
	
	virtual bool SendRtp(const uint8_t* packet, size_t length, const webrtc::PacketOptions& options) {
        struct timeval now, res;
        gettimeofday(&now, NULL);
        timersub(&now, &gStartTime, &res);
        int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
		
        _tot_bytes += length;
        if(_first_packet_time_ms == -1){
			LOG("First packet sent at %d ms \n", ms);
            _first_packet_time_ms = ms;
        }
        _last_packet_time_ms = ms;
		
        float ploss = _loss_rate / 100.0f;
        if( ( !_xtra_prev_lost  && ((float)rand()/RAND_MAX) < ploss / (1.0f - ploss) / _avg_burst_length ) ||
            (  _xtra_prev_lost  && ((float)rand()/RAND_MAX) < 1.0f - 1.0f / _avg_burst_length ) ) {
            _xtra_prev_lost = true;
			return true;
        }
        _xtra_prev_lost = false;
		
        if(_loop_back_state){
            pthread_mutex_lock(&_loop_back_state->mutex_);
            if(length < sizeof(_loop_back_state->rtp_packet_)){
                memcpy(_loop_back_state->rtp_packet_, packet, length);
                _loop_back_state->rtp_bytes_ = length;
			}
			pthread_mutex_unlock(&_loop_back_state->mutex_);
        }
        return true;
    };
    
    virtual bool SendRtcp(const uint8_t* packet, size_t length) {
        if(_loop_back_state){
            pthread_mutex_lock(&_loop_back_state->mutex_);
            if(length < sizeof(_loop_back_state->rtcp_packet_)){
                memcpy(_loop_back_state->rtcp_packet_, packet, length);
                _loop_back_state->rtcp_bytes_ = length;
			}
			pthread_mutex_unlock(&_loop_back_state->mutex_);
        }
        return true;
    };
	
    void SetLoopBackState(struct loop_back_state *loop_back){
        _loop_back_state = loop_back;
    }

    void SetPacketLossParams(int loss_rate, float avg_burst_length){
        _loss_rate = loss_rate;
        _avg_burst_length = avg_burst_length;
    }
	
    void deregister()
    {
    }
    
private:
    int32_t _first_packet_time_ms;
    int32_t _last_packet_time_ms;
    int32_t _tot_bytes;
    int _loss_rate;
    float _avg_burst_length;
    bool _xtra_prev_lost;
    struct loop_back_state *_loop_back_state;
};

class VoELogCallback : public webrtc::TraceCallback {
public:
    VoELogCallback() {};
    virtual ~VoELogCallback() {};
    
    virtual void Print(webrtc::TraceLevel lvl, const char* message,
                       int len) override
    {        
        LOG("%s \n", message);
    };
};

static VoELogCallback logCb;

static void *loop_back_thread(void *arg){
    //struct channel_info *state = (struct channel_info*)arg;
	struct loop_back_state *state = (struct loop_back_state*)arg;
    int sleep_time_ms = 0;
    uint16_t tmp_packet[1000];
    size_t numBytes;

    while(state->is_running_){
        timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 10*1000*1000;
        nanosleep(&t, NULL);
        sleep_time_ms += 10;
		
        numBytes = 0;
        pthread_mutex_lock(&state->mutex_);
        if(state->rtcp_bytes_ > 0){
            memcpy(tmp_packet, state->rtcp_packet_, state->rtcp_bytes_);
            numBytes = state->rtcp_bytes_;
            state->rtcp_bytes_ = 0;
        }
        pthread_mutex_unlock(&state->mutex_);
        if(numBytes > 0){
            state->nw_->ReceivedRTCPPacket(state->channel_id_, tmp_packet, numBytes);
        }

        numBytes = 0;
        pthread_mutex_lock(&state->mutex_);
        if(state->rtp_bytes_ > 0){
            memcpy(tmp_packet, state->rtp_packet_, state->rtp_bytes_);
            numBytes = state->rtp_bytes_;
            state->rtp_bytes_ = 0;
        }
        pthread_mutex_unlock(&state->mutex_);
        if(numBytes > 0){
            state->nw_->ReceivedRTPPacket(state->channel_id_, tmp_packet, numBytes);
        }
	}
	return NULL;
}

#define VOE_THREAD_LOAD_PCT 100
#define MAIN_THREAD_LOAD_PCT 100

void *loopback_test_main_function(void *arg)
{
	//bool is_running = true;
	
    webrtc::VoiceEngine* ve = webrtc::VoiceEngine::Create();

    webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(ve);
    if (!base) {
        LOG("VoEBase::GetInterface failed \n");
    }
    webrtc::VoENetwork *nw = webrtc::VoENetwork::GetInterface(ve);
    if (!nw) {
        LOG("VoENetwork::GetInterface failed \n");
    }
    webrtc::VoECodec *codec = webrtc::VoECodec::GetInterface(ve);
    if (!codec) {
        LOG("VoECodec::GetInterface failed \n");
    }
    webrtc::VoEFile *file = webrtc::VoEFile::GetInterface(ve);
    if (!file) {
        LOG("VoEFile::GetInterface failed \n");
    }
    webrtc::VoEVolumeControl *volume = webrtc::VoEVolumeControl::GetInterface(ve);
    if (!volume) {
        LOG("VoEConfControl::GetInterface failed \n");
    }
    webrtc::VoEHardware *hw = webrtc::VoEHardware::GetInterface(ve);
    if (!hw) {
        LOG("VoEHardware::GetInterface failed \n");
    }
    webrtc::VoENetEqStats *nw_stats = webrtc::VoENetEqStats::GetInterface(ve);
    if (!nw_stats) {
        LOG("VoENetEqStats::GetInterface failed \n");
    }
	
    webrtc::Trace::CreateTrace();
    webrtc::Trace::SetTraceCallback(&logCb);
    webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical | webrtc::kTracePersist);
	
	gettimeofday(&gStartTime, NULL);
	
    base->Init();
    
	struct timeval now, res;
	gettimeofday(&now, NULL);
	timersub(&now, &gStartTime, &res);
	int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
	LOG("base->Init finished at %d ms \n", ms);
	
#if 0
#if TARGET_OS_IPHONE || defined(WEBRTC_ANDROID)
	std::string file_path = (const char *)arg;
    std::string playFileName;
    playFileName.insert(0,file_path);
    playFileName.insert(playFileName.size(),"/far32.pcm");
    file->StartPlayingFileAsMicrophone(-1, playFileName.c_str(),
                                       true, false,
                                       webrtc::kFileFormatPcm32kHzFile, 1.0);
#else
    file->StartPlayingFileAsMicrophone(-1, "../../../../../../test/audio_test/files/far32.pcm",
                                          true, false,
                                          webrtc::kFileFormatPcm32kHzFile, 1.0);
#endif
#endif
	
    webrtc::CodecInst c;
    
    int numberOfCodecs = codec->NumOfCodecs();
    bool codec_found = false;
    for( int i = 0; i < numberOfCodecs; i++ ){
        codec->GetCodec( i, c);
        if(strcmp(c.plname,"opus") == 0){
            codec_found = true;
            break;
        }
    }
	
    int channel_id = base->CreateChannel();
	
	struct loop_back_state loop_back_state;
	
    VoETransport* transport = new VoETransport(&loop_back_state);
        
    nw->RegisterExternalTransport(channel_id, *transport);
        
    c.rate = 40000;
    c.channels = 1;
    c.pltype = 96 + (int)(((float)rand()/RAND_MAX) * 31.0f);
	
    codec->SetSendCodec(channel_id, c);
		
    codec->SetRecPayloadType(channel_id, c);
		
    codec->SetFECStatus(channel_id, true);
		
    //codec->SetOpusDtx(channel_id, true);
		
    void* thread_ret;
	pthread_t tid;
	
	base->StartSend(channel_id);
	gettimeofday(&now, NULL);
	timersub(&now, &gStartTime, &res);
	ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
	LOG("base->StartSend finished at %d ms \n", ms);
	base->StartReceive(channel_id);
	base->StartPlayout(channel_id);
	init_loop_back_state(&loop_back_state);
	loop_back_state.channel_id_ = channel_id;
	loop_back_state.is_running_ = true;
	loop_back_state.nw_ = nw;
	transport->SetLoopBackState(&loop_back_state);
	
	pthread_create(&tid, NULL, loop_back_thread, &loop_back_state);
	
	/* sleep for 1 second */
    timespec t;
    t.tv_sec = 1;
    t.tv_nsec = 0;
    nanosleep(&t, NULL);
	
	base->StopSend(channel_id);
	loop_back_state.is_running_ = false;
	base->StopReceive(channel_id);
	base->StopPlayout(channel_id);
	
	LOG("Waiting to join loopbackthread \n");
	
	pthread_join(tid, &thread_ret);
	
	LOG("loopbackthread joined \n");
	
	free_loop_back_state(&loop_back_state);
	
    //printf("%d s - Encoding finished \n", cur_time);
    
    // Close down the transport ( file writing )
    nw->DeRegisterExternalTransport(channel_id);
    
    base->DeleteChannel(channel_id);
        
    delete transport;

    if(hw){
        hw->Release();
        hw = NULL;
    }
    if(volume){
        volume->Release();
        volume = NULL;
    }
    if(file){
        file->Release();
        file = NULL;
    }
    if(codec){
        codec->Release();
        codec = NULL;
    }
    if(nw){
        nw->Release();
        nw = NULL;
    }
    if(nw_stats){
        nw_stats->Release();
        nw_stats = NULL;
    }
    if(base){
        base->Terminate();
        base->Release();
        base = NULL;
    }
    
    webrtc::VoiceEngine::Delete(ve);
	
	is_running = false;
	
	return NULL;
}

#if TARGET_OS_IPHONE || defined(WEBRTC_ANDROID)
int voe_loopback_test(int argc, char *argv[], const char *path)
#else
int main(int argc, char *argv[])
#endif
{
	pthread_t tid;
	void* thread_ret;
	for (int i = 0; i < 10; i++){
		is_running = true;
#if TARGET_OS_IPHONE || defined(WEBRTC_ANDROID)
		std::string tmp_path = path;
		pthread_create(&tid, NULL, loopback_test_main_function, (void*)path);
#else
		pthread_create(&tid, NULL, loopback_test_main_function, NULL);
#endif
	
		while(is_running){
			timespec t;
			t.tv_sec = 1;
			t.tv_nsec = 0;
			nanosleep(&t, NULL);
		}
	
		pthread_join(tid, &thread_ret);
		LOG("iteration %d finished \n", i);
	}
		
	return 0;
}

