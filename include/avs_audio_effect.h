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
    
typedef void* (create_effect_h)(int fs_hz, int strength);
typedef void (free_effect_h)(void *st);
typedef void (effect_process_h)(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
typedef void (effect_length_h)(void *st, int *length_mod_Q10);
    
typedef enum {
    AUDIO_EFFECT_CHORUS = 0,
    AUDIO_EFFECT_CHORUS_MIN,
    AUDIO_EFFECT_CHORUS_MAX,
    AUDIO_EFFECT_REVERB,
    AUDIO_EFFECT_REVERB_MIN,
    AUDIO_EFFECT_REVERB_MID,
    AUDIO_EFFECT_REVERB_MAX,
    AUDIO_EFFECT_PITCH_UP_SHIFT,
    AUDIO_EFFECT_PITCH_UP_SHIFT_MIN,
    AUDIO_EFFECT_PITCH_UP_SHIFT_MED,
    AUDIO_EFFECT_PITCH_UP_SHIFT_MAX,
    AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE,
    AUDIO_EFFECT_PITCH_DOWN_SHIFT,
    AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN,
    AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED,
    AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX,
    AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE,
    AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN,
    AUDIO_EFFECT_PACE_DOWN_SHIFT_MED,
    AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX,
    AUDIO_EFFECT_PACE_UP_SHIFT_MIN,
    AUDIO_EFFECT_PACE_UP_SHIFT_MED,
    AUDIO_EFFECT_PACE_UP_SHIFT_MAX,
    AUDIO_EFFECT_REVERSE,
    AUDIO_EFFECT_VOCODER_MED,
    AUDIO_EFFECT_NONE,
} audio_effect;

struct aueffect {
    void *effect;
    create_effect_h *e_create_h;
    free_effect_h *e_free_h;
    effect_process_h *e_proc_h;
    effect_length_h *e_length_h;
};
    
int aueffect_alloc(struct aueffect **auep, audio_effect effect_type, int fs_hz);
int aueffect_process(struct aueffect *aue, const int16_t *sampin, int16_t *sampout, size_t n_sampin, size_t *n_sampout);
int aueffect_length_modification(struct aueffect *aue, int *length_modification_q10);
    
void* create_chorus(int fs_hz, int strength);
void free_chorus(void *st);
void chorus_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
    
void* create_reverb(int fs_hz, int strength);
void free_reverb(void *st);
void reverb_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
    
void* create_pitch_up_shift(int fs_hz, int strength);
void* create_pitch_down_shift(int fs_hz, int strength);
void free_pitch_shift(void *st);
void pitch_shift_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
    
void* create_pace_up_shift(int fs_hz, int strength);
void* create_pace_down_shift(int fs_hz, int strength);
void free_pace_shift(void *st);
void pace_shift_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
void pace_shift_length_factor(void *st, int *length_mod_Q10);
    
void* create_vocoder(int fs_hz, int strength);
void free_vocoder(void *st);
void vocoder_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);

void* create_pass_through(int fs_hz, int strength);
void free_pass_through(void *st);
void pass_through_process(void *st, int16_t in[], int16_t out[], size_t L_in, size_t *L_out);
    
typedef void (effect_progress_h)(int progress, void *arg);
int apply_effect_to_wav(const char* wavIn, const char* wavOut, audio_effect effect_type, bool reduce_noise, effect_progress_h* progress_h, void *arg);
int apply_effect_to_pcm(const char* pcmIn, const char* pcmOut, int fs_hz, audio_effect effect_type, bool reduce_noise, effect_progress_h* progress_h, void *arg);
    
#ifdef __cplusplus
}
#endif

#endif // AVS_AUDIO_EFFECT_H
