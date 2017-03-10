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
#ifndef AVS_SRC_AUDIO_EFFECT_PITCH_CYCLER_H
#define AVS_SRC_AUDIO_EFFECT_PITCH_CYCLER_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#include "webrtc/common_audio/resampler/include/push_resampler.h"

#include "find_pitch_lags.h"
#include "time_scale.h"

#define PCE_UP_FAC 5
#define PCE_EXTRA_UP 2
#define PCE_BUF_FRAMES 1
#define PCE_EXTRA_BUF_MS 1

#define PCE_COMP_MAX 1.5
#define PCE_COMP_MIN 0.7

struct pitch_cycler_effect {
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler;
    webrtc::PushResampler<int16_t> *resampler_out;
    struct pitch_estimator pest;
    struct time_scale tscale;
    float read_idx;
    int16_t buf[PCE_UP_FAC * PCE_EXTRA_UP * (PCE_BUF_FRAMES * 10 + PCE_EXTRA_BUF_MS) * 48];
    float comp;
    float used_comp_delta;
    float comp_delta;
    int strength;
};

#endif
