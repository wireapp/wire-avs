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
#include "webrtc/voice_engine/include/voe_rtp_rtcp.h"

#include "NwSimulator.h"

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
    bool is_running_;
    webrtc::VoENetwork *nw_;
    pthread_mutex_t mutex_;
};

void init_loop_back_state(struct loop_back_state * st){
    st->rtcp_bytes_ = 0;
    st->rtp_bytes_ = 0;
    pthread_mutex_init(&st->mutex_,NULL);
    pthread_mutex_unlock(&st->mutex_);
}
void free_loop_back_state(struct loop_back_state * st){
    pthread_mutex_destroy(&st->mutex_);
}

class VoETransport : public webrtc::Transport {
public:
    VoETransport(std::string name, struct loop_back_state *loop_back){
        _fp = fopen(name.c_str(),"wb");
        gettimeofday(&_startTime, NULL);
        _first_packet_time_ms = -1;
        _last_packet_time_ms = -1;
        _tot_bytes = 0;
        _loss_rate = 0;
        _avg_burst_length = 1.0f;
        _xtra_prev_lost = false;
        _loop_back_state = NULL;
    };
	
    virtual ~VoETransport() {
        fclose(_fp);
        float avgBitRate = (float)_tot_bytes * 8.0f / (float)( _last_packet_time_ms - _first_packet_time_ms);
        printf("channel ? average bitrate(incl RTP header) = %.2f kbps \n", avgBitRate);
    };
	
