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

#ifndef AVS_MEDIA_MANAGER_H
#define AVS_MEDIA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

enum mediamgr_auplay {
	MEDIAMGR_AUPLAY_EARPIECE,
	MEDIAMGR_AUPLAY_SPEAKER,
	MEDIAMGR_AUPLAY_HEADSET,
	MEDIAMGR_AUPLAY_BT,
	MEDIAMGR_AUPLAY_LINEOUT,
	MEDIAMGR_AUPLAY_SPDIF,
	MEDIAMGR_AUPLAY_UNKNOWN,
};

enum mediamgr_state {
	MEDIAMGR_STATE_NORMAL = 0,
	MEDIAMGR_STATE_OUTGOING_AUDIO_CALL,
	MEDIAMGR_STATE_OUTGOING_VIDEO_CALL,
	MEDIAMGR_STATE_INCOMMING_CALL,
	MEDIAMGR_STATE_AUDIO_ACTIVATED,
	MEDIAMGR_STATE_AUDIO_DEACTIVATED,
	MEDIAMGR_STATE_CALL_ESTABLISHED,
	MEDIAMGR_STATE_VIDEOCALL_ESTABLISHED,
	MEDIAMGR_STATE_INCALL,
	MEDIAMGR_STATE_INVIDEOCALL,
	MEDIAMGR_STATE_TWO_WAY_AUDIO,
	MEDIAMGR_STATE_ROAMING,
	MEDIAMGR_STATE_HOLD,
	MEDIAMGR_STATE_RESUME, // not really a state, goes to INCALL if held, otherwise NORMAL
};

enum mediamgr_sound_mode {
	MEDIAMGR_SOUND_MODE_ALL = 0,
	MEDIAMGR_SOUND_MODE_SOME,
	MEDIAMGR_SOUND_MODE_NONE,
};
	
typedef void (mediamgr_route_changed_h)(enum mediamgr_auplay new_route,
					void *arg);
typedef void (mediamgr_mcat_changed_h)(enum mediamgr_state new_state,
				       void *arg);

struct mediamgr;

	
int mediamgr_alloc(struct mediamgr **mmp,
		   mediamgr_mcat_changed_h *cat_handler, void *arg);

struct mm *mediamgr_get(struct mediamgr *mm);	
void mediamgr_play_media(struct mediamgr *mm, const char *media_name);
void mediamgr_pause_media(struct mediamgr *mm, const char *media_name);
void mediamgr_stop_media(struct mediamgr *mm, const char *media_name);

void mediamgr_set_call_state(struct mediamgr *mm, enum mediamgr_state state);

void mediamgr_enable_speaker(struct mediamgr *mm, bool enable);
enum mediamgr_auplay mediamgr_get_route(const struct mediamgr *mm);

void mediamgr_register_media(struct mediamgr *mm,
			     const char *media_name,
			     void* media_object,
			     bool mixing,
			     bool incall,
			     int intensity,
			     int priority,
			     bool is_call_media);

void mediamgr_unregister_media(struct mediamgr *mm, const char *media_name);

void mediamgr_register_route_change_h(struct mediamgr *mm,
				      mediamgr_route_changed_h *handler,
				      void *arg);

void mediamgr_set_sound_mode(struct mediamgr *mm,
			     enum mediamgr_sound_mode mode);
    
void mediamgr_set_user_starts_audio(struct mediamgr *mediamgr, bool enable);

void mediamgr_enter_call(struct mediamgr *mediamgr);
void mediamgr_exit_call(struct mediamgr *mediamgr);

    /* Global functions */
struct mm;
void mediamgr_headset_connected(struct mm *mm, bool connected);
void mediamgr_bt_device_connected(struct mm *mm, bool connected);

void mediamgr_device_changed(struct mm *mm);	

#ifdef __cplusplus
}
#endif


#endif // AVS_MEDIA_MANAGER_H

