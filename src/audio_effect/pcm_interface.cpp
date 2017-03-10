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

#include <re.h>
#include "avs_audio_effect.h"

#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include "webrtc/modules/include/module_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

#define LOG2_CIRC_BUF_SZ 14
#define CIRC_BUF_MASK ((1 << LOG2_CIRC_BUF_SZ) -1)

#define FS_PROC 32000

static int get_number_of_frames(const char* pcmIn, int frame_length)
{
    FILE *in_file;
    in_file = fopen(pcmIn,"rb");
    if( in_file == NULL ){
        error("Could not open file for reading \n");
        return -1;
    }
    
    size_t count;
    int n_frames = 0;
    int16_t bufIn[frame_length];
    while(1){
        count = fread(bufIn,
                      sizeof(int16_t),
                      frame_length,
                      in_file);
        
        if(count < frame_length){
            break;
        }
        n_frames++;
    }
    fclose(in_file);
    
    return n_frames;
}

static int reverse_stream(const char* pcmIn,
                      const char* pcmOut,
                      int fs_hz)
{
    FILE *in_file, *out_file;
    int L = fs_hz/100;
    int16_t bufIn[L], bufOut[L];
    
    int n_frames = get_number_of_frames(pcmIn, L);
    
    in_file = fopen(pcmIn,"rb");
    if( in_file == NULL ){
        error("Could not open file for reading \n");
        return -1;
    }
    out_file = fopen(pcmOut,"wb");
    if( out_file == NULL ){
        error("Could not open file for writing \n");
        fclose(in_file);
        return -1;
    }
    size_t count;
    int num_samples = L * n_frames;
    fseek(in_file, num_samples*sizeof(int16_t), SEEK_CUR);
    for( int i = 0; i < n_frames; i++){
        fseek(in_file, -L*sizeof(int16_t), SEEK_CUR);
        
        count = fread(bufIn, sizeof(int16_t), L, in_file);

        for( int j = 0; j < L; j++){
            bufOut[j] = bufIn[L - j - 1];
        }
        
        count = fwrite(bufOut, sizeof(int16_t), L, out_file);
        
        fseek(in_file, -L*sizeof(int16_t), SEEK_CUR);
    }
    fseek(in_file, 0, SEEK_SET);
    for( int i = 0; i < n_frames; i++){
        count = fread(bufIn, sizeof(int16_t), L, in_file);
        count = fwrite(bufIn, sizeof(int16_t), L, out_file);
    }
    
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}

int apply_effect_to_pcm(const char* pcmIn,
                        const char* pcmOut,
                        int fs_hz,
                        enum audio_effect effect_type,
                        bool reduce_noise,                        
                        effect_progress_h* progress_h,
                        void *arg)
{
    if(effect_type == AUDIO_EFFECT_REVERSE){
        /* Special handling for reverse effect */
        int ret = reverse_stream(pcmIn, pcmOut, fs_hz);
        
        if(progress_h){
            progress_h(100, arg);
        }
        return ret;
    }
    FILE *in_file, *out_file;
    
    webrtc::PushResampler<int16_t> input_resampler;
    webrtc::PushResampler<int16_t> output_resampler;
    std::unique_ptr<webrtc::AudioProcessing> apm(webrtc::AudioProcessing::Create());    
    
    struct aueffect *aue;
    int ret = aueffect_alloc(&aue, effect_type, FS_PROC);
    if(ret != 0){
        error("aueffect_alloc failed \n");
        fclose(in_file);
        fclose(out_file);
        return ret;
    }
    
    int L = fs_hz/100;
    int n_frames = get_number_of_frames(pcmIn, L);
    int L_proc = FS_PROC/100;
    int progress = 0;
    int rem = 0;
    int n_samp_out = 0;
    
    info("sample_rate = %d \n", fs_hz);
    
    in_file = fopen(pcmIn,"rb");
    if( in_file == NULL ){
        error("Could not open file for reading \n");
        return -1;
    }
    out_file = fopen(pcmOut,"wb");
    if( out_file == NULL ){
        error("Could not open file for writing \n");
        fclose(in_file);
        return -1;
    }
    
    input_resampler.InitializeIfNeeded(fs_hz, FS_PROC, 1);
    output_resampler.InitializeIfNeeded(FS_PROC, fs_hz, 1);
    
    // Setup Audio Buffer used by apm
    webrtc::AudioFrame near_frame;
    near_frame.samples_per_channel_ = L_proc;
    near_frame.num_channels_ = 1;
    near_frame.sample_rate_hz_ = FS_PROC;
    
    // Setup APM
    webrtc::AudioProcessing::ChannelLayout inLayout = webrtc::AudioProcessing::kMono;
    webrtc::AudioProcessing::ChannelLayout outLayout = webrtc::AudioProcessing::kMono;;
    webrtc::AudioProcessing::ChannelLayout reverseLayout = webrtc::AudioProcessing::kMono;;
    apm->Initialize( FS_PROC, FS_PROC, FS_PROC, inLayout, outLayout, reverseLayout );
    
    // Enable High Pass Filter
    apm->high_pass_filter()->Enable(true);
    
    // Enable Noise Supression
    if(reduce_noise){
        apm->noise_suppression()->Enable(true);
        if(effect_type == AUDIO_EFFECT_VOCODER_MED){
            apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kModerate);
        } else {
            apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kLow);
        }
    }
    
    int16_t circ_buf[(1 << LOG2_CIRC_BUF_SZ)];
    int write_idx = 0;
    int read_idx = 0;
    
    int16_t bufIn[L], procOut[L_proc];
    //int16_t procIn[L_proc], procOut[L_proc];
    size_t count;
    for(int i = 0; i < n_frames; i++){
        count = fread(bufIn,
                      sizeof(int16_t),
                      L,
                      in_file);
        
        if(count < L){
            break;
        }
        if((i % 100) == 0){
            int progress = (i*100)/n_frames;
            if(progress_h){
                progress_h(progress, arg);
            }
        }
        
        input_resampler.Resample( bufIn, L, near_frame.data_, L_proc);
        
        ret = apm->ProcessStream(&near_frame);
        if( ret < 0 ){
            error("apm->ProcessStream returned %d \n", ret);
        }
        
        size_t L_proc_out;
        aueffect_process(aue, near_frame.data_, procOut, L_proc, &L_proc_out);
        
        //input_resampler.Resample( bufIn, L, procIn, L_proc);
        
        //size_t L_proc_out;
        //aueffect_process(aue, procIn, procOut, L_proc, &L_proc_out);
        
        for(int j = 0; j < L_proc_out; j++){
            circ_buf[write_idx] = procOut[j];
            write_idx = (write_idx + 1) & CIRC_BUF_MASK;
        }
        // resampler needs 10 ms chunks
        int buf_smpls = (write_idx - read_idx) & CIRC_BUF_MASK;
        while(buf_smpls >= L_proc){
            for(int j = 0; j < L_proc; j++){
                procOut[j] = circ_buf[read_idx];
                read_idx = (read_idx + 1) & CIRC_BUF_MASK;
            }
            output_resampler.Resample( procOut, L_proc, bufIn, L);
            
            count = fwrite(bufIn,
                           sizeof(int16_t),
                           L,
                           out_file);
            n_samp_out+=L;
            
            buf_smpls = (write_idx - read_idx) & CIRC_BUF_MASK;
        }
    }
    
    if(progress_h){
        progress_h(100, arg);
    }
    
    mem_deref(aue);
    
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}

