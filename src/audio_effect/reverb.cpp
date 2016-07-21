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
#include "reverb.h"
#include "avs_audio_effect.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

static void init_ar_d(struct ar_d *ar, float ad, float b1, int d)
{
    memset(ar->state, 0, sizeof(ar->state));
    ar->ad = ad;
    ar->b1 = b1;
    ar->d = d;
    ar->idx = 0;
}

static void ar_d(struct ar_d *ar, float x, float y[])
{
    float w0, wd, v0;
    int idx;
    
    idx = (ar->idx - ar->d) & MASK;
    wd = ar->state[ idx ];
    v0 = wd;//0.5f * wd + 0.5f * ar->vd;
    ar->vd = wd;
    w0 = x + v0 * ar->ad;
    ar->state[ar->idx] = w0;
    ar->idx = (ar->idx + 1) & MASK;
    y[0] = ar->b1 * w0;
}

static void init_allpass_d(struct ap_d *ap, float c, int d)
{
    memset(ap->state, 0, sizeof(ap->state));
    ap->c = c;
    ap->d = d;
    ap->idx = 0;
}

static void allpass_d_alt(struct ap_d *ap, float x, float y[])
{
    float w0, wd, tmp, cc;
    int d, idx;
    
    tmp = -ap->c * x;
    
    d = ap->d;
    cc = ap->c;
    while(d < ap->max_imp){
        idx = (ap->idx - d) & MASK;
        cc = cc * ap->c;
        d = d + ap->d;
        wd = ap->state[ idx ];
        tmp += cc * wd;
    }
    ap->state[ap->idx] = x;
    ap->idx = (ap->idx + 1) & MASK;
    
    y[0] = tmp;
}

static void allpass_d(struct ap_d *ap, float x, float y[])
{
    float w0, wd;
    int idx;
    
    idx = (ap->idx - ap->d) & MASK;
    wd = ap->state[ idx ];
    w0 = x + wd * ap->c;
    ap->state[ap->idx] = w0;
    ap->idx = (ap->idx + 1) & MASK;
    y[0] = -ap->c * w0 + wd;
}

void* create_reverb(int fs_hz, int strength)
{
    struct ar_d_params ar_params[MAX_NUM_AR] =
    {
        {0.7f, 0.75f, 19.06f},
        {0.7f, 0.75f, 21.56f},
        {0.7f, 0.75f, 23.75f},
        {0.7f, 0.75f, 25.62f},
    };
    
    struct ap_d_params ap_params_min[MAX_NUM_AP] =
    {
        {0.83f, 84.3f},
        {0.81f, 96.8f},
        {0.85f, 73.2f},
    };
    
    struct ap_d_params ap_params_mid[MAX_NUM_AP] =
    {
        {0.93f, 44.3f},
        {0.91f, 56.8f},
        {0.95f, 37.2f},
    };

    struct ap_d_params ap_params_max[MAX_NUM_AP] =
    {
        {0.93f, 84.3f},
        {0.91f, 96.8f},
        {0.95f, 73.2f},
    };
    
    struct reverb_effect* rvb = (struct reverb_effect*)calloc(sizeof(struct reverb_effect),1);
    
    float d, fs_khz = fs_hz/1000.0f;

    for(int i = 0; i < NUM_AR; i++){
        d = ar_params[i].d_ms * fs_khz;
        init_ar_d(&rvb->ar[i], ar_params[i].b1, ar_params[i].ad, (int)d);
    }
    if(strength < 1){
        for(int i = 0; i < NUM_AP; i++){
            d = ap_params_min[i].d_ms * fs_khz;
            init_allpass_d(&rvb->ap[i], ap_params_min[i].c, (int)d);
            rvb->ap[i].max_imp = (fs_khz * MAX_IMP_MS);
        }

    }
    if(strength == 1){
        for(int i = 0; i < NUM_AP; i++){
            d = ap_params_mid[i].d_ms * fs_khz;
            init_allpass_d(&rvb->ap[i], ap_params_mid[i].c, (int)d);
            rvb->ap[i].max_imp = (fs_khz * MAX_IMP_MS);
        }
    }
    if(strength > 1){
        for(int i = 0; i < NUM_AP; i++){
            d = ap_params_max[i].d_ms * fs_khz;
            init_allpass_d(&rvb->ap[i], ap_params_max[i].c, (int)d);
            rvb->ap[i].max_imp = (fs_khz * MAX_IMP_MS);
        }
    }
    
    rvb->pre_sc = 1.0f / 32767.0f;
    rvb->pre_sc = rvb->pre_sc * 0.5;
    rvb->post_sc = 32767.0f * 4.0f;
    
    return (void*)rvb;
}

void free_reverb(void *st)
{
    struct reverb_effect *rvb = (struct reverb_effect*)st;
    
    free(rvb);
}

static float compress(float x)
{
    float y = 1/(exp(-3*x)+1.0f);
    y = y - 0.5f;
    
    return y;
}

void reverb_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    float x, y, tmp, sc = 1.0f / 32767.0f;
    struct reverb_effect *rvb = (struct reverb_effect*)st;
    
    for( size_t i = 0; i < L_in; i++){
        x = (float)in[i];
        x = x * rvb->pre_sc;
        
#if NUM_AR
        y = 0.0f;
#else
        y = x;
#endif
        for(int i = 0; i < NUM_AR; i++){
            ar_d(&rvb->ar[i], x, &tmp);
            y = y + tmp;
        }
        for(int i = 0; i < NUM_AP; i++){
            allpass_d(&rvb->ap[i], y, &y);
        }
        y = 0.7f*y + x;
        y = compress(y);
        y = y * rvb->post_sc;
        
        out[i] = (int16_t)y;
    }
    *L_out = L_in;
}
