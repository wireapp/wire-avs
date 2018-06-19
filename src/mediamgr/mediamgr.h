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

#ifndef MEDIAMGR_H
#define MEDIAMGR_H

#define MM_INTENSITY_THRES_ALL 100
#define MM_INTENSITY_THRES_SOME 50
#define MM_INTENSITY_THRES_NONE  0

struct sound {
	char *name;
	const char *path;
	const char *format;
	bool loop;
	bool mixing;	
	bool incall;
	int intensity;
	int priority;
	bool is_call_media;
	bool sync; /* played synchronosuly */
	void *arg;

	struct le le; /* member of sounds list */
};

int sound_alloc(struct sound **sndp,
		const char *path, const char *fmt,
		bool loop, bool mixing, bool incall, int intensity, bool is_call_media);

const char *mediamgr_route_name(enum mediamgr_auplay route);

void mediamgr_enable_speaker_mm(struct mm *mm, bool enable);

void mediamgr_set_call_state_mm(struct mm *mm, enum mediamgr_state state);
void mediamgr_sys_incoming(struct mm *mm);
void mediamgr_sys_entered_call(struct mm *mm);
void mediamgr_sys_left_call(struct mm *mm);
void mediamgr_hold_and_resume(struct mm *mm);
bool mediamgr_get_speaker(struct mm *mm);
void mediamgr_override_speaker_mm(struct mm *mm, bool speaker);
void mediamgr_audio_release_mm(struct mm *mm);
void mediamgr_audio_reset_mm(struct mm *mm);
void mediamgr_reset_sounds(struct mm *mm);
bool mediamgr_should_reset(struct mm *mm);


#endif

