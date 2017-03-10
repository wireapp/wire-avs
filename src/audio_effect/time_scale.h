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
#ifndef AVS_SRC_AUDIO_EFFECT_TIME_SCALE_H
#define AVS_SRC_AUDIO_EFFECT_TIME_SCALE_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#define TS_LOG2_MAX_D      14
#define TS_MAX_D           (1 << TS_LOG2_MAX_D)
#define TS_MASK            (TS_MAX_D - 1)

#define MAX_L_MS Z_MAX_FS_KHZ*10

struct time_scale {
    int fs_in_khz;
    int fs_out_khz;
    int16_t buf[TS_MAX_D];
    int read_idx;
    int write_idx;
    int maxL;
    int minL;
    bool voiced;
    float nc_bufsz_fac;
};

void time_scale_init(struct time_scale* ts, int fs_in_hz, int fs_out_hz);

void time_scale_insert(struct time_scale* ts,
                       int16_t in[],
                       int N,
                       int pL_max,
                       int pL_min,
                       bool voiced);

void time_scale_extract(struct time_scale* ts,
                        int16_t out[],
                        int N);

#endif
