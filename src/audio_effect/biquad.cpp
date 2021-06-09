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

#include <re.h>
#include "biquad.h"
#include "avs_audio_effect.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

void biquad(struct biquad *bq, float a[2], float b[3], int16_t x[], int16_t y[], int L)
{
    float w0;
    float out;
    
    for(int i = 0; i < L; i++){
        w0 = -a[0]*bq->w1;
        w0 = w0 -a[1]*bq->w2;
        w0 = w0 + (float)x[i];
        out = b[0]*w0;
        out = out + b[1]*bq->w1;
        y[i] = (int16_t)(out + b[2]*bq->w2);
        bq->w2 = bq->w1;
        bq->w1 = w0;
    }
}

