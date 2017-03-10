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
#include "harmonizer.h"
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

static float lag_table[HMZ_NUM_LAGS] = {
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
static float a_lp[HMZ_NUM_BIQUADS][2] = {
    {1.0486f, 0.2961f},
    {1.3209f, 0.6327f}
};

static float b_lp[HMZ_NUM_BIQUADS][3] = {
    {1.0f, 2.0f, 1.0f},
    {0.4328f, 0.8657f, 0.4328f}
};

static void find_nearest_lag(struct harmonizer_effect *he, int lag_in, int lags_out[])
{
    int best_idx = 0;
    int bestE = 1 << 10;
    int E, abs_E;
    int strength = 2;
    
    for(int i = 0; i < HMZ_NUM_LAGS/strength; i++){
        E = (lag_in - lag_table[i*strength]);
        abs_E = E;
        if(abs_E < 0){
            abs_E = -abs_E;
        }
        if(he->prev_idx == (i*strength)){
            abs_E = abs_E - he->lag_hysterisis;
        }
        if(abs_E < bestE){
            bestE = abs_E;
            best_idx = i*strength;
        }
    }
    best_idx = best_idx + ((HMZ_NUM_CHANNELS - 1) >> 1) * he->harm_shift;
    if(best_idx > (HMZ_NUM_LAGS-1)){
        best_idx = HMZ_NUM_LAGS-1;
    }    
    for(int i = 0; i < HMZ_NUM_CHANNELS; i++){
        if((best_idx - he->harm_shift*i) < 0){
            lags_out[i] = lag_table[0];
        } else {
            lags_out[i] = lag_table[best_idx - he->harm_shift*i];
        }
    }
}

void* create_harmonizer(int fs_hz, int strength)
{
    struct harmonizer_effect* he = (struct harmonizer_effect*)calloc(sizeof(struct harmonizer_effect),1);
 
    he->resampler = new webrtc::PushResampler<int16_t>;

    he->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&he->pest, fs_hz, 2);
    
    he->resampler->InitializeIfNeeded(fs_hz, fs_hz * HMZ_UP_FAC, 1);
    
    for(int i = 0; i < HMZ_NUM_CHANNELS; i++){
        time_scale_init(&he->hm_ch[i].tscale, fs_hz, fs_hz);
        he->hm_ch[i].read_idx = he->fs_khz * HMZ_EXTRA_BUF_MS * HMZ_UP_FAC;
        he->hm_ch[i].comp_smth = 1.0f;
    }
        
    he->prev_idx = -1;
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 2){
        strength = 2;
    }

    he->lag_hysterisis = 1;
    he->strength = strength;
    he->comp_smth_alpha = 1.0f;
    he->harm_shift = strength + 2;
    
    return (void*)he;
}

void free_harmonizer(void *st)
{
    struct harmonizer_effect *he = (struct harmonizer_effect*)st;
    
    delete he->resampler;
    free_find_pitch_lags(&he->pest);
    
    free(he);
}

static void find_min_max_pitch(struct harmonizer_effect *he, int *min_pL, int *max_pL, int ch)
{
    int pitchL;
    int maxL = 0;
    int minL = 1000;
    float inv_comp = 1.0/he->hm_ch[ch].comp_smth;
    if(he->pest.voiced){
        for(int i = 0; i < Z_NB_SUBFR; i++){
            pitchL = (he->fs_khz*he->pest.pitchL[i])/16;
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

static int median_pitch(struct harmonizer_effect *he)
{
    float mean = 0;
    for(int i = 0; i < HMZ_PL_BUF_SZ; i++){
        mean += (float)he->pL_buf[i];
    }
    mean = mean / (float)HMZ_PL_BUF_SZ;
    
    int median = 0;
    float min_diff = 1e7;
    float diff;
    for(int i = 0; i < HMZ_PL_BUF_SZ; i++){
        diff = ( mean - (float)he->pL_buf[i]);
        diff = diff * diff;
        if(diff < min_diff){
            median = he->pL_buf[i];
            min_diff = diff;
        }
    }
    return median;
}

void harmonizer_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct harmonizer_effect *he = (struct harmonizer_effect*)st;
    
    int L10 = (he->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in || L_in > (he->fs_khz * MAX_L_MS)){
        error("auto_tune_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    int L10_out = L10*HMZ_UP_FAC;
    int L_extra = he->fs_khz * HMZ_EXTRA_BUF_MS * HMZ_UP_FAC;
    
    int16_t tmp_buf[L10 * 2];
    int16_t in_lp[L10];
    int pL[HMZ_NUM_CHANNELS], median_pL;
    float comp[HMZ_NUM_CHANNELS];
    for( int i = 0; i < N; i++){
        biquad(&he->lp_filt[0], a_lp[0], b_lp[0], &in[i*L10], in_lp, L10);
        for(int j = 1 ; j < HMZ_NUM_BIQUADS; j++){
            biquad(&he->lp_filt[j], a_lp[j], b_lp[j], in_lp, in_lp, L10);
        }
        
        find_pitch_lags(&he->pest, &in[i*L10], L10);

        he->resampler->Resample( &in[i*L10], L10, &he->buf[(HMZ_BUF_FRAMES-1)*L10_out + L_extra], L10_out);
        
        he->pL_buf[HMZ_PL_BUF_SZ-1] = ((he->pest.pitchL[2] + he->pest.pitchL[3]) >> 1);
        
        median_pL = median_pitch(he);
        
        he->pL_smth += 0.5 * ((float)median_pL - he->pL_smth);
        
        if(median_pL > 0){
            find_nearest_lag(he, median_pL, pL);
            for(int c = 0 ; c < HMZ_NUM_CHANNELS; c++){
                comp[c] = (float)median_pL / (float)pL[c];
            }
        } else {
            for(int c = 0 ; c < HMZ_NUM_CHANNELS; c++){
                comp[c] = 1.0f;
            }
            he->prev_idx = -1;
        }
        
        memset(&out[i*L10], 0, L10 * sizeof(int16_t));
        int left_shift = 1;
        for(int c = 0 ; c < HMZ_NUM_CHANNELS; c++){
            he->hm_ch[c].comp_smth += (comp[c] - he->hm_ch[c].comp_smth) * he->comp_smth_alpha;
        
            int n = 0;
            while(he->hm_ch[c].read_idx < (L10_out + L_extra)){
                int idx = (int)he->hm_ch[c].read_idx;
                tmp_buf[n] = he->buf[idx];
                n++;
                he->hm_ch[c].read_idx += he->hm_ch[c].comp_smth * HMZ_UP_FAC;
            }
        
            int min_pL, max_pL;
            find_min_max_pitch(he, &min_pL, &max_pL, c);
        
            time_scale_insert(&he->hm_ch[c].tscale, tmp_buf, n, max_pL, min_pL, he->pest.voiced);
        
            time_scale_extract(&he->hm_ch[c].tscale, tmp_buf, L10);
        
            if(c == (HMZ_NUM_CHANNELS >> 1)){
                left_shift = 1;
            } else {
                left_shift = 2;
            }
            
            for(int j = 0; j < L10; j++){
                out[i*L10 + j] += (tmp_buf[j] >> left_shift);
            }
            
            he->hm_ch[c].read_idx -= L10_out;
        }
        
        memmove(he->buf, &he->buf[L10_out], ((HMZ_BUF_FRAMES-1)*L10_out + L_extra) * sizeof(int16_t));
        memmove(he->pL_buf, &he->pL_buf[1], (HMZ_PL_BUF_SZ-1) * sizeof(int));
    }
    
    *L_out = L_in;
}