	virtual bool SendRtp(const uint8_t* packet, size_t length, const webrtc::PacketOptions& options) {
        struct timeval now, res;
        gettimeofday(&now, NULL);
        timersub(&now, &_startTime, &res);
        int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
		
        _tot_bytes += length;
        if(_first_packet_time_ms == -1){
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
		
        fwrite( &ms, sizeof(int32_t), 1, _fp);
        fwrite( &length, sizeof(size_t), 1, _fp);
        fwrite( packet, sizeof(uint8_t), length, _fp);
		
        if(_loop_back_state){
            pthread_mutex_lock(&_loop_back_state->mutex_);
            if(length < sizeof(_loop_back_state->rtp_packet_)){
                memcpy(_loop_back_state->rtp_packet_,packet, length);
                _loop_back_state->rtp_bytes_ = length;
			}
			pthread_mutex_unlock(&_loop_back_state->mutex_);
        }
        return true;
    };
	
	virtual bool SendRtcp(const uint8_t* packet, size_t length) {
        struct timeval now, res;
        gettimeofday(&now, NULL);
        timersub(&now, &_startTime, &res);
        int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
		
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
    FILE* _fp;
    struct timeval _startTime;
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
        printf("%s \n", message);
    };
};

static VoELogCallback logCb;

int GetNextEvent(FILE *ctrlFile,
                         char* str,
                         int* time,
                         int* ch,
                         int* param
)
{
    int ret = 0;
    *param = -1;
    *ch = -1;
    
    ret = fscanf (ctrlFile, "%d %s", time, str);
    if(strcmp(str,"StartSend") == 0     ||
       strcmp(str,"StopSend") == 0      ||
       strcmp(str,"StartMicMute") == 0  ||
       strcmp(str,"StopMicMute") == 0   ||
       strcmp(str,"SetBitRate") == 0    ||
       strcmp(str,"SetPacketSize") == 0 ||
       strcmp(str,"SetPacketLoss") == 0 ||
       strcmp(str,"ResetAudioDevice") == 0 ||
       strcmp(str,"Stop") == 0 ){
        
        if(strcmp(str,"Stop")){
            ret += fscanf (ctrlFile, "%d", ch);
            if(strcmp(str,"SetBitRate") == 0 ||
               strcmp(str,"SetPacketLoss") == 0 ||
               strcmp(str,"SetPacketSize") == 0 ){
                ret += fscanf (ctrlFile, "%d", param);
            }
        }
    } else {
        printf("Unknown Event : %s \n", str);
        return -1;
    }
    
    return ret;
}

struct channel_info {
    int channel_number_;
    int channel_id_;
    int pltype;
    VoETransport*  transport_;
    std::string file_name_;
    pthread_t tid;
    struct loop_back_state loop_back_state_;
};

struct neteq_monitor_info{
    std::vector<struct channel_info> *ch_info_vec;
    webrtc::VoENetEqStats *neteq_stats;
    webrtc::VoERTP_RTCP *rtp_rtcp;
    bool is_running;
};

void *neteq_monitor(void *arg){
    struct neteq_monitor_info *info = (struct neteq_monitor_info *)arg;
    webrtc::NetworkStatistics stats;
    int sleep_time = 2;
    int tot_time = 0;
    unsigned int NTPHigh = 0, NTPLow = 0, timestamp = 0, playoutTimestamp = 0, jitter = 0;
    unsigned short fractionLostUp_Q8 = 0; // Uplink packet loss as reported by remote side
    webrtc::CallStatistics rtcp_stats;
    float smth_jitter = 0;
    while(info->is_running){
        timespec t;
        t.tv_sec = 1;
        t.tv_nsec = 0;
        nanosleep(&t, NULL);
        if(sleep_time >= 2){
            for( auto it = (*info->ch_info_vec).begin(); it < (*info->ch_info_vec).end(); it++){
			    info->neteq_stats->GetNetworkStatistics(it->channel_number_, stats);
                printf("---- %ds NetEQ stats channel(%d) ---- \n", tot_time, it->channel_number_);
                printf("loss rate = %.2f fec repair rate = %.2f \n", (float)stats.currentPacketLossRate/163.84f, (float)stats.currentSecondaryDecodedRate/163.84f);
				info->rtp_rtcp->GetRemoteRTCPData( it->channel_number_, NTPHigh, NTPLow, timestamp, playoutTimestamp, &jitter, &fractionLostUp_Q8);
				
				info->rtp_rtcp->GetRTCPStatistics( it->channel_number_, rtcp_stats);
				
				if(jitter > smth_jitter){
					smth_jitter = 0.9*(float)jitter + 0.1*smth_jitter;
				} else {
					smth_jitter = 0.1*(float)jitter + 0.9*smth_jitter;
				}
				
				//printf(" ---------- Uplink Jitter value = %d %f rtt = %lld \n", jitter, smth_jitter, rtcp_stats.rttMs);
            }
            sleep_time = 0;
        }
        sleep_time++;
        tot_time++;
    }
    return NULL;
}

void *loop_back_thread(void *arg){
    struct channel_info *state = (struct channel_info*)arg;
    int sleep_time_ms = 0;
    uint8_t tmp_packet[1000];
    int numBytes;
	bool is_rtcp;
	
	NwSimulator *nws = new NwSimulator();
	//nws->Init(0, 0, 1.0f, NW_type_clean, "../../../../../../test/audio_test/files/");
	nws->Init(0, 0, 1.0f, NW_type_clean, "../../files/");
	
    while(state->loop_back_state_.is_running_){
        timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 10*1000*1000;
        nanosleep(&t, NULL);
        sleep_time_ms += 10;
		
        numBytes = 0;
        pthread_mutex_lock(&state->loop_back_state_.mutex_);
        if(state->loop_back_state_.rtcp_bytes_ > 0){
			nws->Add_Packet_Internal(state->loop_back_state_.rtcp_packet_,
                                     (int)state->loop_back_state_.rtcp_bytes_,
									 sleep_time_ms,
                                     true);
			state->loop_back_state_.rtcp_bytes_ = 0;
        }
        pthread_mutex_unlock(&state->loop_back_state_.mutex_);

        numBytes = 0;
        pthread_mutex_lock(&state->loop_back_state_.mutex_);
        if(state->loop_back_state_.rtp_bytes_ > 0){
			nws->Add_Packet_Internal(state->loop_back_state_.rtp_packet_,
									 (int)state->loop_back_state_.rtp_bytes_,
									 sleep_time_ms,
									 false);
            state->loop_back_state_.rtp_bytes_ = 0;
        }
        pthread_mutex_unlock(&state->loop_back_state_.mutex_);
		numBytes = nws->Get_Packet_Internal( tmp_packet, &is_rtcp, sleep_time_ms);
		while(numBytes > 0){
			if(is_rtcp){
				state->loop_back_state_.nw_->ReceivedRTCPPacket(state->channel_id_, tmp_packet, numBytes);
			} else {
				state->loop_back_state_.nw_->ReceivedRTPPacket(state->channel_id_, tmp_packet, numBytes);
			}
			numBytes = nws->Get_Packet_Internal( tmp_packet, &is_rtcp, sleep_time_ms);
		}
	}
	delete nws;
	
	return NULL;
}

#if TARGET_OS_IPHONE
int voe_conf_test(int argc, char *argv[], const char *path)
#else
int main(int argc, char *argv[])
#endif
{
    webrtc::VoiceEngine* ve = webrtc::VoiceEngine::Create();

    webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(ve);
    if (!base) {
        printf("VoEBase::GetInterface failed \n");
    }
    webrtc::VoENetwork *nw = webrtc::VoENetwork::GetInterface(ve);
    if (!nw) {
        printf("VoENetwork::GetInterface failed \n");
    }
    webrtc::VoECodec *codec = webrtc::VoECodec::GetInterface(ve);
    if (!codec) {
        printf("VoECodec::GetInterface failed \n");
    }
    webrtc::VoEFile *file = webrtc::VoEFile::GetInterface(ve);
    if (!file) {
        printf("VoEFile::GetInterface failed \n");
    }
    webrtc::VoEVolumeControl *volume = webrtc::VoEVolumeControl::GetInterface(ve);
    if (!volume) {
        printf("VoEConfControl::GetInterface failed \n");
    }
    webrtc::VoEHardware *hw = webrtc::VoEHardware::GetInterface(ve);
    if (!hw) {
        printf("VoEHardware::GetInterface failed \n");
    }
    webrtc::VoENetEqStats *nw_stats = webrtc::VoENetEqStats::GetInterface(ve);
    if (!nw_stats) {
        printf("VoENetEqStats::GetInterface failed \n");
    }
	webrtc::VoERTP_RTCP *rtp_rtcp = webrtc::VoERTP_RTCP::GetInterface(ve);
	if (!rtp_rtcp) {
		printf("VoERTP_RTCP::GetInterface failed \n");
	}
	
#if TARGET_OS_IPHONE
    std::string ctrl_file_name;
    ctrl_file_name.insert(0,path);
    ctrl_file_name.insert(ctrl_file_name.length(),"/conf_test_control.txt");
    FILE *ctrlFile = fopen(ctrl_file_name.c_str(),"rt");
#else
    //FILE *ctrlFile = fopen("../../../../../../test/audio_test/files/conf_test_control.txt","rt");
	FILE *ctrlFile = fopen("../../files/conf_test_control.txt","rt");
#endif
	if(!ctrlFile){
		printf("Cannot open conferencing test configuration file ! \n");
	}
	
    char str[80];
    int numChannels;
    fscanf (ctrlFile, "%s %d", str, &numChannels);
    printf("numChannels = %d \n", numChannels);
    
    webrtc::Trace::CreateTrace();
    webrtc::Trace::SetTraceCallback(&logCb);
    webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical | webrtc::kTracePersist);
    
