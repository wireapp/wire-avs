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
#ifndef AVS_SRC_AUDIO_EFFECT_HARMONIZER_H
#define AVS_SRC_AUDIO_EFFECT_HARMONIZER_H

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

#define HMZ_UP_FAC 10
#define HMZ_PL_BUF_SZ 3
#define HMZ_NUM_BIQUADS 2

#define HMZ_BUF_FRAMES 2
#define HMZ_EXTRA_BUF_MS 1

#define HMZ_NUM_LAGS 64

#define HMZ_NUM_CHANNELS 3

struct harm_channel {
    float read_idx;
    float comp_smth;
    struct time_scale tscale;
};

struct harmonizer_effect {
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler;
    struct pitch_estimator pest;
    struct biquad lp_filt[HMZ_NUM_BIQUADS];
    struct harm_channel hm_ch[HMZ_NUM_CHANNELS];
    float read_idx_ch1;
    float comp_smth;
    float comp_smth_alpha;
    int16_t buf[HMZ_UP_FAC * (HMZ_BUF_FRAMES * 10 + HMZ_EXTRA_BUF_MS) * 48];
    
    int pL_buf[HMZ_PL_BUF_SZ];
    float pL_smth;
        
    int prev_idx;
    int strength;
    int lag_hysterisis;
    int harm_shift;
};

#endif
