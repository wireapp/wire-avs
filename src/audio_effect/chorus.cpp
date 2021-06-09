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
#include "chorus.h"
#include "avs_audio_effect.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

#define PI 3.1415926536

static void* create_chorus_org(int fs_hz, int strength)
{
    int period_len;
    struct chorus_org_effect* cho = (struct chorus_org_effect*)calloc(sizeof(struct chorus_org_effect),1);
    
    cho->resampler = new webrtc::PushResampler<int16_t>;
    cho->resampler->InitializeIfNeeded(fs_hz, fs_hz*UP_FAC, 1);
    cho->fs_khz = (fs_hz/1000);
    
    float max_a = MAX_A;
    float max_d_ms = MAX_D_MS;
    if(strength < 1){
        max_a = max_a * 0.5f;
        max_d_ms = max_d_ms * 0.75f;
    }
    float min_a = MIN_A;
    float min_d_ms = MIN_D_MS;
    if(strength < 1){
        max_a = max_a * 0.5f;
        min_d_ms = min_d_ms * 0.75f;
    }
    
#if NUM_RAND_ELEM
    period_len = (cho->fs_khz * RAND_PERIOD_MS);
    int offset = period_len / NUM_RAND_ELEM;
    
    for(int j = 0; j < NUM_RAND_ELEM; j++){
        cho->r_elem[j].max_d = max_d_ms * cho->fs_khz;
        cho->r_elem[j].min_d = min_d_ms * cho->fs_khz;
        cho->r_elem[j].max_a = max_a;
        cho->r_elem[j].min_a = min_a;
        cho->r_elem[j].cnt = j*offset;
        cho->r_elem[j].period_smpls = period_len;
        //cho->elem[j].alpha = 0.001; // to do depend on fs
        cho->r_elem[j].alpha = 0.0001;
    }
#endif
    
#if NUM_SINE_ELEM
    period_len = (cho->fs_khz * SINE_PERIOD_MS);
    float d_omega = (2*PI)/period_len;
    
    for(int j = 0; j < NUM_SINE_ELEM; j++){
        cho->s_elem[j].max_d = max_d_ms * cho->fs_khz;
        cho->s_elem[j].min_d = min_d_ms * cho->fs_khz;
        cho->s_elem[j].max_a = max_a;
        cho->s_elem[j].min_a = min_a;
        cho->s_elem[j].d_omega = d_omega;
        cho->s_elem[j].omega = (PI/2)*j;
    }
#endif
    
    return (void*)cho;
}

static void free_chorus_org(void *st)
{
    struct chorus_org_effect *cho = (struct chorus_org_effect*)st;
    
    delete cho->resampler;
    free(cho);
}

static int16_t update_rand_chorus_elem(struct rand_chorus_elem *r_elem, int16_t buf[], int up_fac)
{
    r_elem->cnt++;
    if(r_elem->cnt > r_elem->period_smpls){
        r_elem->d_next = r_elem->min_d + ((float)rand()/RAND_MAX)*(r_elem->max_d-r_elem->min_d);
        r_elem->a_next = r_elem->min_a + ((float)rand()/RAND_MAX)*(r_elem->max_a-r_elem->min_a);
        r_elem->cnt = 0;
    }
    r_elem->d += (r_elem->d_next - r_elem->d) * r_elem->alpha;
    r_elem->a += (r_elem->a_next - r_elem->a) * r_elem->alpha;

    int d = (int)(r_elem->d * (float)up_fac);
    int16_t ret = (int16_t)((float)buf[-d] * r_elem->a);
    
    return ret;
}

static int16_t update_sine_chorus_elem(struct sine_chorus_elem *s_elem, int16_t buf[], int up_fac)
{
    s_elem->omega += s_elem->d_omega;
    s_elem->omega = fmod(s_elem->omega, 2*3.1415926536);
    float s = (sin(s_elem->omega) + 1)/2.0;
    s_elem->d = s_elem->min_d + s*(s_elem->max_d-s_elem->min_d);
    s_elem->a = s_elem->min_a + (1-s)*(s_elem->max_a-s_elem->min_a);

    int d = (int)(s_elem->d * (float)up_fac);
    int16_t ret = (int16_t)((float)buf[-d] * s_elem->a);
    
    return ret;
}

