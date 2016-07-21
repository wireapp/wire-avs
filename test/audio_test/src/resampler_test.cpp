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
#include <string>
#ifdef WEBRTC_ANDROID
#include <sys/stat.h>
#endif
#include <sys/time.h>
#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include <stdlib.h>

#define MAX_SAMPLE_RATE 48000

using webrtc::PushResampler;

#if TARGET_OS_IPHONE
int resampler_test(int argc, char *argv[], const char *path)
#else
int main(int argc, char *argv[])
#endif
{
    std::string in_file_name;
    std::string out_file_name;
    int args;
    int num_channels = 1;
    int in_sample_rate = 16000;
    int out_sample_rate = 16000;
    
    FILE *in_file, *out_file;
    
    int16_t input_buffer[ MAX_SAMPLE_RATE/100 ];
    int16_t output_buffer[ MAX_SAMPLE_RATE/100 ];
    struct timeval now, startTime, res, tmp, totTime;
    
    timerclear(&totTime);
    
    PushResampler<int16_t> resampler;
    
    // Handle Command Line inputs
    args = 0;
    while(args < argc){
        if (strcmp(argv[args], "-in")==0){
            args++;
#if TARGET_OS_IPHONE
            in_file_name.insert(0,path);
            in_file_name.insert(in_file_name.length(),"/");
            in_file_name.insert(in_file_name.length(),argv[args]);
#else
            in_file_name.insert(0,argv[args]);
#endif
        } else if (strcmp(argv[args], "-out")==0){
            args++;
#if TARGET_OS_IPHONE
            out_file_name.insert(0,path);
            out_file_name.insert(out_file_name.length(),"/");
            out_file_name.insert(out_file_name.length(),argv[args]);
#else
            out_file_name.insert(0,argv[args]);
#endif
        } else if (strcmp(argv[args], "-fs_in")==0){
            args++;
            in_sample_rate = atol(argv[args]);
        } else if (strcmp(argv[args], "-fs_out")==0){
            args++;
            out_sample_rate = atol(argv[args]);
        }
        args++;
    }
    
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
    
    
    printf("\n------------------------------------------ \n");
    printf("Start Resampler Processing module test \n");
    printf("Input %d Hz Output %d Hz\n", in_sample_rate, out_sample_rate);
    
    if (resampler.InitializeIfNeeded(in_sample_rate,
                                      out_sample_rate,
                                      num_channels) != 0) {
        printf("InitializeIfNeeded failed");
        return -1;
    }
    
    int samples_per_channel = in_sample_rate/100; // 10 ms
    
    size_t size = samples_per_channel * num_channels;
    size_t read_count;
    int num_frames = 0;
    while(1){
        read_count = fread(input_buffer,
                           sizeof(int16_t),
                           samples_per_channel,
                           in_file);
    
    
        gettimeofday(&startTime, NULL);
        
        int out_length = resampler.Resample(input_buffer,
                                             samples_per_channel * num_channels,
                                             output_buffer,
                                             MAX_SAMPLE_RATE/100);
        
        gettimeofday(&now, NULL);
        timersub(&now, &startTime, &res);
        memcpy( &tmp, &totTime, sizeof(struct timeval));
        timeradd(&res, &tmp, &totTime);
        
        size_t write_count = fwrite(output_buffer,
                                    sizeof(int16_t),
                                    out_length,
                                    out_file);
        
        num_frames++;
        
        if(read_count < size){
            break;
        }
    }
    
    float ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    
    printf( "%d ms audio took %.3f ms %.3f %% of real time \n", 10*num_frames, ms_tot, 100*ms_tot/(float)(10*num_frames));

    printf("------------------------------------------ \n");
    
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}
