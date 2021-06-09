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
#include <pthread.h>

#include "webrtc/modules/audio_coding/main/interface/audio_coding_module.h"
#include "webrtc/system_wrappers/interface/scoped_ptr.h"
#include "webrtc/system_wrappers/interface/trace.h"

#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_conf_control.h"
#include "webrtc/voice_engine/include/voe_errors.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"

namespace voe_dec_test {

/**********************************/
/* RTP help functions             */
/**********************************/

#define RTP_HEADER_IN_BYTES 12
#define MAX_PACKET_SIZE_BYTES 1000
#define NUM_SECONDS 10
#define NUMBER_OF_PACKETS 50*NUM_SECONDS
#define PACKET_SIZE_MS 20

static int MakeRTPheader( uint8_t* rtpHeader,
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
class TransportCallBack : public webrtc::AudioPacketizationCallback {

public:
    TransportCallBack( uint8_t *buf, int length ){
        seqNo_ = 0;
        ssrc_ = 0;
        totBytes_ = 0;
        packetLenBytes_ = 0;
        timestamp_offset_ = rand();
        buf_ = buf;
        buf_size_ = length;
        gettimeofday(&startTime_, NULL);
    }
        
    int32_t SendData(
                    webrtc::FrameType     frame_type,
                    uint8_t       payload_type,
                    uint32_t      timestamp,
                    const uint8_t* payload_data,
                    size_t      payload_len_bytes,
                    const webrtc::RTPFragmentationHeader* fragmentation){

        if((buf_size_ - payload_len_bytes - RTP_HEADER_IN_BYTES - sizeof(int32_t)) <= 0){
            int32_t ms = -1;
            
            memcpy(buf_,&ms, sizeof(int32_t));
            packetLenBytes_ = 0;
        } else {
            struct timeval now, res;
            gettimeofday(&now, NULL);
            timersub(&now, &startTime_, &res);
            int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;

            memcpy(buf_,&ms, sizeof(int32_t));
            buf_ += sizeof(int32_t);
            buf_size_ -= sizeof(int32_t);
            
            int32_t numBytes = payload_len_bytes + RTP_HEADER_IN_BYTES;
            memcpy(buf_,&numBytes, sizeof(int32_t));
            buf_ += sizeof(int32_t);
            buf_size_ -= sizeof(int32_t);
            
            timestamp = timestamp + timestamp_offset_;
            
            int headerLenBytes = MakeRTPheader(buf_,
                                       payload_type,
                                       seqNo_++,
                                       timestamp,
                                       ssrc_);
            
            buf_ += headerLenBytes;
            buf_size_ -= headerLenBytes;
            
            memcpy(buf_, payload_data, payload_len_bytes * sizeof(uint8_t));

            buf_ +=payload_len_bytes;
            buf_size_ -= payload_len_bytes;
            
            packetLenBytes_ = (uint32_t)payload_len_bytes + (uint32_t)headerLenBytes;
            
            totBytes_ += payload_len_bytes;
        }
        
        return (int)packetLenBytes_;
    }
        
    int32_t get_total_bytes(){
        return(totBytes_);
    }
    
private:
    uint32_t ssrc_;
    uint32_t seqNo_;
    uint32_t totBytes_;
    uint32_t packetLenBytes_;
    uint32_t timestamp_offset_;
    uint8_t  *buf_;
    int      buf_size_;
    struct timeval startTime_;
};

class VoETransport : public webrtc::Transport {
public:
    VoETransport(){
    };
    virtual ~VoETransport() {
    };
    
    virtual int SendPacket(int channel, const void *data, size_t len) {
        return (int)len;
    };
    
    virtual int SendRTCPPacket(int channel, const void *data, size_t len){
        return (int)len;
    };
    
    void deregister()
    {
    }
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

class MyObserver : public webrtc::VoiceEngineObserver {
public:
    virtual void CallbackOnError(int channel, int err_code);
};
void MyObserver::CallbackOnError(int channel, int err_code)
{
    std::string msg;
    
    // Add printf for other error codes here
    if (err_code == VE_RECEIVE_PACKET_TIMEOUT) {
        msg = "VE_RECEIVE_PACKET_TIMEOUT\n";
    } else if (err_code == VE_PACKET_RECEIPT_RESTARTED) {
        msg = "VE_PACKET_RECEIPT_RESTARTED\n";
    } else if (err_code == VE_RUNTIME_PLAY_WARNING) {
        msg = "VE_RUNTIME_PLAY_WARNING\n";
    } else if (err_code == VE_RUNTIME_REC_WARNING) {
        msg = "VE_RUNTIME_REC_WARNING\n";
    } else if (err_code == VE_SATURATION_WARNING) {
        msg = "VE_SATURATION_WARNING\n";
    } else if (err_code == VE_RUNTIME_PLAY_ERROR) {
        msg = "VE_RUNTIME_PLAY_ERROR\n";
    } else if (err_code == VE_RUNTIME_REC_ERROR) {
        msg = "VE_RUNTIME_REC_ERROR\n";
    } else if (err_code == VE_REC_DEVICE_REMOVED) {
        msg = "VE_REC_DEVICE_REMOVED\n";
    }
    printf("CallbackOnError msg = %s \n", msg.c_str());
}

static MyObserver my_observer;

static void *reset_audio_device_thread(void *arg);

static void *neteq_monitor_thread_impl(void *arg);

struct neteq_monitor_info {
    webrtc::VoENetEqStats *neteq_stats_;
    int channel_id_;
};

struct reset_audiodevice_info {
    webrtc::VoECodec *codec_;
    webrtc::VoEHardware *hw_;
    int channel_id_;
};
    
#if TARGET_OS_IPHONE
int voe_dec_test(int argc, char *argv[], const char *path)
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
    webrtc::VoEConfControl *conferencing = webrtc::VoEConfControl::GetInterface(ve);
    if (!conferencing) {
        printf("VoEConfControl::GetInterface failed \n");
    }
    webrtc::VoEHardware *hw = webrtc::VoEHardware::GetInterface(ve);
    if (!hw) {
        printf("VoEHardware::GetInterface failed \n");
    }
    webrtc::VoENetEqStats *neteq_stats = webrtc::VoENetEqStats::GetInterface(ve);
    if (!neteq_stats) {
        printf("VoEHardware::GetInterface failed \n");
    }
    
    int ret, i, num_frames = 0;
    size_t read_count;

    webrtc::AudioFrame audioframe;
    
    std::string in_file_name;
#if TARGET_OS_IPHONE
    in_file_name.insert(0,path);
    in_file_name.insert(in_file_name.length(),"/far32.pcm");
#else
    // To ensure it will find the sound files, set the working directory to $PROJECT_DIR.
    // This setting is under Option-click on the Run button, select "Run / Debug" on the left,
    // select the Option tab, and pick "Use custom working directory".
    printf("Current working dir: %s\n", getcwd(NULL, 0));
    in_file_name.insert(0,"../../../test/audio_test/files/far32.pcm");
#endif
    
    webrtc::CodecInst c;
    
    int numberOfCodecs = codec->NumOfCodecs();
    bool codec_found = false;
    for( int i = 0; i < numberOfCodecs; i++ ){
        ret = codec->GetCodec( i, c);
        if(strcmp(c.plname,"opus") == 0){
            codec_found = true;
            break;
        }
    }
    c.rate = 40000;
    c.channels = 1;
    c.pacsize = (c.plfreq * PACKET_SIZE_MS) / 1000;

    /* Encode audio files to RTP stream */
    webrtc::scoped_ptr<webrtc::AudioCodingModule> acm(webrtc::AudioCodingModule::Create(0));
        
    ret = acm->RegisterSendCodec(c);
    if( ret < 0 ){
        printf("acm->SetSendCodec returned %d \n", ret);
    }
        
    char buf[10];
    sprintf(buf,"%d",i);
    std::string rtp_file_name;
#if TARGET_OS_IPHONE
    rtp_file_name.insert(0,path);
    rtp_file_name.insert(rtp_file_name.length(),"/rtp_ch#");
#else
    rtp_file_name = "rtp_ch#";
#endif
    rtp_file_name.insert(rtp_file_name.size(),buf);
    rtp_file_name.insert(rtp_file_name.size(),".dat");
    
    uint8_t *rtp_packet_buf = (uint8_t*)calloc(NUMBER_OF_PACKETS*MAX_PACKET_SIZE_BYTES,sizeof(uint8_t));
    
    TransportCallBack tCB( rtp_packet_buf, NUMBER_OF_PACKETS*MAX_PACKET_SIZE_BYTES );
    
    if(acm->RegisterTransportCallback((webrtc::AudioPacketizationCallback*) &tCB) < 0){
        printf("Register Transport Callback failed \n");
    }
        
    FILE *in_file = fopen(in_file_name.c_str(),"rb");
    if(in_file == NULL){
        printf("Cannot open %s for reading \n", in_file_name.c_str());
    }
        
    printf("Encoding file %s \n", in_file_name.c_str());
        
    audioframe.sample_rate_hz_ = 32000;
    audioframe.num_channels_ = 1;
    audioframe.samples_per_channel_ = audioframe.sample_rate_hz_/100;
    size_t size = audioframe.samples_per_channel_ * audioframe.num_channels_;
    for(num_frames = 0; num_frames < 3000; num_frames++ ) {
        read_count = fread(audioframe.data_,
                            sizeof(int16_t),
                            size,
                            in_file);
            
        if(read_count < size){
            break;
        }
        
        audioframe.timestamp_ = num_frames * audioframe.samples_per_channel_;
        
        ret = acm->Add10MsData(audioframe);
        if( ret < 0 ){
            printf("acm->Add10msData returned %d \n", ret);
        }
        ret = acm->Process();
        if( ret < 0 ){
            printf("acm->Process returned %d \n", ret);
        }
    }
    fclose(in_file);
    
    //webrtc::Trace::CreateTrace();
    //webrtc::Trace::SetTraceCallback(&logCb);
    //webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical | webrtc::kTracePersist);
    
    base->Init();
    
    base->RegisterVoiceEngineObserver(my_observer);
        
    int32_t rcv_time_ms, next_ms = PACKET_SIZE_MS;
    uint32_t bytesIn;
    
    pthread_t reset_thread = NULL;
    pthread_t neteq_monitor_thread = NULL;
    
    int channel_id = base->CreateChannel();
    VoETransport* transport = new VoETransport();
    nw->RegisterExternalTransport(channel_id, *transport);
    base->StartReceive(channel_id);
    base->StartPlayout(channel_id);
    base->StartSend(channel_id);
    
    struct neteq_monitor_info nw_qual_data;
    nw_qual_data.neteq_stats_ = neteq_stats;
    nw_qual_data.channel_id_ = channel_id;

    struct reset_audiodevice_info ad_info;
    ad_info.codec_ = codec;
    ad_info.hw_ = hw;
    nw_qual_data.channel_id_ = channel_id;
    
    pthread_create(&neteq_monitor_thread, NULL, neteq_monitor_thread_impl, &nw_qual_data);
    
    struct timeval now, res, start_time;
    gettimeofday(&start_time, NULL);
    // Setup the wanted Channels
    uint8_t *buf_ptr = rtp_packet_buf;
    while(1){
        /* Read a packet */
        memcpy(&rcv_time_ms, buf_ptr, sizeof(int32_t));
        //printf("rcv_time_ms = %d \n", rcv_time_ms);
        if(rcv_time_ms <= 0 ){
            break;
        }
        buf_ptr += sizeof(int32_t);
        
        memcpy(&bytesIn, buf_ptr, sizeof(int32_t));
        buf_ptr += sizeof(int32_t);
        
        nw->ReceivedRTPPacket(channel_id, (const void*)buf_ptr, bytesIn);
        buf_ptr += bytesIn;
        
        /* Restart the AudioDevice but do it on another thread to not block this one */
        if(next_ms == 8000){
            pthread_create(&reset_thread, NULL, reset_audio_device_thread, &ad_info);
        }
            
        gettimeofday(&now, NULL);
        timersub(&now, &start_time, &res);
        int32_t now_ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
        int32_t sleep_ms = next_ms - now_ms;
        if(sleep_ms < 0){
            printf("Time: %d ms Warning sleep_ms = %d not reading fast enough !! \n", now, sleep_ms);
            sleep_ms = 0;
        }
        next_ms += PACKET_SIZE_MS;
            
        timespec t;
        t.tv_sec = 0;
        t.tv_nsec = sleep_ms*1000*1000;
        nanosleep(&t, NULL);
    }
    
    //nw_qual_data.neteq_stats_ = NULL;
    pthread_join(neteq_monitor_thread, NULL);
    
    base->StopReceive(channel_id);
    base->StopPlayout(channel_id);
    base->StopSend(channel_id);
    
    if(neteq_stats){
        neteq_stats->Release();
        neteq_stats = NULL;
    }
    if(hw){
        hw->Release();
        hw = NULL;
    }
    if(codec){
        codec->Release();
        codec = NULL;
    }
    if(nw){
        nw->Release();
        nw = NULL;
    }
    if(conferencing){
        conferencing->Release();
        conferencing = NULL;
    }
    if (base) {
        base->Terminate();
        base->Release();
        base = NULL;
    }
    
    delete transport;
    
    webrtc::VoiceEngine::Delete(ve);
    
    printf("voe_conf_test_dec finished !! \n");
    
    return 0;
}

static void *reset_audio_device_thread(void *arg){
    struct reset_audiodevice_info *ad_info = (reset_audiodevice_info *)arg;
    webrtc::VoECodec *codec = ad_info->codec_;
    webrtc::VoEHardware *hw = ad_info->hw_;
    int channel_id = ad_info->channel_id_;
    
    struct timeval now, res, start_time;
    gettimeofday(&start_time, NULL);
    
    //printf("Resetting AudioDevice from other thread thread_id = %d !! \n", pthread_mach_thread_np(pthread_self()));
    printf("Reset AudioDevice() \n");
    
    //hw->ResetAudioDevice();
    
    //codec->NetEqFlushBuffers(channel_id);
    
    gettimeofday(&now, NULL);
    timersub(&now, &start_time, &res);
    int32_t now_ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
    printf("resetting AudioDevice took %d ms \n", now_ms);
    
    return NULL;
}

static void *neteq_monitor_thread_impl(void *arg){
    struct neteq_monitor_info *nw_qual_data = (neteq_monitor_info *)arg;

    webrtc::VoENetEqStats *neteq_stats = nw_qual_data->neteq_stats_;
    int channel_id;
    webrtc::NetworkStatistics netstat;
    
    while(1){
        channel_id = nw_qual_data->channel_id_;
        if(channel_id < 0){
            break;
        }
        neteq_stats->GetNetworkStatistics(channel_id, netstat);
        printf("ch# %d BufferSize = %d ms \n", channel_id, netstat.currentBufferSize);
        
        // Sleep 1 second
        timespec t;
        t.tv_sec = 1;
        t.tv_nsec = 0;
        nanosleep(&t, NULL);    
    }
    return NULL;
}

}