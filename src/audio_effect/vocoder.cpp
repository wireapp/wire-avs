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
#include "vocoder.h"
#include "avs_audio_effect.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

void* create_vocoder(int fs_hz, int strength)
{
    struct vocoder_effect* ve = (struct vocoder_effect*)calloc(sizeof(struct vocoder_effect),1);
    
    ve->resampler_in = new webrtc::PushResampler<int16_t>;
    ve->resampler_in->InitializeIfNeeded(fs_hz, PROC_FS_KHZ*1000, 1);
    
    ve->resampler_out = new webrtc::PushResampler<int16_t>;
    ve->resampler_out->InitializeIfNeeded(PROC_FS_KHZ*1000, fs_hz, 1);
    
    ve->fs_khz = fs_hz/1000;
    
    float omega = PI/2.0f;
    float delta_omega = -(PI/(2.0f*Z_REST_WIN_L1*16.0f));
    for(int i = 0; i < Z_REST_WIN_L1*PROC_FS_KHZ; i++){
        ve->rest.win1[i] = cosf(omega);
        omega = omega + delta_omega;
    }
    
    omega = 0;
    delta_omega = (PI/(2.0f*Z_REST_WIN_L3*16.0f));
    for(int i = 0; i < Z_REST_WIN_L3*PROC_FS_KHZ; i++){
        ve->rest.win3[i] = cosf(omega);
        omega = omega + delta_omega;
    }
    
    ve->e_min_track = E_MIN;
    ve->e_max_track = E_MIN + 10;
    
    init_find_pitch_lags(&ve->pest, PROC_FS_KHZ*1000, 2);    
    ve->pitch_period = 84;
    
    ve->strength = strength;
    if(strength == 0){
        ve->max_pitch_delta = E_MAX_PITCH_DELTA;
    } else if(strength == 1){
        ve->max_pitch_delta = 0;
    }
    
    return (void*)ve;
}

void free_vocoder(void *st)
{
    struct vocoder_effect *ve = (struct vocoder_effect*)st;
    
    delete ve->resampler_in;
    delete ve->resampler_out;
    
    free(ve);
}

static float compress(float x)
{
    float xf = x * 3.0518e-05;
    float y = 1/(exp(-3*xf)+1.0f);
    y = y - 0.5f;
    
    y = y * 32767.0 * 2.0f;
    
    return y;
}

