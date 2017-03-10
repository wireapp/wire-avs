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
#include "pace_shift.h"
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

void* create_pace_up_shift(int fs_hz, int strength)
{
    struct pace_shift_effect* pse = (struct pace_shift_effect*)calloc(sizeof(struct pace_shift_effect),1);
 
    pse->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&pse->pest, fs_hz, 2);

    if(strength < 0){
        strength = 0;
    }
    if(strength > 3){
        strength = 3;
    }
    
    pse->shift = 1.0f - 0.2*(strength+1);
    
    time_scale_init(&pse->tscale, fs_hz, fs_hz);
    
    return (void*)pse;
}

void* create_pace_down_shift(int fs_hz, int strength)
{
    struct pace_shift_effect* pse = (struct pace_shift_effect*)calloc(sizeof(struct pace_shift_effect),1);
    
    pse->fs_khz = fs_hz/1000;
    
    init_find_pitch_lags(&pse->pest, fs_hz, 2);
    
    if(strength < 0){
        strength = 0;
    }
    if(strength > 3){
        strength = 3;
    }
    
    pse->shift = 1.0f + 0.2*(strength+1);
    
    time_scale_init(&pse->tscale, fs_hz, fs_hz);
    
    return (void*)pse;
}

void free_pace_shift(void *st)
{
    struct pace_shift_effect *pse = (struct pace_shift_effect*)st;
    
    delete pse->pest.resampler;
    
    free(pse);
}

void pace_shift_length_factor(void *st, int *length_mod_Q10){
    struct pace_shift_effect *pse = (struct pace_shift_effect*)st;
    
    *length_mod_Q10 = (int)(pse->shift * 1024.0f);
}

static void find_min_max_pitch(struct pace_shift_effect *pse, int *min_pL, int *max_pL)
{
    int pitchL;
    int maxL = 0;
    int minL = 1000;
    if(pse->pest.voiced){
        for(int i = 0; i < Z_NB_SUBFR; i++){
            pitchL = (pse->fs_khz*pse->pest.pitchL[i])/16;
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

void pace_shift_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct pace_shift_effect *pse = (struct pace_shift_effect*)st;
    
    int L10 = (pse->fs_khz * 10);
    int N = (int)L_in / L10;
    if( N * L10 != L_in || L_in > (pse->fs_khz * MAX_L_MS)){
        error("pitch_shift_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    int L10_out = (int)((float)L10*pse->shift);
    
    for( int i = 0; i < N; i++){
        find_pitch_lags(&pse->pest, &in[i*L10], L10);
        
        int min_pL, max_pL;
        find_min_max_pitch(pse, &min_pL, &max_pL);
        
        time_scale_insert(&pse->tscale, &in[i*L10], L10, max_pL, min_pL, pse->pest.voiced);
        
        time_scale_extract(&pse->tscale, &out[i*L10_out], L10_out);
        
        pse->cnt++;
    }

    *L_out = L10_out*N;
}
