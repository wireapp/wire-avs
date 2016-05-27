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
    
    debug("aueffect_destructor: %p \n",aue);
    
    aue->e_free_h(aue->effect);
}

int aueffect_alloc(struct aueffect **auep,
                   audio_effect effect_type,
                   int fs_hz)
{
    struct aueffect *aue;
    int err = 0;
    
    if (!auep) {
        return EINVAL;
    }
    
    debug("voe: aueffect_alloc: \n");
    
    aue = (struct aueffect *)mem_zalloc(sizeof(*aue), aueffect_destructor);
    if (!aue)
        return ENOMEM;
    
    switch ((audio_effect)effect_type) {
        case AUDIO_EFFECT_CHORUS:
            aue->e_create_h = create_chorus;
            aue->e_free_h = free_chorus;
            aue->e_proc_h = chorus_process;
            break;
        case AUDIO_EFFECT_REVERB:
            aue->e_create_h = create_reverb;
            aue->e_free_h = free_reverb;
            aue->e_proc_h = reverb_process;
            break;
        default:
            error("voe: no valid audio effect \n");
            err = -1;
            goto out;
    }
    aue->effect = aue->e_create_h(fs_hz);
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

int aueffect_process(struct aueffect *aue, const int16_t *sampin, int16_t *sampout, size_t sampc)
{
    if(!aue->effect || !aue->e_proc_h){
        error("Effect not allocated ! \n");
        return -1;
    }
    
    aue->e_proc_h(aue->effect, (int16_t*)sampin, sampout, sampc);
    
    return 0;
}