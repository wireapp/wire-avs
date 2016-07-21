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

void APM_unit_test(
  const char* near_file_name,
  const char* far_file_name,
  const char* near_file_name_out,
  const char* histogram_file_name,
  struct APMtestSetup* setup
);

using webrtc::AudioFrame;
using webrtc::AudioProcessing;
using webrtc::Config;
using webrtc::EchoCancellation;
using webrtc::GainControl;
using webrtc::NoiseSuppression;
using webrtc::VoiceDetection;

#if TARGET_OS_IPHONE
int apm_test(int argc, char *argv[], const char *path)
#else
int main(int argc, char *argv[])
#endif
{
    std::string near_in_file_name;
    std::string far_in_file_name;
    std::string out_folder_name;
    std::string FskHz = "16";
    struct APMtestSetup setup;
    int args;
    
    setup.sample_rate_hz = 16000;
    //setup.device_sample_rate_hz = 16000;
    
    // Handle Command Line inputs
    args = 0;
    while(args < argc){
        if (strcmp(argv[args], "-near")==0){
            args++;
#if TARGET_OS_IPHONE
            near_in_file_name.insert(0,path);
            near_in_file_name.insert(near_in_file_name.length(),"/");
            near_in_file_name.insert(near_in_file_name.length(),argv[args]);
#else
            near_in_file_name.insert(0,argv[args]);
#endif
        } else if (strcmp(argv[args], "-far")==0){
            args++;
#if TARGET_OS_IPHONE
            far_in_file_name.insert(0,path);
            far_in_file_name.insert(far_in_file_name.length(),"/");
            far_in_file_name.insert(far_in_file_name.length(),argv[args]);
#else
            far_in_file_name.insert(0,argv[args]);
#endif
        } else if (strcmp(argv[args], "-out")==0){
            args++;
#if TARGET_OS_IPHONE
            out_folder_name.insert(0,path);
            out_folder_name.insert(out_folder_name.length(),"/");
            out_folder_name.insert(out_folder_name.length(),argv[args]);
#else
            out_folder_name.insert(0,argv[args]);
#endif
        } else if (strcmp(argv[args], "-fs")==0){
            args++;
            setup.sample_rate_hz = atol(argv[args]);

            FskHz.erase(0, FskHz.length());
            FskHz.insert(0, argv[args]);
            FskHz.erase(2, FskHz.length());
        }
        args++;
    }
    
    printf("\n------------------------------------------ \n");
    printf("Start Audio Processing module test at %d kHz \n", setup.sample_rate_hz/1000);
    printf("------------------------------------------ \n\n");
    
    /* First test only HP */
    setup.enable_HP = true;
    setup.enable_AEC = false;
    setup.enable_AEC_DelayAgnostic = false;
    setup.enable_AEC_ExtendedFilter = false;
    setup.enable_AECm = false;
    setup.enable_NR = false;
    setup.enable_AGC = false;
    setup.ns_level = NoiseSuppression::kHigh;
    
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz + "_out_1.pcm";
        
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                     NULL,
                     &setup);
    }
    
    /* HP and NR */
    setup.enable_NR = true;
    
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz + "_out_2.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                     NULL,
                     &setup);
    }
    /* HP and mAEC */
    setup.enable_NR = false;
    setup.enable_AECm = true;
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz +  "_out_3.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                     NULL,
                     &setup);
    }
    /* HP and AEC */
    setup.enable_AECm = false;
    setup.enable_AEC = true;
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz +  "_out_4.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                     NULL,
                     &setup);
    }

    /* Everything but AEC */
    setup.enable_AECm = true;
    setup.enable_AEC = false;
    setup.enable_NR = true;
    setup.enable_AGC = true;
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz + "_out_5.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                      far_in_file_name.c_str(),
                      near_out_file_name.c_str(),
                      NULL,
                      &setup);
    }
    
    /* Everything but mAEC */
    setup.enable_AECm = false;
    setup.enable_AEC = true;
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz + "_out_6.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                      NULL,
                     &setup);
    }
    
    // Enable AEC ectended mode
    setup.enable_AEC_DelayAgnostic = true;
    {
        const std::string near_out_file_name = out_folder_name + "near" + FskHz + "_out_7.pcm";
        
        APM_unit_test(near_in_file_name.c_str(),
                     far_in_file_name.c_str(),
                     near_out_file_name.c_str(),
                      NULL,
                     &setup);
    }
    printf("------------------------------------------ \n");
    printf("Audio Processing module test finished \n");
    printf("------------------------------------------ \n");
    
    return 0;
}

