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
#include "normalizer.h"
#include "avs_audio_effect.h"
#include <math.h>
#include <cstdlib>

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

void* create_normalizer(int fs_hz, int strength)
{
    struct normalizer_effect* ne = (struct normalizer_effect*)calloc(sizeof(struct normalizer_effect),1);
 
    ne->resampler = new webrtc::PushResampler<int16_t>;

    ne->fs_khz = fs_hz/1000;
    
    ne->resampler->InitializeIfNeeded(fs_hz, NE_VAD_FS_KHZ*1000, 1);
    
    silk_VAD_Init(&ne->silk_enc.sVAD);
    
    ne->Target_vol_db = NE_TARGET_LVL_DB;
    
    ne->gain = 1.0f;
 
    ne->squelch_enabled = false;
    
    return (void*)ne;
}

void reset_normalizer(void *st, int fs_hz)
{
    struct normalizer_effect *ne = (struct normalizer_effect*)st;
    
    ne->fs_khz = fs_hz/1000;
    
    ne->resampler->InitializeIfNeeded(fs_hz, NE_VAD_FS_KHZ*1000, 1);
    
    silk_VAD_Init(&ne->silk_enc.sVAD);
    
    memset(ne->vad_buf, 0, sizeof(ne->vad_buf));
    memset(ne->nrg_buf, 0, sizeof(ne->nrg_buf));
    memset(ne->sig_buf, 0, sizeof(ne->sig_buf));
    
    ne->speechLvl = 0.0f;
    ne->maxLvl = 0.0f;
    
    ne->gain = 1.0f;
}

void free_normalizer(void *st)
{
    struct normalizer_effect *ne = (struct normalizer_effect*)st;
    
    delete ne->resampler;
    
    free(ne);
}

static void apply_gain(struct normalizer_effect *ne,  int16_t in[], int16_t out[], int L)
{
    int L_sub = ne->fs_khz;
    int N = L / L_sub;
    float tot_gain_db = ne->Target_gain_db - ne->Squelch_gain_db;
    float target_gain = pow(10,(float)tot_gain_db/20.0f);
    float alpha, used_target_gain;
    int16_t sig[NE_MAX_FS_KHZ];
    
    for(int i = 0; i < N; i++){
        int max_abs = 0;
        for(int j = 0; j < L_sub; j++){
            max_abs = std::max(max_abs, std::abs((int)in[i*L_sub + j]));
        }
        int max_abs_ = std::max(ne->prev_max_abs, max_abs);
        ne->prev_max_abs = max_abs;
        if((float)max_abs_*target_gain > 32000.0f){
            used_target_gain = 32000.0f/(float)max_abs_;
            alpha = 0.1f;
        } else {
            used_target_gain = target_gain;
            alpha = 0.025f;
        }
        memcpy(sig, ne->sig_buf, L_sub * sizeof(int16_t));
        memcpy(ne->sig_buf, &in[i*L_sub], L_sub * sizeof(int16_t));
        float s;
        for(int j = 0; j < L_sub; j++){
            ne->gain += (used_target_gain - ne->gain) * alpha;
            s = (float)sig[j] * ne->gain;
            if(s > 32767.0){
                out[i*L_sub + j] = 32767;
            } else if(s < -32768.0){
                out[i*L_sub + j] = -32768;
            } else {
                out[i*L_sub + j] = (int16_t)s;
            }
        }
    }
}

void normalizer_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct normalizer_effect *ne = (struct normalizer_effect*)st;
    int L10 = (ne->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in){
        error("normalizer_process needs 10 ms chunks \n");
    }
    int L_buf = (NE_VAD_FS_KHZ * L10) / ne->fs_khz;
    int16_t buf[L_buf];
    for( int i = 0; i < N; i++){
        ne->resampler->Resample( &in[i*L10], L10, buf, L_buf);

        ne->silk_enc.frame_length = L_buf;
        silk_VAD_GetSA_Q8_c(&ne->silk_enc, buf);
    
        ne->vad_buf[ne->buf_idx] = (int)ne->silk_enc.speech_activity_Q8;
    
        float nrg = 0.0f, s;
        for(int j = 0; j < L_in; j++){
            s = (float)in[i*L10 + j];
            nrg += s*s;
        }
        nrg = nrg/(float)L_in;
        nrg += 1.0f;
        float nrgDb = 10.0f*log10(nrg);
        ne->nrg_buf[ne->buf_idx] = (int)nrgDb;
        if(nrgDb > ne->maxLvl){
            ne->maxLvl += (nrgDb - ne->maxLvl)*0.1;
        } else {
            ne->maxLvl += (nrgDb - ne->maxLvl)*0.01;
        }
        ne->buf_idx = (ne->buf_idx + 1) & NE_BUF_MASK;
    
        ne->frame_cnt++;
        if(ne->frame_cnt == 10){
            int32_t mean_vad = 0;
            for(int j = 0; j < NE_BUF_SIZE; j++){
                mean_vad += ne->vad_buf[j];
            }
            mean_vad = mean_vad >> NE_LOG2_BUF_SIZE;
            if(mean_vad > NE_VAD_THRES){
                // Build nrg histogram
                int hist[NE_HIST_LEN] = {0};
                for(int j = 0; j < NE_BUF_SIZE; j++){
                    hist[ne->nrg_buf[j]]++;
                }
                // Find percentile
                int thres = (int)((float)NE_BUF_SIZE * NE_PERCENTILE);
                int sum = 0;
                int bin = NE_HIST_LEN -1;
                while(sum < thres){
                    sum += hist[bin];
                    bin--;
                    if(bin == 0){
                        break;
                    }
                }
                float diff = ne->speechLvl - (float)bin;
                if(diff < NE_UPDATE_RANGE){
                    if(diff < 0){
                        ne->speechLvl -= diff * NE_ALPHA_MAX_UP;
                    } else {
                        ne->speechLvl -= diff * NE_ALPHA_MAX_DOWN;
                    }
                        
                    float diff = ne->Target_vol_db - ( ne->speechLvl + ne->Target_gain_db);
                    if(diff > NE_HYSTERESIS_DB){
                        diff = std::min(diff, NE_MAX_INCR_DB);
                    } else if (diff < -NE_HYSTERESIS_DB){
                        diff = std::max(diff, NE_MAX_DECR_DB);
                    } else {
                        diff = 0.0f;
                    }
                    ne->Target_gain_db += diff;
                    ne->Target_gain_db = std::max(ne->Target_gain_db, 0.0f);
                    ne->Target_gain_db = std::min(ne->Target_gain_db, NE_MAX_GAIN_DB);
                }
            }
            ne->frame_cnt = 0;
        }
        
        if(ne->squelch_enabled){
            float squelsh_gain = ((ne->speechLvl - ne->maxLvl) - 20) * 0.1 * NE_MAX_SQUELCH_GAIN_DB;
            squelsh_gain = std::max(squelsh_gain, 0.0f);
            squelsh_gain = std::min(squelsh_gain, NE_MAX_SQUELCH_GAIN_DB);
            ne->Squelch_gain_db = squelsh_gain;
        }
        
        apply_gain(ne, &in[i*L10], &out[i*L10], L10);
    }
    *L_out = L_in;
}
