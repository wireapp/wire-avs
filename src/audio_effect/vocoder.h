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
#ifndef AVS_SRC_AUDIO_EFFECT_VOCODER_H
#define AVS_SRC_AUDIO_EFFECT_VOCODER_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include "main.h"
#include "float/main_FLP.h"
#include "common_settings.h"
#include "find_pitch_lags.h"

#define WIN_L1 20
#define WIN_L2 10
#define WIN_L3 10

#define Z_REST_LPC_ORDER 16
#define Z_REST_BUF_SZ_MS 20
#define Z_REST_WIN_L1 5
#define Z_REST_WIN_L2 10
#define Z_REST_WIN_L3 5

#define PROC_FS_KHZ 16

#define MIX_SMTH_UP 0.5f
#define MIX_SMTH_DOWN 0.6f

#define E_MIN_TRACK_SMTH_UP 0.002f
#define E_MIN_TRACK_SMTH_DOWN 0.2f
#define E_MIN                 20.0f

#define E_MAX_TRACK_SMTH_UP 0.1f
#define E_MAX_TRACK_SMTH_DOWN 0.005f

#define E_PL_BUF_SZ 3

#define E_BUF_FRAMES 2

#define E_MAX_PITCH_DELTA 2

struct residual_estimator {
    float win1[Z_MAX_FS_KHZ*WIN_L1];
    float win3[Z_MAX_FS_KHZ*WIN_L3];
    int16_t buf[Z_MAX_FS_KHZ*Z_REST_BUF_SZ_MS];
};

struct vocoder_effect {
    silk_float lpc_synth_state[Z_REST_LPC_ORDER];
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler_in;
    webrtc::PushResampler<int16_t> *resampler_out;
    struct residual_estimator rest;
    struct pitch_estimator pest;
    int16_t buf[E_BUF_FRAMES * 10 * 48];
    int pL_buf[E_PL_BUF_SZ];
    int cnt;
    int samples_since_pulse;
    float mix_smth;
    float e_min_track;
    float e_max_track;
    int pitch_period;
    int max_pitch_delta;
    int strength;
};

#endif
