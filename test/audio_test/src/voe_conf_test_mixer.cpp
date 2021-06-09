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

#include "webrtc/modules/audio_coding/main/interface/audio_coding_module.h"
#include "webrtc/system_wrappers/interface/scoped_ptr.h"
#include "webrtc/system_wrappers/interface/trace.h"

#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_conf_control.h"
#include "webrtc/voice_engine/include/voe_errors.h"

/**********************************/
/* RTP help functions             */
/**********************************/

#define RTP_HEADER_IN_BYTES 12
#define MAX_PACKET_SIZE_BYTES 1000

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
    TransportCallBack( std::string name ){
        seqNo_ = 0;
        ssrc_ = 0;
        totBytes_ = 0;
        timestamps_per_packet_ = 0;
        prev_timestamp_ = 0;
        packetLenBytes_ = 0;
        timestamp_offset_ = rand();
        fp_ = fopen(name.c_str(),"wb");
        gettimeofday(&startTime_, NULL);
    }
        
    int32_t SendData(
                    webrtc::FrameType     frame_type,
                    uint8_t       payload_type,
                    uint32_t      timestamp,
                    const uint8_t* payload_data,
                    size_t      payload_len_bytes,
                    const webrtc::RTPFragmentationHeader* fragmentation){
            
        int headerLenBytes;
        
        timestamp = timestamp + timestamp_offset_;
            
        headerLenBytes = MakeRTPheader(packet_,
                                       payload_type,
                                       seqNo_++,
                                       timestamp,
                                       ssrc_);
            
        if( payload_len_bytes < MAX_PACKET_SIZE_BYTES ){
            memcpy(&packet_[headerLenBytes], payload_data, payload_len_bytes * sizeof(uint8_t));
            packetLenBytes_ = (uint32_t)payload_len_bytes + (uint32_t)headerLenBytes;
        } else {
            packetLenBytes_ = 0;
        }
            
        if((prev_timestamp_ != 0) && (timestamps_per_packet_ == 0)){
            //printf("1. timestamp = %d \n", timestamp);
            timestamps_per_packet_ = timestamp - prev_timestamp_;
        }
        prev_timestamp_ = timestamp;
            
        struct timeval now, res;
        gettimeofday(&now, NULL);
        timersub(&now, &startTime_, &res);
        int32_t ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
            
        fwrite( &ms, sizeof(int32_t), 1, fp_);
        fwrite( &packetLenBytes_, sizeof(uint32_t), 1, fp_);
        fwrite( packet_, sizeof(uint8_t), packetLenBytes_, fp_);
        
        totBytes_ += payload_len_bytes;
        
        return (int)packetLenBytes_;
    }
        
    int32_t get_total_bytes(){
        return(totBytes_);
    }
        
    int32_t get_timestamps_per_packet(){
        return(timestamps_per_packet_);
    }
    
private:
    uint32_t ssrc_;
    uint32_t seqNo_;
    uint32_t totBytes_;
    uint16_t timestamps_per_packet_;
    uint32_t prev_timestamp_;
    uint32_t timestamp_offset_;
    uint8_t  packet_[RTP_HEADER_IN_BYTES + MAX_PACKET_SIZE_BYTES];
    uint32_t packetLenBytes_;
    FILE* fp_;
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

#if TARGET_OS_IPHONE
int voe_conf_test_dec(int argc, char *argv[], const char *path)
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
    
    int numChannels, i;
    int ret, num_frames = 0;
    size_t read_count;

    webrtc::AudioFrame audioframe;
    
    std::vector<std::string> rtp_files, in_files;
    
#if TARGET_OS_IPHONE
    {
        std::string file_name;
        file_name.insert(0,path);
        file_name.insert(file_name.length(),"/far32.pcm");
        in_files.push_back(file_name);
    }
    {
        std::string file_name;
        file_name.insert(0,path);
        file_name.insert(file_name.length(),"/testfile2_32kHz.pcm");
        in_files.push_back(file_name);
    }
    {
        std::string file_name;
        file_name.insert(0,path);
        file_name.insert(file_name.length(),"/testfile3_32kHz.pcm");
        in_files.push_back(file_name);
    }    
    {
        std::string file_name;
        file_name.insert(0,path);
        file_name.insert(file_name.length(),"/testfile32kHz.pcm");
        in_files.push_back(file_name);
    }
#else
    // To ensure it will find the sound files, set the working directory to $PROJECT_DIR.
    // This setting is under Option-click on the Run button, select "Run / Debug" on the left,
    // select the Option tab, and pick "Use custom working directory".
    printf("Current working dir: %s\n", getcwd(NULL, 0));
    in_files.push_back("../../../test/audio_test/files/far32.pcm");
    in_files.push_back("../../../test/audio_test/files/testfile2_32kHz.pcm");
    in_files.push_back("../../../test/audio_test/files/testfile3_32kHz.pcm");
    in_files.push_back("../../../test/audio_test/files/testfile32kHz.pcm");
