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

#include "re.h"
#include "avs.h"
#include "mm_platform.h"

static struct {
	struct mm *mm;
	enum mediamgr_auplay current_route;
} dummy = {
	.mm = NULL,
	.current_route = MEDIAMGR_AUPLAY_EARPIECE,
};


int mm_platform_init(struct mm *mm, struct dict *sounds)
{
	dummy.mm = mm;
	
	(void)sounds;
	return 0;
}

int mm_platform_free(struct mm *mm)
{
	return 0;
}

void mm_platform_play_sound(struct sound *snd, bool sync, bool delayed)
{
	(void)snd;
}

void mm_platform_pause_sound(struct sound *snd)
{
	(void)snd;
}

void mm_platform_resume_sound(struct sound *snd)
{
	(void)snd;
}

void mm_platform_stop_sound(struct sound *snd)
{
	(void)snd;
}

bool mm_platform_is_sound_playing(struct sound *snd)
{
	(void)snd;
	return false;
}

int mm_platform_enable_speaker(void)
{
    dummy.current_route = MEDIAMGR_AUPLAY_SPEAKER;

    return 0;
}

int mm_platform_enable_bt_sco(void)
{
    dummy.current_route = MEDIAMGR_AUPLAY_BT;

    return 0;
}

int mm_platform_enable_earpiece(void)
{
    dummy.current_route = MEDIAMGR_AUPLAY_EARPIECE;

    return 0;
}

int mm_platform_enable_headset(void)
{
    dummy.current_route = MEDIAMGR_AUPLAY_HEADSET;

    return 0;
}

enum mediamgr_auplay mm_platform_get_route(void)
{
    return dummy.current_route;
}

void mm_platform_incoming(void){
    info("mm_platform_dummy: enter_call()\n");
    mediamgr_sys_incoming(dummy.mm);
}

void mm_platform_enter_call(void){
    info("mm_platform_dummy: enter_call()\n");
    mediamgr_sys_entered_call(dummy.mm);
}

void mm_platform_exit_call(void){
    info("mm_platform_dummy: exit_call()\n");
    mediamgr_sys_left_call(dummy.mm);
}

void mm_platform_set_active(void){
    info("mm_platform_set_active() \n");
}

void mm_platform_registerMedia(struct dict *sounds,
                               const char *name,
                               void *mediaObj,
                               bool mixing,
                               bool incall,
                               int intensity,
                               int priority,
                               bool is_call_media)
{
    debug("mm_platform_registerMedia name = %s obj = %p \n", name, mediaObj);
    
    struct sound *snd;
    
    snd = mem_zalloc(sizeof(struct sound), NULL);
    
    snd->arg = mediaObj;
    snd->mixing = mixing;
    snd->incall = incall;
    snd->intensity = intensity;
    snd->priority = priority;
    snd->is_call_media = is_call_media;
    
    dict_add( sounds, name, (void*)snd);
    mem_deref(snd); // to get the ref count to 1
}

void mm_platform_unregisterMedia(struct dict *sounds, const char *name){
    dict_remove(sounds, name);
}


void mm_platform_reset_sound(struct sound *snd)
{
	(void)snd;
}


void mm_platform_start_recording(struct mm_platform_start_rec *rec_elem)
{
}


void mm_platform_stop_recording(void)
{
}

void mm_platform_confirm_route(enum mediamgr_auplay route)
{
}

