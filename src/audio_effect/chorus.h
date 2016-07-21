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
#ifndef AVS_SRC_AUDIO_EFFECT_CHORUS_H
#define AVS_SRC_AUDIO_EFFECT_CHORUS_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include "common_settings.h"

#define UP_FAC 2
#define MAX_L_MS 40
#define MAX_D_MS 100
#define MIN_D_MS 20
#define MAX_A 0.8
#define MIN_A 0.7
#define NUM_SINE_ELEM 4
#define NUM_RAND_ELEM 0

#define RAND_PERIOD_MS 500
#define SINE_PERIOD_MS 1200

struct rand_chorus_elem {
    float d;
    float a;
    float max_d;
    float min_d;
    float max_a;
    float min_a;
    float a_next;
    float d_next;
    int cnt;
    int period_smpls;
    float alpha;
};

struct sine_chorus_elem {
    float d;
    float a;
    float max_d;
    float min_d;
    float max_a;
    float min_a;
    float omega;
    float d_omega;
};

struct chorus_org_effect {
    int fs_khz;
    int16_t buf[(MAX_D_MS+MAX_L_MS)*Z_MAX_FS_KHZ*UP_FAC];
#if NUM_RAND_ELEM
    struct rand_chorus_elem r_elem[NUM_RAND_ELEM];
#endif
#if NUM_SINE_ELEM
    struct sine_chorus_elem s_elem[NUM_SINE_ELEM];
#endif
    webrtc::PushResampler<int16_t> *resampler;
};

struct chorus_alt_effect {
    void* pse1;
    void* pse2;
};

struct chorus_effect {
    int strength;
    void* st;
};

#endif
