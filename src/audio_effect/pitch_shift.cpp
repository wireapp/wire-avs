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
#include "pitch_shift.h"
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

void* create_pitch_up_shift(int fs_hz, int strength)
{
    struct up_down_params up_down_table[4] = {
        {9,10},  // 100 Hz -> 111 Hz
        {4,5},   // 100 Hz -> 125 Hz
        {7,10},  // 100 Hz -> 142 Hz
        {3,5}    // 100 Hz -> 166 Hz
    };
    struct pitch_shift_effect* pse = (struct pitch_shift_effect*)calloc(sizeof(struct pitch_shift_effect),1);
 
    pse->resampler = new webrtc::PushResampler<int16_t>;

    pse->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&pse->pest, fs_hz, 2);
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 3){
        strength = 3;
    }
    
    pse->up = up_down_table[strength].up;
    pse->down = up_down_table[strength].down;

    time_scale_init(&pse->tscale, (fs_hz*pse->up)/pse->down, fs_hz);
    
    pse->resampler->InitializeIfNeeded(fs_hz, (fs_hz*pse->up)/pse->down, 1);
    
    return (void*)pse;
}

void* create_pitch_down_shift(int fs_hz, int strength)
{
    struct up_down_params up_down_table[4] = {
        {9,8},  // 100 Hz -> 89 Hz
        {5,4},  // 100 Hz -> 80 Hz
        {11,8}, // 100 Hz -> 73 Hz
        {3,2}   // 100 Hz -> 66 Hz
    };
    
    struct pitch_shift_effect* pse = (struct pitch_shift_effect*)calloc(sizeof(struct pitch_shift_effect),1);
    
    pse->resampler = new webrtc::PushResampler<int16_t>;
    
    pse->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&pse->pest, fs_hz, 2);
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 3){
        strength = 3;
    }
    
    pse->up = up_down_table[strength].up;
    pse->down = up_down_table[strength].down;
    
    time_scale_init(&pse->tscale, (fs_hz*pse->up)/pse->down, fs_hz);
    
    pse->resampler->InitializeIfNeeded(fs_hz, (fs_hz*pse->up)/pse->down, 1);
    
    return (void*)pse;
}

void free_pitch_shift(void *st)
{
    struct pitch_shift_effect *pse = (struct pitch_shift_effect*)st;
    
    delete pse->resampler;
    free_find_pitch_lags(&pse->pest);
    
    free(pse);
}

static void find_min_max_pitch(struct pitch_shift_effect *pse, int *min_pL, int *max_pL)
{
    int pitchL;
    int maxL = 0;
    int minL = 1000;
    if(pse->pest.voiced){
        for(int i = 0; i < Z_NB_SUBFR; i++){
            pitchL = (pse->fs_khz*pse->pest.pitchL[i]*pse->up)/(16*pse->down);
            if(pitchL > maxL){
                maxL = pitchL;
            }
            if(pitchL < minL){
                minL = pitchL;
            }
        }
    }
    *min_pL = minL;
    *max_pL = maxL;
}

void pitch_shift_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct pitch_shift_effect *pse = (struct pitch_shift_effect*)st;
    
    int L10 = (pse->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in || L_in > (pse->fs_khz * MAX_L_MS)){
        error("pitch_shift_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    int L10_out = (L10*pse->up)/pse->down;
    
    int16_t tmp_buf[L10_out];
    for( int i = 0; i < N; i++){
        find_pitch_lags(&pse->pest, &in[i*L10], L10);

        pse->resampler->Resample( &in[i*L10], L10, tmp_buf, L10_out);
        
        int min_pL, max_pL;
        find_min_max_pitch(pse, &min_pL, &max_pL);
    
        time_scale_insert(&pse->tscale, tmp_buf, L10_out, max_pL, min_pL, pse->pest.voiced);
        
        time_scale_extract(&pse->tscale, &out[i*L10], L10);
    }
    *L_out = L_in;
}
