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
#ifndef AVS_SRC_AUDIO_EFFECT_FIND_PITCH_LAGS_H
#define AVS_SRC_AUDIO_EFFECT_FIND_PITCH_LAGS_H

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

#define Z_LPC_ORDER 10
#define Z_NB_SUBFR 4
#define Z_PEST_BUF_SZ_MS 40
#define Z_WIN_LEN_MS 2
#define Z_FS_KHZ 16

struct pitch_estimator {
    int16_t buf[Z_MAX_FS_KHZ*40];
    webrtc::PushResampler<int16_t> *resampler;
    opus_int pitchL[Z_NB_SUBFR];
    opus_int LTPCorr_Q15;
    int fs_khz;
    int complexity;
    bool voiced;
};

void init_find_pitch_lags(struct pitch_estimator *pest, int fs_hz, int complexity);
void free_find_pitch_lags(struct pitch_estimator *pest);

void find_pitch_lags(struct pitch_estimator *pest, int16_t x[], int L);

#endif