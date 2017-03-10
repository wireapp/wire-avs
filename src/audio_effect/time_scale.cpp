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

#include "time_scale.h"
#include <math.h>

static void time_scale_remove_one(struct time_scale *ts, int best_d, int L)
{
    int buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
    if(best_d > buf_smpls){
        return;
    }
    if(buf_smpls < (2*L + best_d)){
        L = (buf_smpls - best_d) >> 1;
    }
    int L1 = L - ts->fs_out_khz;
    int16_t buf1[2*L];
    int16_t buf2[2*L];
    int idx1 = (ts->write_idx - 2*L) & TS_MASK;
    int idx2 = (ts->write_idx - 2*L - best_d) & TS_MASK;
    
    for(int i = 0; i < 2*L; i++){
        buf1[i] = ts->buf[idx1];
        buf2[i] = ts->buf[idx2];
        idx1 = (idx1 + 1) & TS_MASK;
        idx2 = (idx2 + 1) & TS_MASK;
    }
    
    idx1 = (ts->write_idx - 2*L + L1) & TS_MASK;
    idx2 = (ts->write_idx - 2*L + L1 - best_d) & TS_MASK;
    
    int L2 = ts->fs_out_khz*2;
    float win_up, win_down = 1.0f;
    float win_delta = 1.0f/L2; // ToDo use better window
    float tmp;
    for(int i = 0; i < L2; i++){
        win_down = win_down - win_delta;
        win_up = 1.0f - win_down;
        tmp = (float)buf2[i+L1] * win_down;
        tmp += (float)buf1[i+L1] * win_up;
        ts->buf[idx2] = (int16_t)tmp;
        idx1 = (idx1 + 1) & TS_MASK;
        idx2 = (idx2 + 1) & TS_MASK;
    }
    for(int i = 0; i < L1; i++){
        ts->buf[idx2] = buf1[i+L1+L2];
        idx1 = (idx1 + 1) & TS_MASK;
        idx2 = (idx2 + 1) & TS_MASK;
    }
    
    ts->write_idx = idx2;
}

static void time_scale_insert_one(struct time_scale *ts, int best_d, int L)
{
    int L1 = ts->fs_out_khz*1;
    int buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
    
    if( buf_smpls < L + L1){
        L = buf_smpls - L1;
    }
    
    int16_t buf1[L+L1];
    int idx1 = (ts->write_idx - L - L1) & TS_MASK;
    int idx2 = (ts->write_idx - L - best_d - L1) & TS_MASK;
    for(int i = 0; i < (L + L1); i++){
        buf1[i] = ts->buf[idx1];
        idx1 = (idx1 + 1) & TS_MASK;
    }
    int16_t buf2[best_d+L1];
    for(int i = 0; i < (best_d + L1); i++){
        buf2[i] = ts->buf[idx2];
        idx2 = (idx2 + 1) & TS_MASK;
    }
    
    idx1 = (ts->write_idx - L - L1) & TS_MASK;
    int L2 = ts->fs_out_khz*2;
    float win_up, win_down = 1.0f;
    float win_delta = 1.0f/L2; // ToDo use better window
    float tmp;
    for(int i = 0; i < L2; i++){
        win_down = win_down - win_delta;
        win_up = 1.0f - win_down;
        tmp = (float)buf1[i] * win_down;
        tmp += (float)buf2[i] * win_up;
        ts->buf[idx1] = (int16_t)tmp;
        idx1 = (idx1 + 1) & TS_MASK;
    }
    idx2 = (idx1 - best_d) & TS_MASK;
    for( int i = 0; i < best_d - L1; i++){
        ts->buf[idx1] = buf2[i + L2];
        idx1 = (idx1 + 1) & TS_MASK;
        idx2 = (idx2 + 1) & TS_MASK;
    }
    for(int i = 0; i < L; i++){
        ts->buf[idx1] = buf1[i + L1];
        idx1 = (idx1 + 1) & TS_MASK;
    }
    ts->write_idx = idx1;
}

void time_scale_init(struct time_scale* ts, int fs_in_hz, int fs_out_hz)
{
    ts->read_idx = 0;
    ts->write_idx = 0;
    ts->fs_in_khz = fs_in_hz/1000;
    ts->fs_out_khz = fs_out_hz/1000;
    ts->nc_bufsz_fac = 0.015f / (float)ts->fs_out_khz;
}

void time_scale_insert(struct time_scale* ts,
                       int16_t in[],
                       int N,
                       int pL_max,
                       int pL_min,
                       bool voiced)
{
    ts->maxL = pL_max;
    ts->minL = pL_min;
    ts->voiced = voiced;
 
    for( int i = 0; i < N; i++){
        ts->buf[ts->write_idx] = in[i];
        ts->write_idx = (ts->write_idx + 1) & TS_MASK;
    }
}

void time_scale_extract(struct time_scale* ts,
                        int16_t out[],
                        int N)
{
    int pitchL, maxL, minL;
    int idx1, idx2;
    float best_nc;
    
    int best_d = -1;
    int L10 = (ts->fs_in_khz*10);
    if(ts->voiced){
        best_nc = -1.0;
        for(int d = ts->minL; d < (ts->maxL + 1); d++){
            idx1 = (ts->write_idx - 2*L10) & TS_MASK;
            idx2 = (ts->write_idx - 2*L10 - d) & TS_MASK;
            float cc, e1, e2, nc;
            e1 = 0;
            e2 = 0;
            cc = 0;
            for(int i = 0; i < 2*L10; i++){
                cc += ts->buf[idx2] * ts->buf[idx1];
                e1 += ts->buf[idx1] * ts->buf[idx1];
                e2 += ts->buf[idx2] * ts->buf[idx2];
                idx1 = (idx1 + 1) & TS_MASK;
                idx2 = (idx2 + 1) & TS_MASK;
            }
            nc = cc / sqrt(e1 * e2);
            if(nc > best_nc){
                best_d = d;
                best_nc = nc;
            }
        }
        if(best_d == -1){
            best_d = L10;
            best_nc = 0.1f;
        }
    } else {
        best_d = L10;
        best_nc = 0.1f;
    }
    
    int buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
    
    if(buf_smpls > ts->fs_out_khz*45){
        /* Buffer is growing shorten the signal */
        while(buf_smpls > ts->fs_out_khz*45){
            time_scale_remove_one(ts, best_d, 2*L10);
            buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
        }
    } else {
        /* Buffer is shrinking expand the signal */
        if(buf_smpls < ts->fs_out_khz*35){
            float nc_thres = 0.75 - (float)(ts->fs_out_khz*35 - buf_smpls)*ts->nc_bufsz_fac;
            if(best_nc > nc_thres){
                int n = (int)(((float)(10*ts->fs_out_khz) / (float)best_d) + 0.5f);
                for(int i = 0; i < n; i++){
                    time_scale_insert_one(ts, best_d, 2*L10);
                }
            }
        }
        
        buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
        while(buf_smpls < (N + 3*ts->fs_out_khz) ){
            time_scale_insert_one(ts, best_d, ts->fs_out_khz);
            
            buf_smpls = (ts->write_idx - ts->read_idx) & TS_MASK;
        }
    }
    for( int i = 0; i < N; i++){
        out[i] = ts->buf[ts->read_idx];
        ts->read_idx = (ts->read_idx + 1) & TS_MASK;
    }
}