    base->Init();
    
#if TARGET_OS_IPHONE
    std::string playFileName;
    playFileName.insert(0,path);
    playFileName.insert(playFileName.size(),"/far32.pcm");
    file->StartPlayingFileAsMicrophone(-1, playFileName.c_str(),
                                       true, false,
                                       webrtc::kFileFormatPcm32kHzFile, 1.0);
#else
    //file->StartPlayingFileAsMicrophone(-1, "../../../../../../test/audio_test/files/far32.pcm",
	file->StartPlayingFileAsMicrophone(-1, "../../files/far32.pcm",
                                          true, false,
                                          webrtc::kFileFormatPcm32kHzFile, 1.0);
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
	
    std::vector<struct channel_info> ch_info_vec;
    
    // Setup the wanted Channels
    for( int i = 0; i < numChannels; i++){
        struct channel_info ch_info;
        
        ch_info.channel_number_ = i;
		
        ch_info.channel_id_ = base->CreateChannel();
        
        char buf[10];
        sprintf(buf,"%d",ch_info.channel_number_);
#if TARGET_OS_IPHONE
        ch_info.file_name_.insert(0,path);
        ch_info.file_name_.insert(ch_info.file_name_.length(),"/out");
#else
        ch_info.file_name_ = "out";
#endif
        ch_info.file_name_.insert(ch_info.file_name_.size(),buf);
        ch_info.file_name_.insert(ch_info.file_name_.size(),".dat");

        printf("ch_info.file_name_ = %s \n", ch_info.file_name_.c_str());
        
        ch_info.transport_ = new VoETransport(ch_info.file_name_, &ch_info.loop_back_state_);
        
        nw->RegisterExternalTransport(ch_info.channel_id_, *ch_info.transport_);
        
        c.rate = 40000;
        c.channels = 1;
        c.pltype = 96 + (int)(((float)rand()/RAND_MAX) * 31.0f);
        ch_info.pltype = c.pltype;
        
        codec->SetSendCodec(ch_info.channel_id_, c);
		
        codec->SetRecPayloadType(ch_info.channel_id_, c);
		
        codec->SetFECStatus(ch_info.channel_id_, true);
		
        codec->SetOpusDtx(ch_info.channel_id_, true);
        
        ch_info_vec.push_back(ch_info);
    }
	