static float compress(float x)
{
    float y = 1/(exp(-3*x)+1.0f);
    y = y - 0.5f;
    
    return y;
}

static void chorus_process_org(void *st, int16_t in[], int16_t out[], size_t L)
{
    struct chorus_org_effect *cho = (struct chorus_org_effect*)st;
    
    int32_t tmp = 0;
    int16_t *ptr;
    int hist_size = (MAX_D_MS * cho->fs_khz) * UP_FAC;
    float y, sc1 = 1.0f/(32768.0f*2.0f), sc2 = (32768.0f*2.0f);
    
    int L10 = (cho->fs_khz * 10);
    int N = (int)L / L10;
    if( N * L10 != L || L > (cho->fs_khz * MAX_L_MS)){
        error("chorus_process needs 10 ms chunks max %d ms \n", MAX_L_MS);
    }
    
    for( int i = 0; i < N; i++){
        cho->resampler->Resample( &in[i*L10], L10, &cho->buf[hist_size + i*L10*UP_FAC], L10*UP_FAC);
    }
            
    ptr = &cho->buf[hist_size];
    for(size_t i = 0; i < L; i++){
        tmp = ptr[i * UP_FAC];

#if NUM_RAND_ELEM
        for(int j = 0; j < NUM_RAND_ELEM; j++){
            tmp += update_rand_chorus_elem(&cho->r_elem[j], &ptr[i * UP_FAC], UP_FAC);
        }
#endif

#if NUM_SINE_ELEM
        for(int j = 0; j < NUM_SINE_ELEM; j++){
            tmp += update_sine_chorus_elem(&cho->s_elem[j], &ptr[i * UP_FAC], UP_FAC);
        }
#endif
        
        y = (float)tmp * sc1;
        y = compress(y);
        y = y * sc2;
        
        out[i] = (int16_t)y;
    }
    
    memmove(cho->buf, &cho->buf[L * UP_FAC], hist_size*sizeof(int16_t)); // todo make circular
}

static void* create_chorus_alt(int fs_hz, int strength)
{
    struct chorus_alt_effect* cho = (struct chorus_alt_effect*)calloc(sizeof(struct chorus_alt_effect),1);
    
    cho->pse1 = create_pitch_up_shift(fs_hz, 0);
    cho->pse2 = create_pitch_down_shift(fs_hz, 0);
    
    return (void*)cho;
}

static void free_chorus_alt(void *st)
{
    struct chorus_alt_effect *cho = (struct chorus_alt_effect*)st;
    
    free_pitch_shift(cho->pse1);
    free_pitch_shift(cho->pse2);
    free(cho);
}

static void chorus_process_alt(void *st, int16_t in[], int16_t out[], size_t L)
{
    struct chorus_alt_effect *cho = (struct chorus_alt_effect*)st;
    int16_t out1[L], out2[L];
    int32_t tmp;
    float y, sc1 = 1.0f/(32768.0f*2.0f), sc2 = (32768.0f*2.0f);
    
    size_t L_out;
    pitch_shift_process(cho->pse1, in, out1, L, &L_out);
    pitch_shift_process(cho->pse2, in, out2, L, &L_out);
    
    for(int i = 0; i < L; i++){
        tmp = in[i] + out1[i] + out2[i];
        y = (float)tmp * sc1;
        y = compress(y);
        y = y * sc2;
        out[i] = (int16_t)y;
    }
}

void* create_chorus(int fs_hz, int strength)
{
    struct chorus_effect* cho = (struct chorus_effect*)calloc(sizeof(struct chorus_effect),1);

    cho->strength = strength;
    if(strength > 0){
        cho->st = create_chorus_org(fs_hz, strength - 1);
    } else {
        cho->st = create_chorus_alt(fs_hz, 0);
    }
    return (void*)cho;
}

void free_chorus(void *st)
{
    struct chorus_effect *cho = (struct chorus_effect*)st;

    if(cho->strength > 0){
        free_chorus_org(cho->st);
    } else {
        free_chorus_alt(cho->st);
    }
    free(cho);
}


void chorus_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    struct chorus_effect *cho = (struct chorus_effect*)st;
    
    if(cho->strength > 0){
        chorus_process_org(cho->st, in, out, L_in);
    } else {
        chorus_process_alt(cho->st, in, out, L_in);
    }
    *L_out = L_in;
}
