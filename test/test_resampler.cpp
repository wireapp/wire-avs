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

#include "gtest/gtest.h"
#include "complexity_check.h"

#define MAX_SAMPLE_RATE 48000

using webrtc::PushResampler;

static int resampler_unit_test(
                         const char* in_file_name,
                         const char* out_file_name,
                         int in_sample_rate,
                         int out_sample_rate,
                         float* cpu_load)
{
    int num_channels = 1;
    
    FILE *in_file, *out_file;
    
    int16_t input_buffer[ MAX_SAMPLE_RATE/100 ];
    int16_t output_buffer[ MAX_SAMPLE_RATE/100 ];
    struct timeval now, startTime, res, tmp, totTime;
    
    timerclear(&totTime);
    
    PushResampler<int16_t> resampler;
    
    in_file = fopen(in_file_name,"rb");
    if( in_file == NULL ){
        printf("Could not open file for reading \n");
        return -1;
    }
    out_file = fopen(out_file_name,"wb");
    if( out_file == NULL ){
        printf("Could not open file for writing \n");
        fclose(in_file);
        return -1;
    }
    
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
    *cpu_load = 100*ms_tot/(float)(10*num_frames);
        
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}

TEST(resampler, 16000_to_44100)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/near16.pcm",
                            "./test/data/out.pcm",
                            16000,
                            44100,
                            &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 44100_to_16000)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/out.pcm",
                                  "./test/data/out2.pcm",
                                  44100,
                                  16000,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 32000_to_44100)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/near32.pcm",
                                  "./test/data/out.pcm",
                                  32000,
                                  44100,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 44100_to_32000)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/out.pcm",
                                  "./test/data/out2.pcm",
                                  32000,
                                  44100,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 32000_to_48000)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/near32.pcm",
                                  "./test/data/out.pcm",
                                  32000,
                                  48000,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 48000_to_32000)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/out.pcm",
                                  "./test/data/out2.pcm",
                                  48000,
                                  32000,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 48000_to_44100)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/out.pcm",
                                  "./test/data/out2.pcm",
                                  48000,
                                  44100,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(resampler, 44100_to_48000)
{
    float cpu_load;
    
    int ret = resampler_unit_test("./test/data/out2.pcm",
                                  "./test/data/out.pcm",
                                  44100,
                                  48000,
                                  &cpu_load);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}