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

#ifndef MM_PLATFORM_H
#define MM_PLATFORM_H

#include "mediamgr.h"

struct mm;

#ifdef __cplusplus__
extern "C" {
#endif

struct mm_platform_start_rec {
	mediamgr_start_rec_h *rech;
	void *arg;
};
	
int mm_platform_init(struct mm *mm, struct dict *sounds);
int mm_platform_free(struct mm *mm);
	
void mm_platform_play_sound(struct sound *snd, bool sync, bool delayed);
void mm_platform_pause_sound(struct sound *snd);
void mm_platform_resume_sound(struct sound *snd);
void mm_platform_stop_sound(struct sound *snd);
bool mm_platform_is_sound_playing(struct sound *snd);
void mm_platform_reset_sound(struct sound *snd);	

/* Audio Routing */
int mm_platform_enable_speaker(void);
int mm_platform_enable_bt_sco(void);
int mm_platform_enable_earpiece(void);
int mm_platform_enable_headset(void);
	
enum mediamgr_auplay mm_platform_get_route(void);
void mm_platform_confirm_route(enum mediamgr_auplay route);

	
void mm_platform_registerMedia(struct dict *sounds,
	const char *name,
	void *mediaObj,
	bool mixing,
	bool incall,
	int intensity,
	int priority,
	bool is_call_media);
	
void mm_platform_unregisterMedia(struct dict *sounds, const char *name);

void mm_platform_incoming(void);
void mm_platform_enter_call(void);
void mm_platform_exit_call(void);

void mm_platform_set_active(void);


void mm_platform_start_recording(struct mm_platform_start_rec *rec_elem);
void mm_platform_stop_recording(void);
	
#ifdef __cplusplus__
};
#endif

#endif

