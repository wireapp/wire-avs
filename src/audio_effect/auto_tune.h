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
#ifndef AVS_SRC_AUDIO_EFFECT_AUTO_TUNE_H
#define AVS_SRC_AUDIO_EFFECT_AUTO_TUNE_H

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
#include "biquad.h"

#define ATE_UP_FAC 10
#define ATE_PL_BUF_SZ 3
#define ATE_NUM_BIQUADS 2

#define ATE_BUF_FRAMES 2
#define ATE_EXTRA_BUF_MS 1

#define ATE_NUM_LAGS 64

struct auto_tune_effect {
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler;
    struct pitch_estimator pest;
    struct time_scale tscale;
    struct biquad lp_filt[ATE_NUM_BIQUADS];
    float read_idx;
    float comp_smth;
    float comp_smth_alpha;
    int16_t buf[ATE_UP_FAC * (ATE_BUF_FRAMES * 10 + ATE_EXTRA_BUF_MS) * 48];
    
    int pL_buf[ATE_PL_BUF_SZ];
    float pL_smth;
        
    int prev_idx;
    int strength;
    int lag_hysterisis;
    int pitch_up_bias;
};

#endif
