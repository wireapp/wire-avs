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
    pest->resampler = new webrtc::PushResampler<int16_t>;
    pest->resampler->InitializeIfNeeded(fs_hz, 16000, 1);
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
    delete pest->resampler;
}

void find_pitch_lags(struct pitch_estimator *pest, int16_t x[], int L)
{
#if !defined(WEBRTC_ARCH_ARM)
    silk_float thrhld, res_nrg;
    silk_float auto_corr[ Z_LPC_ORDER + 1 ];
    silk_float A[         Z_LPC_ORDER ];
    silk_float refl_coef[ Z_LPC_ORDER ];
    silk_float Wsig[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    silk_float sig[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    silk_float res[Z_FS_KHZ*Z_PEST_BUF_SZ_MS];
    int L_re = (L*Z_FS_KHZ)/pest->fs_khz;
    
    /* resample to 16 khz if > 16 khz */
    pest->resampler->Resample( x, L, &pest->buf[(Z_FS_KHZ*Z_PEST_BUF_SZ_MS) - L_re], L_re);

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
#else
    opus_int16 Wsig[16*Z_PEST_BUF_SZ_MS];
    opus_int16 res[16*Z_PEST_BUF_SZ_MS];
    opus_int   scale;
    opus_int32 auto_corr[ Z_LPC_ORDER + 1 ];
    opus_int32 res_nrg;
    opus_int16 rc_Q15[ Z_LPC_ORDER ];
    opus_int32 A_Q24[     MAX_FIND_PITCH_LPC_ORDER ];
    opus_int16 A_Q12[     MAX_FIND_PITCH_LPC_ORDER ];
    int L_re = (L*16)/pest->fs_khz;
    
    /* resample to 16 khz if > 16 khz */
    pest->resampler->Resample( x, L, &pest->buf[(Z_FS_KHZ*Z_PEST_BUF_SZ_MS) - L_re], L_re);
    
    /* Window 40 ms */
    silk_apply_sine_window( Wsig, pest->buf, 1, Z_WIN_LEN_MS*Z_FS_KHZ );
    for( int i = Z_WIN_LEN_MS*Z_FS_KHZ; i < Z_FS_KHZ*(Z_PEST_BUF_SZ_MS-Z_WIN_LEN_MS) ; i++ ) {
        Wsig[i] = (opus_int16)pest->buf[i];
    }
    silk_apply_sine_window( &Wsig[Z_FS_KHZ*(Z_PEST_BUF_SZ_MS-Z_WIN_LEN_MS)], &pest->buf[Z_FS_KHZ*(Z_PEST_BUF_SZ_MS-Z_WIN_LEN_MS)], 2, Z_WIN_LEN_MS*Z_FS_KHZ );
    
    /* Calculate autocorrelation sequence */
    silk_autocorr( auto_corr, &scale, pest->buf, Z_FS_KHZ*Z_PEST_BUF_SZ_MS, Z_LPC_ORDER + 1, 0 );
    
    /* Add white noise, as fraction of energy */
    auto_corr[ 0 ] = silk_SMLAWB( auto_corr[ 0 ], auto_corr[ 0 ], 65 ) + 1;
    
    /* Calculate the reflection coefficients using schur */
    res_nrg = silk_schur( rc_Q15, auto_corr, Z_LPC_ORDER );
    
    /* Convert reflection coefficients to prediction coefficients */
    silk_k2a( A_Q24, rc_Q15, Z_LPC_ORDER );
    
    /* Convert From 32 bit Q24 to 16 bit Q12 coefs */
    for( int i = 0; i < Z_LPC_ORDER; i++ ) {
        A_Q12[ i ] = (opus_int16)silk_SAT16( silk_RSHIFT( A_Q24[ i ], 12 ) );
    }
    
    /* Do BWE */
    silk_bwexpander( A_Q12, Z_LPC_ORDER, 64881); // 0.99 in Q16
    
    /* LPC analysis filtering */
    silk_LPC_analysis_filter( res, pest->buf, A_Q12, Z_FS_KHZ*Z_PEST_BUF_SZ_MS, Z_LPC_ORDER, 0 );
    
    /* Threshold for pitch estimator */
    opus_int thrhld_Q13  = 1638; // 0.2f
    opus_int thrhld_Q16  = 45875;
    opus_int16 lagIndex;
    opus_int8 contourIndex;
    /*****************************************/
    /* Call Pitch estimator                  */
    /*****************************************/
    if( silk_pitch_analysis_core( res, pest->pitchL, &lagIndex,
                                     &contourIndex, &pest->LTPCorr_Q15, pest->pitchL[3],
                                     thrhld_Q16, thrhld_Q13, 16, pest->complexity , 4, 0 ) == 0 )
    {
        pest->voiced = true;
    } else {
        pest->voiced = false;
    }
    memmove(&pest->buf[0], &pest->buf[L_re], ((16*Z_PEST_BUF_SZ_MS) - L_re)*sizeof(int16_t));
#endif
}

