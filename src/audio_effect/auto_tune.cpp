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
#include "auto_tune.h"
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

static float lag_table[ATE_NUM_LAGS] = {
    14, 15, 16, 17, 18, 19, 20, 22,
    23, 24, 26, 27, 29, 31, 32, 34,
    36, 39, 41, 43, 46, 49, 51, 54,
    58, 61, 65, 69, 73, 77, 82, 86,
    92, 97, 103, 109, 115, 122, 130, 137,
    145, 154, 163, 173, 183, 194, 206, 218,
    231, 245, 259, 275, 291, 308, 327, 346,
    367, 388, 411, 436, 462, 489, 518, 549,
};

// cuts off above 0.9 * fs/2 as we adjust the pitch by a factor 0.9 - 1/0.9
static float a_lp[ATE_NUM_BIQUADS][2] = {
    {1.0486f, 0.2961f},
    {1.3209f, 0.6327f}
};

static float b_lp[ATE_NUM_BIQUADS][3] = {
    {1.0f, 2.0f, 1.0f},
    {0.4328f, 0.8657f, 0.4328f}
};

static int find_nearest_lag(struct auto_tune_effect *ate, int lag_in)
{
    int best_idx = 0;
    int bestE = 1 << 10;
    int E, abs_E;
    int strength = 2 + ate->strength;
    
    for(int i = 0; i < ATE_NUM_LAGS/strength; i++){
        E = (lag_in - lag_table[i*strength]);
        abs_E = E;
        if(abs_E < 0){
            abs_E = -abs_E;
        }
        if(ate->prev_idx == (i*strength)){
            abs_E = abs_E - ate->lag_hysterisis;
        }
        if(E > 0){
            abs_E = abs_E - ate->pitch_up_bias;
        }
        if(abs_E < bestE){
            bestE = abs_E;
            best_idx = i*strength;
        }
    }
    return lag_table[best_idx];
}

static int find_nearest_lag_always_up(struct auto_tune_effect *ate, int lag_in)
{
    int best_idx = 0;
    int strength = 2 + ate->strength;
    int E;
    for(int i = 0; i < ATE_NUM_LAGS/strength; i++){
        E = (lag_in - lag_table[i*strength]);
        if(E < 0){
            break;
        }
        best_idx = i*strength;
    }
    return lag_table[best_idx];
}

void* create_auto_tune(int fs_hz, int strength)
{
    struct auto_tune_effect* ate = (struct auto_tune_effect*)calloc(sizeof(struct auto_tune_effect),1);
 
    ate->resampler = new webrtc::PushResampler<int16_t>;

    ate->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&ate->pest, fs_hz, 2);
    
    time_scale_init(&ate->tscale, fs_hz, fs_hz);
    
    ate->resampler->InitializeIfNeeded(fs_hz, fs_hz * ATE_UP_FAC, 1);
    
    ate->read_idx = ate->fs_khz * ATE_EXTRA_BUF_MS * ATE_UP_FAC;
    ate->comp_smth = 1.0f;
    
    ate->prev_idx = -1;
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 2){
        strength = 2;
    }

    ate->lag_hysterisis = 1;
    ate->strength = strength;
    ate->comp_smth_alpha = 1.0f;
    if(strength == 2){
        ate->comp_smth_alpha = 0.4;
        ate->pitch_up_bias = 2;
        ate->lag_hysterisis = 3;
    }
    
    return (void*)ate;
}

void free_auto_tune(void *st)
{
    struct auto_tune_effect *ate = (struct auto_tune_effect*)st;
    
    delete ate->resampler;
    free_find_pitch_lags(&ate->pest);
    
    free(ate);
}

static void find_min_max_pitch(struct auto_tune_effect *ate, int *min_pL, int *max_pL)
{
    int pitchL;
    int maxL = 0;
    int minL = 1000;
    float inv_comp = 1.0/ate->comp_smth;
    if(ate->pest.voiced){
        for(int i = 0; i < Z_NB_SUBFR; i++){
            pitchL = (ate->fs_khz*ate->pest.pitchL[i])/16;
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

static int median_pitch(struct auto_tune_effect *ate)
{
    float mean = 0;
    for(int i = 0; i < ATE_PL_BUF_SZ; i++){
        mean += (float)ate->pL_buf[i];
    }
    mean = mean / (float)ATE_PL_BUF_SZ;
    
    int median = 0;
    float min_diff = 1e7;
    float diff;
    for(int i = 0; i < ATE_PL_BUF_SZ; i++){
        diff = ( mean - (float)ate->pL_buf[i]);
        diff = diff * diff;
        if(diff < min_diff){
            median = ate->pL_buf[i];
            min_diff = diff;
        }
    }
    return median;
}

void auto_tune_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct auto_tune_effect *ate = (struct auto_tune_effect*)st;
    
    int L10 = (ate->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in || L_in > (ate->fs_khz * MAX_L_MS)){
        error("auto_tune_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    int L10_out = L10*ATE_UP_FAC;
    int L_extra = ate->fs_khz * ATE_EXTRA_BUF_MS * ATE_UP_FAC;
    
    int16_t tmp_buf[L10 * 2];
    int16_t in_lp[L10];
    int pL, median_pL;
    float comp;
    for( int i = 0; i < N; i++){
        biquad(&ate->lp_filt[0], a_lp[0], b_lp[0], &in[i*L10], in_lp, L10);
        for(int j = 1 ; j < ATE_NUM_BIQUADS; j++){
            biquad(&ate->lp_filt[j], a_lp[j], b_lp[j], in_lp, in_lp, L10);
        }
        
        find_pitch_lags(&ate->pest, &in[i*L10], L10);

        ate->resampler->Resample( &in[i*L10], L10, &ate->buf[(ATE_BUF_FRAMES-1)*L10_out + L_extra], L10_out);
        
        pL = ((ate->pest.pitchL[2] + ate->pest.pitchL[3]) >> 1);
        ate->pL_buf[ATE_PL_BUF_SZ-1] = pL;
        
        median_pL = median_pitch(ate);
        
        ate->pL_smth += 0.5 * ((float)median_pL - ate->pL_smth);
        
        if(median_pL > 0){
            pL = find_nearest_lag(ate, median_pL);
            comp = (float)median_pL / (float)pL;
        } else {
            comp = 1.0f;
            ate->prev_idx = -1;
        }
        
        ate->comp_smth += (comp - ate->comp_smth) * ate->comp_smth_alpha;
        
        int n = 0;
        while(ate->read_idx < (L10_out + L_extra)){
            int idx = (int)ate->read_idx;
            tmp_buf[n] = ate->buf[idx];
            n++;
            ate->read_idx += ate->comp_smth * ATE_UP_FAC;
        }
        
        int min_pL, max_pL;
        find_min_max_pitch(ate, &min_pL, &max_pL);
        
        time_scale_insert(&ate->tscale, tmp_buf, n, max_pL, min_pL, ate->pest.voiced);
        
        time_scale_extract(&ate->tscale, &out[i*L10], L10);
        
        memmove(ate->buf, &ate->buf[L10_out], ((ATE_BUF_FRAMES-1)*L10_out + L_extra) * sizeof(int16_t));
        ate->read_idx -= L10_out;
        memmove(ate->pL_buf, &ate->pL_buf[1], (ATE_PL_BUF_SZ-1) * sizeof(int));
    }
    
    *L_out = L_in;
}