#define ProcTimeBins 100
#define ProcTimeMaxMs 1
#define ProcTimeDelta (ProcTimeMaxMs/(float)ProcTimeBins)
void APM_unit_test(
  const char* near_file_name,
  const char* far_file_name,
  const char* near_file_name_out,
  const char* histogram_file_name,
  struct APMtestSetup* setup)
{
  AudioProcessing* apm(AudioProcessing::Create());
    
  AudioFrame far_frame;
  AudioFrame near_frame;
    
  FILE *near_file, *far_file, *out_file, *histogram_file = NULL;
    
  int32_t sample_rate_hz = setup->sample_rate_hz;
  int32_t ProcTimeHist[ 100 ];
    
  int num_capture_input_channels = 1;
  int num_capture_output_channels = 1;
  int num_render_channels = 1;
  int samples_per_channel = sample_rate_hz / 100;
  int ret;
  size_t read_count;
  struct timeval now, startTime, res, tmp, totTime;
    
  timerclear(&totTime);
    
  memset(ProcTimeHist,0,sizeof(ProcTimeHist));
    
  far_file = fopen(far_file_name,"rb");
  if( far_file == NULL ){
    printf("Could not open file for reading \n");
    return;
  }
  near_file = fopen(near_file_name,"rb");
  if( near_file == NULL ){
    printf("Could not open file for reading \n");
    fclose(far_file);
    return;
  }

  out_file = fopen(near_file_name_out,"wb");
  if( out_file == NULL ){
    printf("Could not open file for writing \n");
    fclose(far_file);
    fclose(near_file);
    return;
  }

  if( histogram_file_name != NULL ){
    histogram_file = fopen(histogram_file_name,"wt");
    if( histogram_file == NULL ){
      printf("Could not open file for writing \n");
      fclose(out_file);
      fclose(far_file);
      fclose(near_file);
      return;
    }
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
      printf("apm->AnalyzeReverseStream returned %d \n", ret);
    }
      
    apm->echo_cancellation()->set_stream_drift_samples(0);
    apm->set_stream_delay_ms(0);
  
    ret = apm->ProcessStream(&near_frame);
    if( ret < 0 ){
      printf("apm->ProcessStream returned %d \n", ret);
    }
    gettimeofday(&now, NULL);
    timersub(&now, &startTime, &res);
    memcpy( &tmp, &totTime, sizeof(struct timeval));
    timeradd(&res, &tmp, &totTime);
      
    /* Histogram of execution times */
    float ms = (float)res.tv_sec*1000.0 + (float)res.tv_usec/1000.0;

    int idx = (int)(ms / (float)ProcTimeDelta);
    if( idx > (ProcTimeBins-1)){
      idx = ProcTimeBins-1;
    }
      
    ProcTimeHist[idx] = ProcTimeHist[idx] + 1;
      
    size_t write_count = fwrite(near_frame.data_,
                                sizeof(int16_t),
                                read_count,
                                out_file);
  
    num_frames++;
      
    if(read_count < size){
      break;
    }
  }
    
  /* Save Proc Time Histogram */
  if( histogram_file != NULL ){
    for( int i = 0 ; i < ProcTimeBins; i++){
      fprintf(histogram_file,"%d ", ProcTimeHist[i]);
    }
    fclose(histogram_file);
  }
    
  float ms_tot;
    
  ms_tot = (float)totTime.tv_sec*1000.0 + (float)totTime.tv_usec/1000.0;
    
  printf( "-- AP unit test finished processed %d frames \n", num_frames);
  printf( "HP enabled = %d \n", apm->high_pass_filter()->is_enabled());
  printf( "NS enabled = %d \n", apm->noise_suppression()->is_enabled());
  printf( "AEC enabled = %d \n", apm->echo_cancellation()->is_enabled());
  printf( "AEC delayAgnostic enabled = %d \n", setup->enable_AEC_DelayAgnostic);
  printf( "AECM enabled = %d \n", apm->echo_control_mobile()->is_enabled());
  printf( "AGC enabled = %d \n", apm->gain_control()->is_enabled());
  printf( "%d ms audio took %.3f ms %.1f %% of real time \n\n", 10*num_frames, ms_tot, 100*ms_tot/(float)(10*num_frames));
  fclose(far_file);
  fclose(near_file);
  fclose(out_file);
}