    int cur_time, next_time, sleep_time, ch, parm, ret;
    void* thread_ret;
    pthread_t tid;
    struct neteq_monitor_info neteq_info;
    neteq_info.is_running = true;
    neteq_info.ch_info_vec = &ch_info_vec;
    neteq_info.neteq_stats = nw_stats;
	neteq_info.rtp_rtcp = rtp_rtcp;
    pthread_create(&tid, NULL, neteq_monitor, &neteq_info);
	
    cur_time = 0;
    while(1){
        ret = GetNextEvent(ctrlFile, str, &next_time, &ch, &parm);
        if(ret < 0){
            break;
        }
        sleep_time = std::max(next_time - cur_time,0);
        
        timespec t;
        t.tv_sec = sleep_time;
        t.tv_nsec = 0;
        nanosleep(&t, NULL);
		
        cur_time = next_time;
        
        /* Handle the Event */
        if(!(strcmp(str,"Stop") == 0) && !(strcmp(str,"Invalid") == 0)){
            int ch_id = -1;
            std::vector<struct channel_info>::iterator it;
            for( it = ch_info_vec.begin(); it < ch_info_vec.end(); it++){
                if(it->channel_number_ == ch){
                    ch_id = it->channel_id_;
                    break;
                }
            }
            if(ch_id == -1){
                printf("Cannot map ch %d to a channel id \n", ch);
            } else {
                printf("%d s - ", cur_time);
                if(strcmp(str,"StartSend") == 0){
                    printf("StartSend(%d) \n", ch);
                    base->StartSend(ch_id);
					base->StartReceive(ch_id);
					base->StartPlayout(ch_id);
                    init_loop_back_state(&it->loop_back_state_);
					it->loop_back_state_.is_running_ = true;
                    it->loop_back_state_.nw_ = nw;
                    it->transport_->SetLoopBackState(&it->loop_back_state_);
					pthread_create(&it->tid, NULL, loop_back_thread, &(*it));
                }
                if(strcmp(str,"StopSend") == 0){
					printf("StopSend(%d) \n", ch);
					base->StopSend(ch_id);
					it->loop_back_state_.is_running_ = false;
					base->StopReceive(ch_id);
					base->StopPlayout(ch_id);
                    pthread_join(it->tid, &thread_ret);
                    free_loop_back_state(&it->loop_back_state_);
                }
                if(strcmp(str,"StartMicMute") == 0){
                    printf("StartMicMute(%d) \n", ch);
                    volume->SetInputMute(-1, false, true);
                }
                if(strcmp(str,"StopMicMute") == 0){
                    printf("StopMicMute(%d) \n", ch);
                    volume->SetInputMute(-1, false, false);
                }
                if(strcmp(str,"SetBitRate") == 0){
                    webrtc::CodecInst c;
                    printf("SetBitRate(%d) to %d bps \n", ch, parm);
                    codec->GetSendCodec(ch_id, c);
                    c.rate = parm;
                    codec->SetSendCodec(ch_id, c);
                }
                if(strcmp(str,"SetPacketSize") == 0){
                    webrtc::CodecInst c;
                    printf("SetPacketSize(%d) to %d ms \n", ch, parm);
                    codec->GetSendCodec(ch_id, c);
                    c.pacsize = (c.plfreq * parm) / 1000;
                    codec->SetSendCodec(ch_id, c);
                }
                if(strcmp(str,"SetPacketLoss") == 0){
                    it->transport_->SetPacketLossParams(parm, 1.2f);
                    printf("SetPacketLoss(%d) to %d pct \n", ch, parm);
                }
                if(strcmp(str,"ResetAudioDevice") == 0){
                    hw->ResetAudioDevice();
                    printf("ResetAudioDevice(%d) \n", ch);
                }
            }
        }
    }
    neteq_info.is_running = false;
    pthread_join(tid, &thread_ret);
	
    printf("%d s - Encoding finished \n", cur_time);
    
    for( std::vector<struct channel_info>::iterator it = ch_info_vec.begin(); it < ch_info_vec.end(); it++){
        
        // Close down the transport ( file writing )
        nw->DeRegisterExternalTransport(it->channel_id_);
    
        base->DeleteChannel(it->channel_id_);
        
        delete it->transport_;
    }

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
	if(rtp_rtcp){
		rtp_rtcp->Release();
		rtp_rtcp = NULL;
	}
    if(base){
        base->Terminate();
        base->Release();
        base = NULL;
    }
    