#endif
    //in_files.push_back("../../../../../../test/audio_test/files/far32.pcm");
    numChannels = (int)in_files.size();
    
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

    /* Encode audio files to RTP stream files */
    for( i = 0; i < numChannels; i++){
        webrtc::scoped_ptr<webrtc::AudioCodingModule> acm(webrtc::AudioCodingModule::Create(0));
        
        int ret = acm->RegisterSendCodec(c);
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
        
        TransportCallBack tCB( rtp_file_name.c_str() );

        rtp_files.push_back(rtp_file_name);
        
        if(acm->RegisterTransportCallback((webrtc::AudioPacketizationCallback*) &tCB) < 0){
            printf("Register Transport Callback failed \n");
        }
        
        FILE *in_file = fopen(in_files[i].c_str(),"rb");
        if(in_file == NULL){
            printf("Cannot open %s for reading \n", in_files[i].c_str());
        }
        
        printf("Encoding file %s \n", in_files[i].c_str());
        
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
    }
    
    webrtc::Trace::CreateTrace();
    webrtc::Trace::SetTraceCallback(&logCb);
    webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical | webrtc::kTracePersist);
    
    base->Init();
    
    base->RegisterVoiceEngineObserver(my_observer);
    
    bool stereoAvailable;
    conferencing->SupportsStereo(stereoAvailable);
    if(stereoAvailable){
        printf("voe: stereo playout available \n");
    } else {
        printf("voe: stereo playout not available \n");
    }
    conferencing->SetUseStereoConf(stereoAvailable);
    
    struct channel_info {
        int channel_number_;
        int channel_id_;
        VoETransport*  transport_;
        FILE* fp_;
    };
    
    std::list<struct channel_info> ch_info_vec;
    std::list<int> conf_list;
    
    struct timeval now, res, start_time;
    gettimeofday(&start_time, NULL);
    
    int32_t rcv_time_ms, next_ms = PACKET_SIZE_MS;
    uint32_t bytesIn;
    uint8_t RTPpacketBuf[MAX_PACKET_SIZE_BYTES];
    size_t read = 0;
    
    // delays between adding/removing participants
    //int delays_ms[] = {2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500, 2500};  // even distribution
    //int delays_ms[] = {3000, 50, 50, 3000, 50, 50, 5000, 5000, 5000};  // add and remove 2nd and 3rd part very quickly
    int delays_ms[] = {100, 100, 100, 1400, 100, 100, 4000, 4000, 4000, 4000, 4000, 4000};  // add before notifications are played
    
    // Setup the wanted Channels
    i = 0;
    while(1){
        struct channel_info ch_info;
        
        if( i < numChannels ) {
            ch_info.channel_number_ = i;
            ch_info.channel_id_ = base->CreateChannel();
            ch_info.transport_ = new VoETransport();
            nw->RegisterExternalTransport(ch_info.channel_id_, *ch_info.transport_);
            ch_info.fp_ = fopen(rtp_files[i].c_str(),"rb");
            ch_info_vec.push_back(ch_info);
            base->StartReceive(ch_info.channel_id_);
            base->StartPlayout(ch_info.channel_id_);
            printf("\n--> Adding channel on the right side\n");
            conf_list.push_back(ch_info.channel_id_);
            conferencing->UpdateConference(conf_list);
        } else {
            ch_info = ch_info_vec.front();
            ch_info_vec.pop_front();
            nw->DeRegisterExternalTransport(ch_info.channel_id_);
            base->StopReceive(ch_info.channel_id_);
            base->StopPlayout(ch_info.channel_id_);
            base->DeleteChannel(ch_info.channel_id_);
            printf("\n--> Removing channel on the left side\n");
            conf_list.pop_front();
            conferencing->UpdateConference(conf_list);
        }
        
        for(int nPacket = 0; nPacket < delays_ms[i] / PACKET_SIZE_MS; nPacket++) {
            // Iterate over channels
            for( std::list<struct channel_info>::iterator it = ch_info_vec.begin(); it != ch_info_vec.end(); it++){
                /* Read a packet */
                read = fread(&rcv_time_ms, sizeof(int32_t), 1, it->fp_);
                if(read <= 0){
                    break;
                }
                fread(&bytesIn, sizeof(uint32_t), 1, it->fp_);
                fread(RTPpacketBuf, sizeof(uint8_t), bytesIn, it->fp_);
                
                nw->ReceivedRTPPacket(it->channel_id_, (const void*)RTPpacketBuf, bytesIn);
            }
            if(read <= 0){
                break;
            }

            gettimeofday(&now, NULL);
            timersub(&now, &start_time, &res);
            int32_t now_ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
            int32_t sleep_ms = next_ms - now_ms;
            if(sleep_ms < 0){
                printf("Warning sleep_ms = %d not reading fast enough !! \n", sleep_ms);
                sleep_ms = 0;
            }
            next_ms += PACKET_SIZE_MS;
            
            timespec t;
            t.tv_sec = 0;
            t.tv_nsec = sleep_ms*1000*1000;
            nanosleep(&t, NULL);
        }
        i++;
        if(read <= 0 || i == 2*numChannels){
            break;
        }
    }
    
    for( std::list<struct channel_info>::iterator it = ch_info_vec.begin(); it != ch_info_vec.end(); it++){
        
        // Close down the transport
        delete it->transport_;
        
        fclose(it->fp_);
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
    
    webrtc::VoiceEngine::Delete(ve);
    
    return 0;
}
