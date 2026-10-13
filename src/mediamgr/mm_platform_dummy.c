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
	enum mediamgr_auplay fallback_route;
	bool reject_routes;
	int route_requests;
} dummy = {
	.mm = NULL,
	.current_route = MEDIAMGR_AUPLAY_EARPIECE,
	.fallback_route = MEDIAMGR_AUPLAY_UNKNOWN,
	.reject_routes = false,
	.route_requests = 0,
};


/* Applies a route and reports it back, like the Android platform.
 * Tests change routes through the media manager's queue, but the
 * rejection flag and request count are read across threads without
 * it, e.g. while a restore check is pending, so they are atomic. The
 * request is counted after deciding its outcome, so a test that sees
 * the count can change the behaviour for the next request only.
 */
static int request_route(enum mediamgr_auplay route)
{
	bool reject = __atomic_load_n(&dummy.reject_routes, __ATOMIC_SEQ_CST);
	enum mediamgr_auplay fallback = dummy.fallback_route;

	__atomic_add_fetch(&dummy.route_requests, 1, __ATOMIC_SEQ_CST);
	if (reject)
		return -1;

	if (fallback != MEDIAMGR_AUPLAY_UNKNOWN)
		dummy.current_route = fallback;
	else
		dummy.current_route = route;

	mediamgr_platform_route_changed(dummy.mm);

	return 0;
}


/* Test hooks emulating platform routing */
void mm_platform_dummy_reset(void)
{
	dummy.current_route = MEDIAMGR_AUPLAY_EARPIECE;
	dummy.fallback_route = MEDIAMGR_AUPLAY_UNKNOWN;
	dummy.reject_routes = false;
	dummy.route_requests = 0;
}

void mm_platform_dummy_change_route(enum mediamgr_auplay route)
{
	dummy.current_route = route;
	mediamgr_platform_route_changed(dummy.mm);
}

void mm_platform_dummy_set_fallback(enum mediamgr_auplay route)
{
	dummy.fallback_route = route;
}

void mm_platform_dummy_reject_routes(bool reject)
{
	__atomic_store_n(&dummy.reject_routes, reject, __ATOMIC_SEQ_CST);
}

int mm_platform_dummy_route_requests(void)
{
	return __atomic_load_n(&dummy.route_requests, __ATOMIC_SEQ_CST);
}


int mm_platform_init(struct mm *mm, struct dict *sounds)
{
	dummy.mm = mm;
	
	(void)sounds;
	return 0;
}

int mm_platform_free(struct mm *mm)
{
	dummy.mm = NULL;

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
    return request_route(MEDIAMGR_AUPLAY_SPEAKER);
}

int mm_platform_enable_bt_sco(void)
{
    return request_route(MEDIAMGR_AUPLAY_BT);
}

int mm_platform_enable_earpiece(void)
{
    return request_route(MEDIAMGR_AUPLAY_EARPIECE);
}

int mm_platform_enable_headset(void)
{
    return request_route(MEDIAMGR_AUPLAY_HEADSET);
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


void mm_platform_stop_delayed_play(void)
{
}
