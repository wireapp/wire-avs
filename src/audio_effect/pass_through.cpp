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
#include "avs_audio_effect.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

void* create_pass_through(int fs_hz, int strength)
{
    int16_t* dummy = (int16_t*)calloc(sizeof(int16_t),1);
    
    return (void*)dummy;
}

void free_pass_through(void *st)
{
    free(st);
    return;
}

void pass_through_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out)
{
    for(int i = 0; i < L_in; i++){
        out[i] = in[i];
    }
    *L_out = L_in;
}


