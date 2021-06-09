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
#ifndef AVS_SRC_AUDIO_EFFECT_REVERB_H
#define AVS_SRC_AUDIO_EFFECT_REVERB_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#define NUM_AR 0
#define NUM_AP 3

#define MAX_NUM_AR 4
#define MAX_NUM_AP 3

#define LOG2_MAX_D      13
#define MAX_D           (1 << LOG2_MAX_D)
#define MASK            (MAX_D - 1)

#define MAX_IMP_MS 100

struct ar_d_params{
    float b1;
    float ad;
    float d_ms;
};

struct ap_d_params{
    float c;
    float d_ms;
};

struct ar_d {
    float state[MAX_D];
    float vd;
    int d;
    float b1;
    float ad;
    int idx;
};

struct ap_d {
    float state[MAX_D];
    int d;
    float c;
    int idx;
    int max_imp;
};

struct reverb_effect {
    struct ar_d ar[MAX_NUM_AR];
    struct ap_d ap[MAX_NUM_AP];
    int fs_khz;
    float pre_sc;
    float post_sc;
};

#endif
