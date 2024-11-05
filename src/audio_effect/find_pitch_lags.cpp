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
#include "find_pitch_lags.h"
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

void init_find_pitch_lags(struct pitch_estimator *pest, int fs_hz, int complexity)
{
    pest->fs_khz = fs_hz/1000;
    if(complexity < 0){
        pest->complexity = 0;
    }
    if(complexity > 2){
        pest->complexity = 2;
    }
}

void free_find_pitch_lags(struct pitch_estimator *pest)
{
}

void find_pitch_lags(struct pitch_estimator *pest, int16_t x[], int L)
{
    silk_float thrhld, res_nrg;
    silk_float auto_corr[ Z_LPC_ORDER + 1 ];
    silk_float A[         Z_LPC_ORDER ];
    silk_float refl_coef[ Z_LPC_ORDER ];
    silk_float Wsig[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    silk_float sig[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    silk_float res[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    int L_re = (L*Z_FS_KHZ)/pest->fs_khz;

    /* resample to 16 khz if > 16 khz */
    auto resampler = webrtc::PushResampler<int16_t>(L, L_re, 1);
    webrtc::MonoView<int16_t> inv(x, L);
    webrtc::MonoView<int16_t> outv(&pest->buf[(Z_FS_KHZ*Z_PEST_BUF_SZ_MS) - L_re], L_re);
    resampler.Resample(inv, outv); 

    /* Apply window */
    for( int i = 0; i < Z_FS_KHZ*Z_PEST_BUF_SZ_MS; i++ ) {
        Wsig[i] = (silk_float)pest->buf[i];
        sig[i] = (silk_float)pest->buf[i];
    }
    silk_apply_sine_window_FLP( Wsig, Wsig, 1, Z_WIN_LEN_MS*Z_FS_KHZ );
    silk_apply_sine_window_FLP( &Wsig[Z_FS_KHZ*(Z_PEST_BUF_SZ_MS - Z_WIN_LEN_MS)], &Wsig[Z_FS_KHZ*(Z_PEST_BUF_SZ_MS - Z_WIN_LEN_MS)], 2, Z_WIN_LEN_MS*Z_FS_KHZ );
    
    /* Calculate autocorrelation sequence */
    silk_autocorrelation_FLP( auto_corr, Wsig, Z_FS_KHZ*Z_PEST_BUF_SZ_MS, Z_LPC_ORDER + 1 );
        
    /* Add white noise, as fraction of energy */
    auto_corr[ 0 ] += auto_corr[ 0 ] * 1e-3f + 1;
    
    /* Calculate the reflection coefficients using Schur */
    res_nrg = silk_schur_FLP( refl_coef, auto_corr, Z_LPC_ORDER );    
    
    /* Convert reflection coefficients to prediction coefficients */
    silk_k2a_FLP( A, refl_coef, Z_LPC_ORDER );
    
    /* Bandwidth expansion */
    silk_bwexpander_FLP( A, Z_LPC_ORDER, 0.99f );
    
    /*****************************************/
    /* LPC analysis filtering                */
    /*****************************************/
    silk_LPC_analysis_filter_FLP( res, A, sig, Z_FS_KHZ*Z_PEST_BUF_SZ_MS, Z_LPC_ORDER );

    /* Threshold for pitch estimator */
    thrhld  = 0.2f;
    opus_int16 lagIndex;
    opus_int8 contourIndex;
    /*****************************************/
    /* Call Pitch estimator                  */
    /*****************************************/
    silk_float LTPCorr;
    if( silk_pitch_analysis_core_FLP( res, pest->pitchL, &lagIndex,
                                     &contourIndex, &LTPCorr, pest->pitchL[3], 0.7,
                                     thrhld, Z_FS_KHZ, pest->complexity, 4, 4 ) == 0 )
    {
        pest->voiced = true;
    } else {
        pest->voiced = false;
    }
    pest->LTPCorr_Q15 = (opus_int)(LTPCorr * (float)((int)1 << 15));
    memmove(&pest->buf[0], &pest->buf[L_re], ((Z_FS_KHZ*Z_PEST_BUF_SZ_MS) - L_re)*sizeof(int16_t));
    (void)res_nrg;
}

