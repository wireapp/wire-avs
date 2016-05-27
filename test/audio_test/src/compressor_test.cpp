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
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef WEBRTC_ANDROID
#include <sys/stat.h>
#endif

#include <sys/time.h>

#include <algorithm>

#include "webrtc/common.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include "webrtc/modules/interface/module_common_types.h"
#include "webrtc/system_wrappers/interface/cpu_features_wrapper.h"
#include "webrtc/system_wrappers/interface/scoped_ptr.h"
#include "webrtc/system_wrappers/interface/tick_util.h"
#include "webrtc/test/testsupport/fileutils.h"
#include "webrtc/test/testsupport/perf_test.h"

using webrtc::AudioFrame;
using webrtc::AudioProcessing;
using webrtc::Config;
using webrtc::DelayCorrection;
using webrtc::EchoCancellation;
using webrtc::GainControl;
using webrtc::NoiseSuppression;
//using webrtc::scoped_array;
//using webrtc::scoped_ptr;
using webrtc::TickInterval;
using webrtc::TickTime;
using webrtc::VoiceDetection;

#if TARGET_OS_IPHONE
int apm_test(int argc, char *argv[], const char *path)
#else
int main(int argc, char *argv[])
#endif
{
    std::string in_file_name;
    std::string out_file_name;
    std::string FskHz = "16";
    int args;
    FILE *in_file, *out_file;

    int enable_limiter = false;
    int compression_gain_db = 0;
    int32_t sample_rate_hz = 16000;
    
    // Handle Command Line inputs
    args = 0;
    while(args < argc){
        if (strcmp(argv[args], "-in")==0){
            args++;
            in_file_name.insert(0,argv[args]);
        } else if (strcmp(argv[args], "-out")==0){
            args++;
            out_file_name.insert(0,argv[args]);
        } else if (strcmp(argv[args], "-enable_limiter")==0){
            enable_limiter = true;
        } else if (strcmp(argv[args], "-compression_gain_db")==0){
            args++;
            compression_gain_db = atol(argv[args]);
        } else if (strcmp(argv[args], "-fs")==0){
            args++;
            sample_rate_hz = atol(argv[args]);

            FskHz.erase(0, FskHz.length());
            FskHz.insert(0, argv[args]);
            FskHz.erase(2, FskHz.length());
        }
        args++;
    }
    
    printf("\n------------------------------------------ \n");
    printf("Start Audio Processing module test at %d kHz \n", sample_rate_hz/1000);
    printf("------------------------------------------ \n\n");
    
    AudioProcessing* apm(AudioProcessing::Create());
    
    AudioFrame far_frame;
    AudioFrame near_frame;
    
    int num_capture_input_channels = 1;
    int num_capture_output_channels = 1;
    int num_render_channels = 1;
    int samples_per_channel = sample_rate_hz / 100;
    int ret;
    size_t read_count;
        
    in_file = fopen(in_file_name.c_str(),"rb");
    if( in_file == NULL ){
        printf("Could not open file for reading \n");
        return -1;
    }
    out_file = fopen(out_file_name.c_str(),"wb");
    if( out_file == NULL ){
        printf("Could not open file for writing \n");
        fclose(in_file);
        return -1;
    }
    
    // Setup Audio Buffers
    far_frame.samples_per_channel_ = samples_per_channel;
    far_frame.num_channels_ = num_capture_input_channels;
    far_frame.sample_rate_hz_ = sample_rate_hz;
    near_frame.samples_per_channel_ = samples_per_channel;
    near_frame.num_channels_ = num_render_channels;
    near_frame.sample_rate_hz_ = sample_rate_hz;
    
    // Setup APM
    webrtc::AudioProcessing::ChannelLayout inLayout = webrtc::AudioProcessing::kMono;
    webrtc::AudioProcessing::ChannelLayout outLayout = webrtc::AudioProcessing::kMono;;
    webrtc::AudioProcessing::ChannelLayout reverseLayout = webrtc::AudioProcessing::kMono;;
    if(num_capture_input_channels == 2){
        inLayout = webrtc::AudioProcessing::kStereo;
    }
    if(num_capture_output_channels == 2){
        outLayout = webrtc::AudioProcessing::kStereo;
    }
    if(num_render_channels == 2){
        reverseLayout = webrtc::AudioProcessing::kStereo;
    }
    apm->Initialize( sample_rate_hz,
                    sample_rate_hz,
                    sample_rate_hz,
                    inLayout,
                    outLayout,
                    reverseLayout );
    
    // Enable High Pass Filter
    apm->high_pass_filter()->Enable(false);
    
    // Enable AGC
    apm->gain_control()->set_mode(GainControl::kFixedDigital);
    apm->gain_control()->Enable(true);
    
    apm->gain_control()->enable_limiter(enable_limiter);
    
    apm->gain_control()->set_compression_gain_db(compression_gain_db);
    
    size_t size = samples_per_channel * num_render_channels;
    memset(far_frame.data_, 0, size*sizeof(int16_t));
    int num_frames = 0;
    while(1){
        read_count = fread(near_frame.data_,
                           sizeof(int16_t),
                           size,
                           in_file);
        
        ret = apm->AnalyzeReverseStream(&far_frame);
        if( ret < 0 ){
            printf("apm->AnalyzeReverseStream returned %d \n", ret);
        }
        
        ret = apm->ProcessStream(&near_frame);
        if( ret < 0 ){
            printf("apm->ProcessStream returned %d \n", ret);
        }
        
        size_t write_count = fwrite(near_frame.data_,
                                    sizeof(int16_t),
                                    read_count,
                                    out_file);
        
        num_frames++;
        
        if(read_count < size){
            break;
        }
    }
    
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}
