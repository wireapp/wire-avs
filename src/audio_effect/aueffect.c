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

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

static void aueffect_destructor(void *arg)
{
    struct aueffect *aue = (struct aueffect *)arg;
    
    debug("aueffect_destructor: %p free_h: %p\n",aue, aue->e_free_h);

    if (aue->e_free_h)
	    aue->e_free_h(aue->effect);
}

int aueffect_alloc(struct aueffect **auep,
                   enum audio_effect effect_type,
                   int fs_hz)
{
    struct aueffect *aue;
    int err = 0, strength = 0;
    
    if (!auep) {
        return EINVAL;
    }
    
    debug("voe: aueffect_alloc: \n");
    
    aue = (struct aueffect *)mem_zalloc(sizeof(*aue), aueffect_destructor);
    if (!aue)
        return ENOMEM;
    
    switch ((enum audio_effect)effect_type) {

        case AUDIO_EFFECT_CHORUS_MAX:
            strength++;
        case AUDIO_EFFECT_CHORUS_MED:
            strength++;
        case AUDIO_EFFECT_CHORUS_MIN:
        case AUDIO_EFFECT_CHORUS:
        case AUDIO_EFFECT_REVERSE:
            aue->e_create_h = create_chorus;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_chorus;
            aue->e_proc_h = chorus_process;
            break;
        case AUDIO_EFFECT_REVERB_MAX:
            strength++;
        case AUDIO_EFFECT_REVERB_MID:
            strength++;
        case AUDIO_EFFECT_REVERB_MIN:
        case AUDIO_EFFECT_REVERB:
            aue->e_create_h = create_reverb;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_reverb;
            aue->e_proc_h = reverb_process;
            break;
        case AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE:
            strength++;
        case AUDIO_EFFECT_PITCH_UP_SHIFT_MAX:
            strength++;
        case AUDIO_EFFECT_PITCH_UP_SHIFT_MED:
            strength++;
        case AUDIO_EFFECT_PITCH_UP_SHIFT_MIN:
        case AUDIO_EFFECT_PITCH_UP_SHIFT:
            aue->e_create_h = create_pitch_up_shift;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pitch_shift;
            aue->e_proc_h = pitch_shift_process;
            break;
        case AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE:
            strength++;
        case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX:
            strength++;
        case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED:
            strength++;
        case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN:
        case AUDIO_EFFECT_PITCH_DOWN_SHIFT:
            aue->e_create_h = create_pitch_down_shift;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pitch_shift;
            aue->e_proc_h = pitch_shift_process;
            break;
        case AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX:
            strength++;
        case AUDIO_EFFECT_PACE_DOWN_SHIFT_MED:
            strength++;
        case AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN:
            aue->e_create_h = create_pace_down_shift;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pace_shift;
            aue->e_proc_h = pace_shift_process;
            aue->e_length_h = pace_shift_length_factor;
            break;
        case AUDIO_EFFECT_PACE_UP_SHIFT_MAX:
            strength++;
        case AUDIO_EFFECT_PACE_UP_SHIFT_MED:
            strength++;
        case AUDIO_EFFECT_PACE_UP_SHIFT_MIN:
            aue->e_create_h = create_pace_up_shift;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pace_shift;
            aue->e_proc_h = pace_shift_process;
            aue->e_length_h = pace_shift_length_factor;
            break;
        case AUDIO_EFFECT_VOCODER_MED:
            strength++;            
        case AUDIO_EFFECT_VOCODER_MIN:
            aue->e_create_h = create_vocoder;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_vocoder;
            aue->e_proc_h = vocoder_process;
            break;
        case AUDIO_EFFECT_AUTO_TUNE_MAX:
            strength++;
        case AUDIO_EFFECT_AUTO_TUNE_MED:
            strength++;            
        case AUDIO_EFFECT_AUTO_TUNE_MIN:
            aue->e_create_h = create_auto_tune;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_auto_tune;
            aue->e_proc_h = auto_tune_process;
            break;
        case AUDIO_EFFECT_HARMONIZER_MAX:
            strength++;
        case AUDIO_EFFECT_HARMONIZER_MED:
            strength++;
        case AUDIO_EFFECT_HARMONIZER_MIN:
            aue->e_create_h = create_harmonizer;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_harmonizer;
            aue->e_proc_h = harmonizer_process;
            break;
        case AUDIO_EFFECT_NORMALIZER:
            aue->e_create_h = create_normalizer;
            aue->e_reset_h = reset_normalizer;
            aue->e_free_h = free_normalizer;
            aue->e_proc_h = normalizer_process;
            break;
        case AUDIO_EFFECT_PITCH_UP_DOWN_MAX:
            strength++;
        case AUDIO_EFFECT_PITCH_UP_DOWN_MED:
            strength++;
        case AUDIO_EFFECT_PITCH_UP_DOWN_MIN:
            aue->e_create_h = create_pitch_cycler;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pitch_cycler;
            aue->e_proc_h = pitch_cycler_process;
            break;
        case AUDIO_EFFECT_NONE:
            aue->e_create_h = create_pass_through;
            aue->e_reset_h = NULL;
            aue->e_free_h = free_pass_through;
            aue->e_proc_h = pass_through_process;
            break;
        default:
            error("voe: no valid audio effect \n");
            err = -1;
            goto out;
    }
    aue->effect = aue->e_create_h(fs_hz, strength);
    if(!aue->effect){
        err = -1;
    }
    
out:
    if (err) {
        mem_deref(aue);
    }
    else {
        *auep = aue;
    }
    
    return err;
}

int aueffect_reset(struct aueffect *aue, int fs_hz)
{
    if(!aue->effect || !aue->e_proc_h){
        error("Effect not allocated ! \n");
        return -1;
    }
    
    aue->e_reset_h(aue->effect, fs_hz);
    
    return 0;
}

int aueffect_process(struct aueffect *aue, const int16_t *sampin, int16_t *sampout, size_t n_sampin, size_t *n_sampout)
{
    if(!aue->effect || !aue->e_proc_h){
        error("Effect not allocated ! \n");
        return -1;
    }
    
    aue->e_proc_h(aue->effect, (int16_t*)sampin, sampout, n_sampin, n_sampout);
    
    return 0;
}

int aueffect_length_modification(struct aueffect *aue, int *length_modification_q10)
{
    if(aue->e_length_h){
        aue->e_length_h(aue->effect, length_modification_q10);
    } else {
        *length_modification_q10 = 1024;
    }
 
    return 0;
}
