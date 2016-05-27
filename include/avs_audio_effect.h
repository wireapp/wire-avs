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

#ifndef AVS_AUDIO_EFFECT_H
#define AVS_AUDIO_EFFECT_H

#ifdef __cplusplus
extern "C" {
#endif
    
typedef void* (create_effect_h)(int fs_hz);
typedef void (free_effect_h)(void *st);
typedef void (effect_process_h)(void *st, int16_t in[], int16_t out[], int L);
    
typedef enum {
    AUDIO_EFFECT_CHORUS = 0,
    AUDIO_EFFECT_REVERB,
} audio_effect;

struct aueffect {
    void *effect;
    create_effect_h *e_create_h;
    free_effect_h *e_free_h;
    effect_process_h *e_proc_h;
};
    
int aueffect_alloc(struct aueffect **auep, audio_effect effect_type, int fs_hz);
int aueffect_process(struct aueffect *aue, const int16_t *sampin, int16_t *sampout, size_t sampc);
    
void* create_chorus(int fs_hz);
void free_chorus(void *st);
void chorus_process(void *st, int16_t in[], int16_t out[], int L);    

void* create_reverb(int fs_hz);
void free_reverb(void *st);
void reverb_process(void *st, int16_t in[], int16_t out[], int L);
    
#ifdef __cplusplus
}
#endif

#endif // AVS_AUDIO_EFFECT_H
