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
#ifndef AVS_SRC_AUDIO_EFFECT_PITCH_SHIFT_H
#define AVS_SRC_AUDIO_EFFECT_PITCH_SHIFT_H

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

struct up_down_params{
    int up;
    int down;
};

struct pitch_shift_effect {
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler;
    struct pitch_estimator pest;
    struct time_scale tscale;
    int up;
    int down;
    int cnt;
};

#endif
