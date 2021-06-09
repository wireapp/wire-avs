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
#include <stdlib.h>
#include <math.h>
#ifdef WEBRTC_ANDROID
#include <sys/stat.h>
#endif
#include <sys/time.h>
#include <algorithm>

#include "webrtc/common.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include "webrtc/modules/include/module_common_types.h"

#include "gtest/gtest.h"
#include "complexity_check.h"

struct APMtestSetup {
    bool enable_HP;
    bool enable_AEC;
    bool enable_AEC_ExtendedFilter;
	bool enable_AEC_DelayAgnostic;
    bool enable_AECm;
    bool enable_NR;
    bool enable_AGC;
    int  sample_rate_hz;
    int  device_sample_rate_hz;
    webrtc::NoiseSuppression::Level ns_level;
};

using webrtc::AudioFrame;
using webrtc::AudioProcessing;
using webrtc::Config;
using webrtc::EchoCancellation;
using webrtc::GainControl;
using webrtc::NoiseSuppression;
using webrtc::VoiceDetection;

static int APM_unit_test(
  const char* near_file_name,
  const char* far_file_name,
  const char* near_file_name_out,
  float* cpu_load,
  struct APMtestSetup* setup)
{
  std::unique_ptr<AudioProcessing> apm(AudioProcessing::Create());
    
  AudioFrame far_frame;
  AudioFrame near_frame;
    
  FILE *near_file, *far_file, *out_file;
    
  int32_t sample_rate_hz = setup->sample_rate_hz;
    
  int num_capture_input_channels = 1;
  int num_capture_output_channels = 1;
  int num_render_channels = 1;
  int samples_per_channel = sample_rate_hz / 100;
  int ret;
  size_t read_count;
  struct timeval now, startTime, res, tmp, totTime;
    
  timerclear(&totTime);
    
  far_file = fopen(far_file_name,"rb");
  if( far_file == NULL ){
    printf("Could not open file for reading \n");
    return -1;
  }
  near_file = fopen(near_file_name,"rb");
  if( near_file == NULL ){
    printf("Could not open file for reading \n");
    fclose(far_file);
    return -1;
  }

  out_file = fopen(near_file_name_out,"wb");
  if( out_file == NULL ){
    printf("Could not open file for writing \n");
    fclose(far_file);
    fclose(near_file);
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
  apm->high_pass_filter()->Enable(setup->enable_HP);
    
  // Enable Noise Supression
  apm->noise_suppression()->Enable(setup->enable_NR);
  apm->noise_suppression()->set_level(setup->ns_level);
    
  // Enable AEC
  apm->echo_cancellation()->Enable(setup->enable_AEC);
  apm->set_stream_delay_ms(0);
  
  apm->echo_cancellation()->set_stream_drift_samples(0);
  apm->echo_cancellation()->enable_drift_compensation(true);

  webrtc::Config config;
  config.Set<webrtc::ExtendedFilter>(new webrtc::ExtendedFilter(setup->enable_AEC_ExtendedFilter));
  config.Set<webrtc::DelayAgnostic>(new webrtc::DelayAgnostic(setup->enable_AEC_DelayAgnostic));
  apm->SetExtraOptions(config);
    
  // Enable AECm
  apm->echo_control_mobile()->Enable(setup->enable_AECm);
    
  // Enable AGC
  apm->gain_control()->set_mode(GainControl::kAdaptiveDigital);
  apm->gain_control()->Enable(setup->enable_AGC);

  // Enable level estimator ??
  //apm->level_estimator()->Enable(true);
    
  // Enable voice detection
  //apm->voice_detection()->Enable(true);
    
  size_t size = samples_per_channel * num_render_channels;
  int num_frames = 0;
  while(1){
    read_count = fread(far_frame.data_,
                     sizeof(int16_t),
                     size,
                     far_file);

    read_count = fread(near_frame.data_,
                     sizeof(int16_t),
                     read_count,
                     near_file);
    
    gettimeofday(&startTime, NULL);
    ret = apm->ProcessReverseStream(&far_frame);
    if( ret < 0 ){
      return -1;
    }
      
    apm->echo_cancellation()->set_stream_drift_samples(0);
    apm->set_stream_delay_ms(0);
  
    ret = apm->ProcessStream(&near_frame);
    if( ret < 0 ){
      return -1;
    }
    gettimeofday(&now, NULL);
    timersub(&now, &startTime, &res);
    memcpy( &tmp, &totTime, sizeof(struct timeval));
    timeradd(&res, &tmp, &totTime);
      
    size_t write_count = fwrite(near_frame.data_,
                                sizeof(int16_t),
                                read_count,
                                out_file);
  
    num_frames++;
      
    if(read_count < size){
      break;
    }
  }
    
  float ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
  *cpu_load = 100*ms_tot/(float)(10*num_frames);
    
  fclose(far_file);
  fclose(near_file);
  fclose(out_file);
  
  return 0;
}

TEST(apm, hp)
{
    APMtestSetup setup;
    float cpu_load;
    
    setup.enable_HP = true;
    setup.enable_AEC = false;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = false;
    setup.enable_NR = false;
    setup.enable_AGC = false;
    setup.ns_level = NoiseSuppression::kHigh;
    setup.sample_rate_hz = 32000;
    
    int ret = APM_unit_test("./test/data/near32.pcm",
                  "./test/data/far32.pcm",
                  "./test/data/near32_out.pcm",
                  &cpu_load,
                  &setup);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 1.0 );
}

