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
#ifndef AVS_SRC_AUDIO_EFFECT_NORMALIZER_H
#define AVS_SRC_AUDIO_EFFECT_NORMALIZER_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#include <string>
#include <stdlib.h>

#include "webrtc/common_audio/resampler/include/push_resampler.h"

#ifdef  __cplusplus
extern "C"
{
#endif

#include "main.h"

#ifdef  __cplusplus
}
#endif

#include "float/main_FLP.h"
#include "common_settings.h"

#define NE_LOG2_BUF_SIZE   7 // 128 = 1.28 sec
#define NE_BUF_SIZE        (1 << NE_LOG2_BUF_SIZE)
#define NE_BUF_MASK        (NE_BUF_SIZE - 1)

#define NE_MAX_FS_KHZ     48
#define NE_VAD_FS_KHZ     16

#define NE_HYSTERESIS_DB 3.0f
#define NE_MAX_INCR_DB 0.5f
#define NE_MAX_DECR_DB -1.0f
#define NE_MAX_GAIN_DB 9.0f
#define NE_TARGET_LVL_DB 70.0f
#define NE_HIST_LEN 100

#define NE_VAD_THRES 128
#define NE_PERCENTILE 0.1f

#define NE_UPDATE_RANGE 15.0f
#define NE_ALPHA_MAX_UP 0.1f
#define NE_ALPHA_MAX_DOWN 0.0025f

#define NE_MAX_SQUELCH_GAIN_DB 9.0f

struct normalizer_effect {
    int fs_khz;
    webrtc::PushResampler<int16_t> *resampler;
    silk_encoder_state silk_enc;
    int vad_buf[NE_BUF_SIZE];
    int nrg_buf[NE_BUF_SIZE];
    int16_t sig_buf[NE_MAX_FS_KHZ];
    int buf_idx;
    int frame_cnt;
    float Target_vol_db;
    float Target_gain_db;
    float Squelch_gain_db;
    int prev_max_abs;
    float gain;
    float speechLvl;
    float maxLvl;
    
    bool squelch_enabled;
};

#endif