static void find_res(struct vocoder_effect *ve, int16_t x[], int L, int16_t res[], silk_float a[], float *g,  float *tilt)
{
#if !defined(WEBRTC_ARCH_ARM)
    silk_float thrhld, res_nrg;
    silk_float auto_corr[ Z_REST_LPC_ORDER + 1 ];
    silk_float A[         Z_REST_LPC_ORDER ];
    silk_float refl_coef[ Z_REST_LPC_ORDER ];
    silk_float sig[PROC_FS_KHZ*Z_REST_BUF_SZ_MS];
    silk_float Wsig[PROC_FS_KHZ*Z_REST_BUF_SZ_MS];
    silk_float res_buf[PROC_FS_KHZ*Z_REST_BUF_SZ_MS];

    for( int i = 0; i < L; i++ ) {
        ve->rest.buf[(PROC_FS_KHZ*Z_REST_BUF_SZ_MS) - L + i] = x[i];
    }
    
    for( int i = 0; i < PROC_FS_KHZ*Z_REST_BUF_SZ_MS; i++ ) {
        sig[i] = (silk_float)ve->rest.buf[i];
    }
    
    /* Apply Window */
    for(int i = 0; i < Z_REST_WIN_L1*PROC_FS_KHZ; i++){
        Wsig[i] = sig[i] * ve->rest.win1[i];
    }
    for(int i = Z_REST_WIN_L1*PROC_FS_KHZ; i < (Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ; i++){
        Wsig[i] = sig[i];
    }
    for(int i = 0; i < Z_REST_WIN_L3*PROC_FS_KHZ; i++){
        Wsig[(Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ + i] = sig[(Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ +i] * ve->rest.win3[i];
    }
    
    /* Calculate autocorrelation sequence */
    silk_autocorrelation_FLP( auto_corr, Wsig, PROC_FS_KHZ*Z_REST_BUF_SZ_MS, Z_REST_LPC_ORDER + 1 );
    
    /* Add white noise, as fraction of energy */
    auto_corr[ 0 ] += auto_corr[ 0 ] * 1e-3 + 1;
    
    *tilt = auto_corr[ 1 ] / auto_corr[ 0 ];
    
    /* Calculate the reflection coefficients using Schur */
    res_nrg = silk_schur_FLP( refl_coef, auto_corr, Z_REST_LPC_ORDER );
    
    /* Convert reflection coefficients to prediction coefficients */
    silk_k2a_FLP( A, refl_coef, Z_REST_LPC_ORDER );
    
    /* Bandwidth expansion */
    silk_bwexpander_FLP( A, Z_REST_LPC_ORDER, 0.99f );
    
    /*****************************************/
    /* LPC analysis filtering                */
    /*****************************************/
    silk_LPC_analysis_filter_FLP( res_buf, A, sig, PROC_FS_KHZ*Z_REST_BUF_SZ_MS, Z_REST_LPC_ORDER );
    
    for(int i = 0; i < 10*PROC_FS_KHZ; i++){
        res[i] = (int16_t)res_buf[Z_REST_WIN_L1*PROC_FS_KHZ + i];
    }
    
    float e1 = 1;
    for(int i = Z_REST_LPC_ORDER; i < Z_REST_BUF_SZ_MS*PROC_FS_KHZ; i++){
        e1 = e1 + (float)res_buf[i] * (float)res_buf[i];
    }
    *g = sqrtf(e1/(Z_REST_BUF_SZ_MS*PROC_FS_KHZ - Z_REST_LPC_ORDER));
    
    memcpy(a, A, Z_REST_LPC_ORDER*sizeof(silk_float));
    
    memmove(&ve->rest.buf[0], &ve->rest.buf[L], ((PROC_FS_KHZ*Z_REST_BUF_SZ_MS) - L)*sizeof(int16_t));
#else
    opus_int16 Wsig[16*Z_REST_BUF_SZ_MS];
    opus_int16 res_buf[16*Z_REST_BUF_SZ_MS];
    opus_int   scale;
    opus_int32 auto_corr[ Z_REST_LPC_ORDER + 1 ];
    opus_int32 res_nrg;
    opus_int16 rc_Q15[ Z_REST_LPC_ORDER ];
    opus_int32 A_Q24[     Z_REST_LPC_ORDER ];
    opus_int16 A_Q12[     Z_REST_LPC_ORDER ];
    
    for( int i = 0; i < L; i++ ) {
        ve->rest.buf[(PROC_FS_KHZ*Z_REST_BUF_SZ_MS) - L + i] = x[i];
    }
    
    /* Apply Window */
    for(int i = 0; i < Z_REST_WIN_L1*PROC_FS_KHZ; i++){
        Wsig[i] = (int16_t)((float)ve->rest.buf[i] * ve->rest.win1[i]);
    }
    for(int i = Z_REST_WIN_L1*PROC_FS_KHZ; i < (Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ; i++){
        Wsig[i] = ve->rest.buf[i];
    }
    for(int i = 0; i < Z_REST_WIN_L3*PROC_FS_KHZ; i++){
        Wsig[(Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ + i] = (opus_int16)((float)ve->rest.buf[(Z_REST_WIN_L1+Z_REST_WIN_L2)*PROC_FS_KHZ +i] * ve->rest.win3[i]);
    }
        
    /* Calculate autocorrelation sequence */
    silk_autocorr( auto_corr, &scale, Wsig, PROC_FS_KHZ*Z_REST_BUF_SZ_MS, Z_REST_LPC_ORDER + 1, 0 );
    
    /* Add white noise, as fraction of energy */
    auto_corr[ 0 ] = silk_SMLAWB( auto_corr[ 0 ], auto_corr[ 0 ], 65 ) + 1;
    
    *tilt = (float)auto_corr[ 1 ] / (float)auto_corr[ 0 ];
    
    /* Calculate the reflection coefficients using schur */
    res_nrg = silk_schur( rc_Q15, auto_corr, Z_REST_LPC_ORDER );
    
    /* Convert reflection coefficients to prediction coefficients */
    silk_k2a( A_Q24, rc_Q15, Z_REST_LPC_ORDER );
    
    /* Convert From 32 bit Q24 to 16 bit Q12 coefs */
    for( int i = 0; i < Z_REST_LPC_ORDER; i++ ) {
        A_Q12[ i ] = (opus_int16)silk_SAT16( silk_RSHIFT( A_Q24[ i ], 12 ) );
    }
    
    /* Do BWE */
    silk_bwexpander( A_Q12, Z_REST_LPC_ORDER, 64881); // 0.99 in Q16
    
    /* LPC analysis filtering */
    silk_LPC_analysis_filter( res_buf, &ve->rest.buf[0], A_Q12, PROC_FS_KHZ*Z_REST_BUF_SZ_MS, Z_REST_LPC_ORDER, 0 );
    
    for(int i = 0; i < 10*PROC_FS_KHZ; i++){
        res[i] = (int16_t)res_buf[Z_REST_WIN_L1*PROC_FS_KHZ + i];
    }
    
    float e1 = 1;
    for(int i = Z_REST_LPC_ORDER; i < Z_REST_BUF_SZ_MS*PROC_FS_KHZ; i++){
        e1 = e1 + (float)res_buf[i] * (float)res_buf[i];
    }
    *g = sqrtf(e1/(Z_REST_BUF_SZ_MS*PROC_FS_KHZ - Z_REST_LPC_ORDER));
    
    for(int i = 0; i < Z_REST_LPC_ORDER; i++){
        a[i] = (silk_float)A_Q12[i] * 2.4414e-4f;
    }
    
    memmove(&ve->rest.buf[0], &ve->rest.buf[L], ((PROC_FS_KHZ*Z_REST_BUF_SZ_MS) - L)*sizeof(int16_t));
#endif
}

static void lpc_synthesis(struct vocoder_effect *ve, int16_t res[], silk_float a[], silk_float out[], int L)
{
    silk_float pred;
    for(int i = 0; i < L; i++){
        pred = 0;
        for(int j = 0; j < Z_REST_LPC_ORDER; j++){
            pred += (a[j] * ve->lpc_synth_state[ j ]);
        }
        out[i] = (silk_float)res[i] + pred;
        memmove(&ve->lpc_synth_state[1], &ve->lpc_synth_state[0], (Z_REST_LPC_ORDER-1)*sizeof(silk_float));
        ve->lpc_synth_state[0] = out[i];
    }
}

static float energy_based_mix(struct vocoder_effect *ve, int16_t sig[], int L)
{
    float e = 0.0;
    for(int j = 0; j < L; j++){
        e += (float)sig[j]*(float)sig[j];
    }
    e = e/L;
    e = e + 1;
    e = 10.0f * log10f(e);
    
    if(e > ve->e_min_track){
        ve->e_min_track += E_MIN_TRACK_SMTH_UP*(e - ve->e_min_track);
    } else {
        ve->e_min_track += E_MIN_TRACK_SMTH_DOWN*(e - ve->e_min_track);
    }
    if(ve->e_min_track < E_MIN){
        ve->e_min_track = E_MIN;
    }
    
    if(e > ve->e_max_track){
        ve->e_max_track += E_MAX_TRACK_SMTH_UP*(e - ve->e_max_track);
    } else {
        ve->e_max_track += E_MAX_TRACK_SMTH_DOWN*(e - ve->e_max_track);
    }
    
    float energy_mix = (e - ve->e_min_track)/(ve->e_max_track - ve->e_min_track) * 1.0f;
    if(energy_mix > 1.0f){
        energy_mix = 1.0f;
    }
    if(energy_mix < 0.25f){
        energy_mix = 0.25f;
    }
    energy_mix = sqrt(energy_mix);

    return energy_mix;
}

static int median_pitch(struct vocoder_effect *ve)
{
    float mean = 0;
    for(int i = 0; i < E_PL_BUF_SZ; i++){
        mean += (float)ve->pL_buf[i];
    }
    mean = mean / (float)E_PL_BUF_SZ;
    
    int median = 0;
    float min_diff = 1e7;
    float diff;
    for(int i = 0; i < E_PL_BUF_SZ; i++){
        diff = ( mean - (float)ve->pL_buf[i]);
        diff = diff * diff;
        if(diff < min_diff){
            median = ve->pL_buf[i];
            min_diff = diff;
        }
    }
    return median;
}

void vocoder_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct vocoder_effect *ve = (struct vocoder_effect*)st;
    
    int L10 = (ve->fs_khz * 10);
    int N = L_in / L10;
    //if( N * L10 != L_in || L_in > (ve->fs_khz * MAX_L_MS)){
    //    printf("vocoder_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    //}
    
    int L10_out = (L_in * 16) / ve->fs_khz;
    
    size_t n_samp = 0;
    int16_t tmp_buf[L10_out];
    int16_t res[L10_out];
    silk_float filt_out[L10_out];
    silk_float a[Z_REST_LPC_ORDER];
    float g, mix, tilt;
    int pL, median_pL;
    for( int i = 0; i < N; i++){
        ve->resampler_in->Resample( &in[i*L10], L10, &ve->buf[L10_out], L10_out);
        
        find_pitch_lags(&ve->pest, &ve->buf[L10_out], L10_out);
        
        pL = ((ve->pest.pitchL[2] + ve->pest.pitchL[3]) >> 1);
        ve->pL_buf[E_PL_BUF_SZ-1] = pL;
        
        median_pL = median_pitch(ve);
        
        find_res(ve, ve->buf, L10_out, res, a, &g, &tilt);
        
        if(median_pL > 0 && tilt > 0.55f){
            int pitch_delta = median_pL - ve->pitch_period;
            if(pitch_delta < -ve->max_pitch_delta){
                pitch_delta = -ve->max_pitch_delta;
            }
            if(pitch_delta > ve->max_pitch_delta){
                pitch_delta = ve->max_pitch_delta;
            }
            ve->pitch_period += pitch_delta;
            
            mix = (tilt * tilt);
        } else {
            mix = 0.3;
        }
        
        float energy_mix = energy_based_mix(ve, ve->buf, L10_out);
        if(mix > energy_mix){
            mix = energy_mix;
        }
        
        if(mix > ve->mix_smth){
            ve->mix_smth += MIX_SMTH_UP * (mix - ve->mix_smth);
        } else {
            ve->mix_smth += MIX_SMTH_DOWN * (mix - ve->mix_smth);
        }
        
        g = g * 0.0417f * (float)ve->pitch_period;
        int16_t exe;
        // generate pulse train as exitation
        for(int j = 0; j < L10_out; j++){
            if(ve->samples_since_pulse >= ve->pitch_period){
                exe = (int16_t)g;
                ve->samples_since_pulse = 0;
            } else if(ve->samples_since_pulse < 3){
                exe = (int16_t)g;
                ve->samples_since_pulse++;
            } else {
                exe = 0;
                ve->samples_since_pulse++;
            }
            res[j] = (1-ve->mix_smth)*res[j] + ve->mix_smth*exe;
        }
        
        lpc_synthesis(ve, res, a, filt_out, L10_out);
        
        for(int j = 0; j < L10_out; j++){
            tmp_buf[j] = (int16_t)compress(filt_out[j]);
        }
        
        ve->resampler_out->Resample( tmp_buf, L10_out, &out[i*L10], L10);
        
        memmove(ve->pL_buf, &ve->pL_buf[1], (E_PL_BUF_SZ-1) * sizeof(int));
        memmove(ve->buf, &ve->buf[L10_out], L10_out * sizeof(int16_t));
        
        n_samp += L10;
        
        ve->cnt++;
    }
    *L_out = n_samp;
}