TEST(apm, hp_nr)
{
    APMtestSetup setup;
    float cpu_load;
    
    setup.enable_HP = true;
    setup.enable_AEC = false;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = false;
    setup.enable_NR = true;
    setup.enable_AGC = false;
    setup.ns_level = NoiseSuppression::kHigh;
    setup.sample_rate_hz = 32000;
    
    int ret = APM_unit_test("./test/data/near32.pcm",
                             "./test/data/far32.pcm",
                             "./test/data/near32_out.pcm",
                            &cpu_load,
                            &setup);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 3.0 );
}

TEST(apm, hp_nr_aec)
{
    APMtestSetup setup;
    float cpu_load;
    
    setup.enable_HP = true;
    setup.enable_AEC = true;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = false;
    setup.enable_NR = true;
    setup.enable_AGC = false;
    setup.ns_level = NoiseSuppression::kHigh;
    setup.sample_rate_hz = 32000;
    
    int ret = APM_unit_test("./test/data/near32.pcm",
                            "./test/data/far32.pcm",
                            "./test/data/near32_out.pcm",
                            &cpu_load,
                            &setup);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 5.0 );
}

TEST(apm, hp_nr_aecm)
{
    APMtestSetup setup;
    float cpu_load;
    
    setup.enable_HP = true;
    setup.enable_AEC = false;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = true;
    setup.enable_NR = true;
    setup.enable_AGC = false;
    setup.ns_level = NoiseSuppression::kHigh;
    setup.sample_rate_hz = 16000;
    
    int ret = APM_unit_test("./test/data/near16.pcm",
                            "./test/data/far16.pcm",
                            "./test/data/near16_out.pcm",
                            &cpu_load,
                            &setup);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 4.0 );
}

TEST(apm, hp_nr_aecm_agc)
{
    APMtestSetup setup;
    float cpu_load;
    
    setup.enable_HP = true;
    setup.enable_AEC = false;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = true;
    setup.enable_NR = true;
    setup.enable_AGC = true;
    setup.ns_level = NoiseSuppression::kHigh;
    setup.sample_rate_hz = 16000;
    
    int ret = APM_unit_test("./test/data/near16.pcm",
                            "./test/data/far16.pcm",
                            "./test/data/near16_out.pcm",
                            &cpu_load,
                            &setup);
    
    ASSERT_EQ(0, ret);
    COMPLEXITY_CHECK( cpu_load, 5.0 );
}