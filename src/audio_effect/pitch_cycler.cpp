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
#include "pitch_cycler.h"
#include "avs_audio_effect.h"
#include <math.h>

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

void* create_pitch_cycler(int fs_hz, int strength)
{
    struct pitch_cycler_effect* pce = (struct pitch_cycler_effect*)calloc(sizeof(struct pitch_cycler_effect),1);
 
    pce->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&pce->pest, fs_hz, 2);
    
    time_scale_init(&pce->tscale, fs_hz * PCE_EXTRA_UP, fs_hz * PCE_EXTRA_UP);
 
    pce->resampler = new webrtc::PushResampler<int16_t>;
    pce->resampler->InitializeIfNeeded(fs_hz, fs_hz * PCE_UP_FAC * PCE_EXTRA_UP, 1);

    pce->resampler_out = new webrtc::PushResampler<int16_t>;
    pce->resampler_out->InitializeIfNeeded(fs_hz * PCE_EXTRA_UP, fs_hz, 1);
    
    pce->read_idx = pce->fs_khz * PCE_EXTRA_BUF_MS * PCE_UP_FAC * PCE_EXTRA_UP;
    
    pce->comp = 1.5;
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 2){
        strength = 2;
    }
    
    pce->comp_delta = 0.001 * (strength+1);
    pce->used_comp_delta = pce->comp_delta;
    
    return (void*)pce;
}

void free_pitch_cycler(void *st)
{
    struct pitch_cycler_effect *pce = (struct pitch_cycler_effect*)st;
    
    delete pce->resampler;
    free_find_pitch_lags(&pce->pest);
    
    free(pce);
}

static void find_min_max_pitch(struct pitch_cycler_effect *pce, int *min_pL, int *max_pL)
{
    int pitchL;
    int maxL = 0;
    int minL = 1000;
    float inv_comp = 1.0/pce->comp;
    if(pce->pest.voiced){
        for(int i = 0; i < Z_NB_SUBFR; i++){
            pitchL = (PCE_EXTRA_UP*pce->fs_khz*pce->pest.pitchL[i])/16;
            pitchL = (int)((float)pitchL * inv_comp);
            if(pitchL > maxL){
                maxL = pitchL;
            }
            if(pitchL < minL){
                minL = pitchL;
            }
        }
    } else {
        maxL = 0;
        minL = 0;
    }
    *min_pL = minL;
    *max_pL = maxL;
}

void pitch_cycler_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct pitch_cycler_effect *pce = (struct pitch_cycler_effect*)st;
    
    int L10 = (pce->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in || L_in > (pce->fs_khz * MAX_L_MS)){
        error("pitch_cycler_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    int L10_out = L10*PCE_UP_FAC*PCE_EXTRA_UP;
    int L_extra = pce->fs_khz * PCE_EXTRA_BUF_MS * PCE_UP_FAC * PCE_EXTRA_UP;
    
    int16_t tmp_buf[L10 * 4];
    int16_t tmp_out[L10 * PCE_EXTRA_UP];
    int16_t in_lp[L10];
    int pL, median_pL;
    float comp;
    for( int i = 0; i < N; i++){
        find_pitch_lags(&pce->pest, &in[i*L10], L10);

        pce->resampler->Resample( &in[i*L10], L10, &pce->buf[(PCE_BUF_FRAMES-1)*L10_out + L_extra], L10_out);
        
        int n = 0;
        while(pce->read_idx < (L10_out + L_extra)){
            int idx = (int)pce->read_idx;
            tmp_buf[n] = pce->buf[idx];
            n++;
            pce->read_idx += pce->comp * PCE_UP_FAC;
        }
        
        pce->comp += pce->used_comp_delta;
        if(pce->comp > PCE_COMP_MAX){
            pce->used_comp_delta = -pce->comp_delta;
        }
        if(pce->comp < PCE_COMP_MIN){
            pce->used_comp_delta = pce->comp_delta;
        }
        
        int min_pL, max_pL;
        find_min_max_pitch(pce, &min_pL, &max_pL);
        
        time_scale_insert(&pce->tscale, tmp_buf, n, max_pL, min_pL, pce->pest.voiced);
        
        time_scale_extract(&pce->tscale, tmp_out, L10 * PCE_EXTRA_UP);
        
        pce->resampler_out->Resample( tmp_out, L10 * PCE_EXTRA_UP, &out[i*L10], L10);
        
        memmove(pce->buf, &pce->buf[L10_out], ((PCE_BUF_FRAMES-1)*L10_out + L_extra) * sizeof(int16_t));
        pce->read_idx -= L10_out;
    }
    
    *L_out = L_in;
}