    webrtc::VoiceEngine::Delete(ve);

    fclose(ctrlFile);
    
    /* Decode the encoded files */
    webrtc::AudioFrame audioframe;
    int32_t sample_rate_hz = 48000;
    
    for( std::vector<struct channel_info>::iterator it = ch_info_vec.begin(); it < ch_info_vec.end(); it++){
        
        std::string outFile = it->file_name_;
        outFile.erase(outFile.size()-4,4);
        outFile.insert(outFile.size(),".pcm");

        std::string rtpStatFile = it->file_name_;
        rtpStatFile.erase(rtpStatFile.size()-4,4);
        rtpStatFile.insert(rtpStatFile.size(),".txt");
        
        int32_t rcv_time_ms, time_ms = 0;
        int ret;
        FILE* fp = fopen(it->file_name_.c_str(),"rb");
        FILE* out_file = fopen(outFile.c_str(),"wb");
        FILE* rtp_stats_file = fopen(rtpStatFile.c_str(),"wt");
        
		std::unique_ptr<webrtc::AudioCodingModule> acm(webrtc::AudioCodingModule::Create(0));
		
        c.pltype = it->pltype;
        ret = acm->RegisterReceiveCodec(c);
        if( ret < 0 ){
            printf("acm->RegisterReceiveCodec returned %d \n", ret);
        }
        
        webrtc::WebRtcRTPHeader rtp_info;
        size_t bytesIn;
        uint8_t RTPpacketBuf[MAX_PACKET_SIZE_BYTES];
        
        fread(&rcv_time_ms, sizeof(int32_t), 1, fp);
        fread(&bytesIn, sizeof(size_t), 1, fp);
        fread(RTPpacketBuf, sizeof(uint8_t), bytesIn, fp);
        
        int totLength = 0;
        uint32_t prev_timestamp = -1;
        while(1){
            // Put the packet in the Jb at the right time
            if(rcv_time_ms <= time_ms){
                GetRTPheader( RTPpacketBuf,
                              &rtp_info.header.markerBit,
                              &rtp_info.header.payloadType,
                              &rtp_info.header.sequenceNumber,
                              &rtp_info.header.timestamp,
                              &rtp_info.header.ssrc);
                
                rtp_info.type.Audio.isCNG = false;
                rtp_info.type.Audio.channel = 1;
                
                // arrival,length,payloadtype,seq,timestamp,
                fprintf(rtp_stats_file,"%d %d %d %d %d \n", rcv_time_ms, (int)bytesIn - RTP_HEADER_IN_BYTES,
                        rtp_info.header.payloadType, rtp_info.header.sequenceNumber, rtp_info.header.timestamp);
                
                // Check the time stamp difference
                if(prev_timestamp != -1){
                    int64_t timestamp_diff = rtp_info.header.timestamp - prev_timestamp;
                
                    if((timestamp_diff != 20 * 48) &&
                       (timestamp_diff != 40 * 48) &&
                       (timestamp_diff != 60 * 48)
                       ){
                        printf("Wrong timestamp_diff = %lld \n", timestamp_diff);
                    }
                }
                prev_timestamp = rtp_info.header.timestamp;
                
                ret = acm->IncomingPacket( RTPpacketBuf + RTP_HEADER_IN_BYTES,
                                          bytesIn - RTP_HEADER_IN_BYTES,
                                          rtp_info );
                if( ret < 0 ){
                    printf("acm->IncomingPacket returned %d \n", ret);
                }
                
                // Get the next packet
                size_t read = fread(&rcv_time_ms, sizeof(int32_t), 1, fp);
                if(read <= 0){
                    break;
                }
                fread(&bytesIn, sizeof(size_t), 1, fp);
                fread(RTPpacketBuf, sizeof(uint8_t), bytesIn, fp);
            }
            
            // write a frame every 10 ms
            if( time_ms % 10 == 0 ){
                ret = acm->PlayoutData10Ms(sample_rate_hz, &audioframe);
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
                totLength += audioframe.samples_per_channel_;
                
            }
            
            time_ms += 1;
        }
        
        printf("NetEQ produced %d samples !! \n", totLength);
        
        fclose(fp);
        fclose(out_file);
        fclose(rtp_stats_file);        
    }
    return 0;
}
