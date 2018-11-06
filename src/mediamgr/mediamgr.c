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
#include "avs.h"
#include "avs_mediamgr.h"
#include "avs_lockedqueue.h"
#include <pthread.h>
#include "mediamgr.h"
#include "mm_platform.h"
#include "avs_flowmgr.h"
#include <unistd.h>

#define MM_USE_THREAD   1

enum mm_sys_state {
	MM_SYS_STATE_UNKNOWN,
	MM_SYS_STATE_NORMAL,
	MM_SYS_STATE_INCOMING,
	MM_SYS_STATE_ENTERING_CALL,
	MM_SYS_STATE_INCALL,
	MM_SYS_STATE_EXITING_CALL,
};

enum mm_route_update_event {
	MM_HEADSET_PLUGGED = 0,
	MM_HEADSET_UNPLUGGED,
	MM_BT_DEVICE_CONNECTED,
	MM_BT_DEVICE_DISCONNECTED,
	MM_DEVICE_CHANGED,
	MM_SPEAKER_ENABLE_REQUEST,
	MM_SPEAKER_DISABLE_REQUEST,
 };

struct mm_route_state_machine {
	bool prefer_loudspeaker;
	bool bt_device_is_connected;
	bool wired_hs_is_connected;
	enum mediamgr_auplay cur_route;
};

typedef enum {
	MM_PLAYBACK_NONE = 0,
	MM_PLAYBACK_MIXING,
	MM_PLAYBACK_EXCLUSIVE
} mm_playback_mode;

typedef enum {
	MM_MARSHAL_EXIT = 0,
	MM_MARSHAL_PLAY_MEDIA,
	MM_MARSHAL_PAUSE_MEDIA,
	MM_MARSHAL_STOP_MEDIA,
	MM_MARSHAL_CALL_STATE,
	MM_MARSHAL_ENABLE_SPEAKER,
	MM_MARSHAL_HEADSET_CONNECTED,
	MM_MARSHAL_BT_DEVICE_CONNECTED,
	MM_MARSHAL_DEVICE_CHANGED,
	MM_MARSHAL_REGISTER_MEDIA,
	MM_MARSHAL_DEREGISTER_MEDIA,
	MM_MARSHAL_SET_INTENSITY,
	MM_MARSHAL_SET_USER_START_AUDIO,
	MM_MARSHAL_ENTER_CALL,
	MM_MARSHAL_EXIT_CALL,
	MM_MARSHAL_AUDIO_ALLOC,
	MM_MARSHAL_AUDIO_RELEASE,
	MM_MARSHAL_AUDIO_RESET,
	MM_MARSHAL_SYS_INCOMING,
	MM_MARSHAL_SYS_ENTERED_CALL,
	MM_MARSHAL_SYS_LEFT_CALL,
	MM_MARSHAL_INVOKE_INCOMINGH,
	MM_MARSHAL_START_RECORDING,
	MM_MARSHAL_STOP_RECORDING,
} mm_marshal_id;

struct mm_message {
	union {
		struct {
			char media_name[128];
		} media_elem;
		struct {
			enum mediamgr_state state;
		} state_elem;
		struct {
			bool val;
		} bool_elem;
		struct {
			char media_name[128];
			void *media_object;
			bool mixing;
			bool incall;
			int intensity;
			int priority;
			bool is_call_media;
		} register_media_elem;
		struct {
			int intensity;
		} set_intensity_elem;
		struct {
			mediamgr_incoming_h *incomingh;
			char *convid;
			char *userid;
			uint32_t msg_time;
			int video_call;
			int should_ring;
			void *arg;
		} incomingh;
		struct mm_platform_start_rec *rec_elem;
	};
};

struct mm {
	struct mqueue *mq;
	struct dict *sounds;

	enum mediamgr_state call_state;
	enum mediamgr_state hold_state;
	enum mm_sys_state sys_state;
	volatile bool started;
	bool should_reset;
	bool alloc_pending;

	pthread_t thread;

	struct mm_route_state_machine router;

	int intensity_thres;
	bool user_starts_audio;
    
	struct audio_io *aio;
    
	struct list mml;
};

struct mediamgr {
	struct mm *mm;
	struct le le; /* member of the mm list */
	
	mediamgr_route_changed_h *route_changed_h;
	void* route_changed_arg;
	
	mediamgr_mcat_changed_h *mcat_changed_h;
	void *arg;
};


static struct mm *g_mm = NULL;


/* prototypes */
static void *mediamgr_thread(void *arg);
static void enter_call(struct mm *mm);
static void exit_call(struct mm *mm);
static void enable_speaker(struct mm *mm, bool enable);



const char *mediamgr_route_name(enum mediamgr_auplay route)
{
	switch (route) {            
        case MEDIAMGR_AUPLAY_EARPIECE:	return "Earpiece";
        case MEDIAMGR_AUPLAY_SPEAKER:	return "Speakerphone";
        case MEDIAMGR_AUPLAY_HEADSET:	return "Headset";
        case MEDIAMGR_AUPLAY_BT:	return "Bluetooth";
        case MEDIAMGR_AUPLAY_LINEOUT:	return "Line";
        case MEDIAMGR_AUPLAY_SPDIF:	return "SPDif";
        default:                        return "???";
    }
}

static const char *mm_sys_state_name(enum mm_sys_state st)
{
	switch(st) {
	case MM_SYS_STATE_UNKNOWN:
		return "Unknown";

	case MM_SYS_STATE_NORMAL:
		return "Normal";
		
	case MM_SYS_STATE_ENTERING_CALL:
		return "Entering-Call";

	case MM_SYS_STATE_INCOMING:
		return "Incoming";
		
	case MM_SYS_STATE_INCALL:
		return "In-Call";
		
	case MM_SYS_STATE_EXITING_CALL:
		return "Exiting-Call";

	default:
		return "???";
	}
}

static const char *mmstate_name(enum mediamgr_state st)
{
	switch (st) {            
	case MEDIAMGR_STATE_NORMAL:
		return "Normal";

	case MEDIAMGR_STATE_OUTGOING_AUDIO_CALL:
		return "Outgoing-Audio-Call";

	case MEDIAMGR_STATE_OUTGOING_VIDEO_CALL:
		return "Outgoing-Video-Call";

	case MEDIAMGR_STATE_INCOMING_AUDIO_CALL:
		return "Incoming-Audio-Call";

	case MEDIAMGR_STATE_INCOMING_VIDEO_CALL:
		return "Incoming-Video-Call";
		
	case MEDIAMGR_STATE_INCALL:
		return "In-Call";

	case MEDIAMGR_STATE_INVIDEOCALL:
		return "In-VideoCall";

	case MEDIAMGR_STATE_HOLD:
		return "Hold";

	case MEDIAMGR_STATE_RESUME:
		return "Resume";
		
	default:
		return "???";
	}
}

static const char *event_name(enum mm_route_update_event event)
{
	switch (event) {
	case MM_HEADSET_PLUGGED:
		return "Headset-PlugIn";

	case MM_HEADSET_UNPLUGGED:
		return "Headset-PlugOut";
		
	case MM_BT_DEVICE_CONNECTED:
		return "Bluetooth-PlugIn";
		
	case MM_BT_DEVICE_DISCONNECTED:
		return "Bluetooth-PlugOut";
		
	case MM_DEVICE_CHANGED:
		return "System-DeviceChange";
		
	case MM_SPEAKER_ENABLE_REQUEST:
		return "Speaker-On";

	case MM_SPEAKER_DISABLE_REQUEST:
		return "Speaker-Off";

	default:
		return "???";
	}
}

static void fire_callback(struct mm *mm)
{
	struct le *le;	

	info("mediamgr: fire_callback\n");
	
	LIST_FOREACH(&g_mm->mml, le) {
		struct mediamgr *mgr = le->data;

		info("mediamgr: fire_callback: firing callback for mm=%p\n",
		     mgr);
		
		if (mgr->mcat_changed_h)
			mgr->mcat_changed_h(mm->call_state, mgr->arg);
	}
}

static bool stop_playing_media(char *key, void *val, void *arg)
{
	struct sound *snd = (struct sound *)val;
	bool is_playing;

	(void)key;
	(void)arg;

	is_playing = mm_platform_is_sound_playing(snd);

	if (is_playing)
	    mm_platform_stop_sound(snd);

	return false;
}


static void stop_all_media(struct mm *mm)
{
	dict_apply(mm->sounds, stop_playing_media, NULL);
	
}


static bool check_play_mode(char *key, void *val, void *arg)
{
	struct sound* snd = (struct sound*)val;
	mm_playback_mode *mode = (mm_playback_mode*)arg;

	(void)key;

	if (mm_platform_is_sound_playing(snd)) {
		if (snd->mixing) {
			*mode = MM_PLAYBACK_MIXING;
		}
		else {
			*mode = MM_PLAYBACK_EXCLUSIVE;
			return true;
		}
	}

	return false;
}


static bool mediamgr_can_play_sound(struct mm *mm,
				    struct dict *sounds,
				    struct sound *to_play)
{
	mm_playback_mode mode = MM_PLAYBACK_NONE;
	bool incall = (mm->call_state == MEDIAMGR_STATE_INCALL ||
		       mm->call_state == MEDIAMGR_STATE_INVIDEOCALL);

	info("mediamgr: can_play_sound: incall=%d(%d) intensity=%d(%d) "
	     "prio=%d\n",
	     to_play->incall, incall,
	     to_play->intensity, mm->intensity_thres,
	     to_play->priority);
	
	/* Check intensity setting */
	if (to_play->intensity > mm->intensity_thres) {
		return false;
	}

	/* Some sounds are not allowed in-call */
	if (!to_play->incall && incall) {
		return false;
	}

	if (to_play->priority > 0) {
		return true;
	}

	dict_apply(sounds, check_play_mode, &mode);

	/* Only allow 1 exclusive or many mixings */
	switch (mode) {

	case MM_PLAYBACK_NONE:
		return true;

	case MM_PLAYBACK_EXCLUSIVE:
		return false;

	case MM_PLAYBACK_MIXING:
		return to_play->mixing;
	}

	return false;
}

#if 0
static bool stop_play(char *key, void *val, void *arg)
{
	struct sound *snd = (struct sound *)val;

	(void)key;
	(void)arg;

	if (mm_platform_is_sound_playing(snd)) {
		mm_platform_stop_sound(snd);
	}

	return false;
}
#endif

static void play_sound(struct mm *mm, const char *mname, bool sync,
		       bool delayed)
{
	struct sound *snd;
	
	snd = dict_lookup(mm->sounds, mname);
	if (!snd) {
		error("mediamgr: play_sound: %s not found\n", mname);
		return;
	}

	info("mediamgr: play_sound: %s\n", mname);

	if (mediamgr_can_play_sound(mm, mm->sounds, snd)) {
#if 0
		if (snd->priority > 0) {
			debug("mediamgr: play_sound: priority audio\n");
			dict_apply(mm->sounds, stop_play, NULL);
		}
#endif
			
		mm_platform_play_sound(snd, sync, delayed);
	}
}

static int mediamgr_post_media_cmd(struct mm *mm,
				   mm_marshal_id cmd,
				   const char* media_name)
{
	struct mm_message *elem = NULL;

	switch (cmd) {
	case MM_MARSHAL_AUDIO_ALLOC:
		if (mm->alloc_pending)
			return 0;
		else {
			mm->alloc_pending = true;
		}
		break;

	default:
		break;
	}
	
	if (media_name) {
		elem = mem_zalloc(sizeof(struct mm_message), NULL);
		if (!elem) {
			return ENOMEM;
		}

		str_ncpy(elem->media_elem.media_name,
			 media_name,
			 sizeof(elem->media_elem.media_name));
	}

	return mqueue_push(mm->mq, cmd, elem);
}

static void set_video_route(struct mm *mm)
{
	enum mediamgr_auplay route;

	mm->router.prefer_loudspeaker = true;
	
	route = mm_platform_get_route();
	switch(route) {
	case MEDIAMGR_AUPLAY_HEADSET:
	case MEDIAMGR_AUPLAY_BT:
	case MEDIAMGR_AUPLAY_LINEOUT:
	case MEDIAMGR_AUPLAY_SPDIF:
		break;

	default:
		enable_speaker(mm, true);
		break;
	}
}

static void incall_action(struct mm *mm)
{
	info("mediamgr: incall_action\n");
	
	stop_all_media(mm);

	if (mm->call_state == MEDIAMGR_STATE_INVIDEOCALL)
		set_video_route(mm);
	
	play_sound(mm, "ready_to_talk", true, false);

	mediamgr_post_media_cmd(mm, MM_MARSHAL_AUDIO_ALLOC, NULL);
}


static void handle_incall(struct mm *mm, bool hold)
{
	info("mediamgr: handle_incall: call_state=%s sys_state=%s hold=%d\n",
	     mmstate_name(mm->call_state), mm_sys_state_name(mm->sys_state),
	     hold);

	if (mm->call_state == MEDIAMGR_STATE_INVIDEOCALL)
		mm->router.prefer_loudspeaker = true;

	if (hold) {
		enter_call(mm);
		return;
	}
	
	switch(mm->sys_state) {
	case MM_SYS_STATE_INCALL:
		incall_action(mm);
		break;

	case MM_SYS_STATE_ENTERING_CALL:
		break;

	default:
		enter_call(mm);
		break;
	}
}


static void mm_destructor(void *arg)
{
	struct mm *mm = arg;

	if (mm->started) {
		mqueue_push(mm->mq, MM_MARSHAL_EXIT, NULL);

		/* waits untill re_cancel() is called on mediamgr_thread */
		pthread_join(mm->thread, NULL);
	}

	dict_flush(mm->sounds);
	mem_deref(mm->sounds);
	mem_deref(mm->mq);
	mem_deref(mm->aio);
    
	mm_platform_free(mm);

	g_mm = NULL;
}


static void mediamgr_destructor(void *arg)
{
	struct mediamgr *mm = arg;

	list_unlink(&mm->le);

	mem_deref(mm->mm);
}


struct mm *mediamgr_get(struct mediamgr *mediamgr)
{
	return mediamgr ? mediamgr->mm : NULL;
}


static int mm_alloc(struct mm **mmp)
{
	struct mm *mm;
	int err = 0;

	mm = mem_zalloc(sizeof(*mm), mm_destructor);
	if (!mm)
		return ENOMEM;

	err = dict_alloc(&mm->sounds);
	if (err)
		goto out;

	mm->started = false;

#ifdef MM_USE_THREAD	
	err = pthread_create(&mm->thread, NULL, mediamgr_thread, mm);
	if (err != 0) {
		goto out;
	}
#else
	mediamgr_thread(mm);
#endif
	for ( int cnt = 0; cnt < 10000; cnt++) {
		if (mm->started) {
			break;
		}
		usleep(1000);  /* 1ms so allow to wait 10 secs */
	}

	mm->router.cur_route = MEDIAMGR_AUPLAY_UNKNOWN;

	mm->intensity_thres = MM_INTENSITY_THRES_ALL;
	mm->user_starts_audio = false;
    
 out:
	if (err) {
		mem_deref(mm);
	}
	else {
		*mmp = mm;
	}

	return err;
}


int mediamgr_alloc(struct mediamgr **mmp,
		   mediamgr_mcat_changed_h *mcat_handler, void *arg)
{
	struct mediamgr *mm;
	int err = 0;

	if (!mmp || !mcat_handler)
		return EINVAL;
	
	mm = mem_zalloc(sizeof(*mm), mediamgr_destructor);
	if (!mm)
		return ENOMEM;
	
	if (!g_mm) {
		err = mm_alloc(&g_mm);
		if (err)
			goto out;
		mm->mm = g_mm;
	}
	else {
		mm->mm = mem_ref(g_mm);
	}

	mm->mcat_changed_h = mcat_handler;
	mm->arg = arg;

	list_append(&g_mm->mml, &mm->le, mm);
	
 out:
	if (err)
		mem_deref(mm);
	else
		*mmp = mm;

	return err;
}

static enum mediamgr_auplay get_wanted_route(struct mm *mm)
{
	enum mediamgr_auplay wanted_route = MEDIAMGR_AUPLAY_EARPIECE;
	
	if (mm->router.prefer_loudspeaker)
		wanted_route = MEDIAMGR_AUPLAY_SPEAKER;
	else if (mm->router.wired_hs_is_connected)
		wanted_route = MEDIAMGR_AUPLAY_HEADSET;
	else if (mm->router.bt_device_is_connected)
		wanted_route = MEDIAMGR_AUPLAY_BT;

	return wanted_route;
}

static void satisfy_route(enum mediamgr_auplay route)
{
	switch (route) {
	case MEDIAMGR_AUPLAY_HEADSET:
		mm_platform_enable_headset();
		break;

	case MEDIAMGR_AUPLAY_EARPIECE:
		mm_platform_enable_earpiece();
		break;

	case MEDIAMGR_AUPLAY_SPEAKER:
		mm_platform_enable_speaker();
		break;

	case MEDIAMGR_AUPLAY_BT:
		mm_platform_enable_bt_sco();
		break;

	default:
		warning("mediamgr: satisfy_route: unsupported route: %s\n",
			mediamgr_route_name(route));
		break;
	}
}


static void enable_speaker(struct mm *mm, bool enable)
{
	enum mediamgr_auplay cur_route = mm_platform_get_route();
	enum mediamgr_auplay wanted_route = MEDIAMGR_AUPLAY_UNKNOWN;

	info("enable_speaker: enable=%d\n", enable);
	
	mm->router.prefer_loudspeaker = enable;
	
	wanted_route = get_wanted_route(mm);

	info("mediamgr: enable_speaker: enable=%s "
	     "wanted_route=%s cur_route=%s\n",
	     enable ? "yes" : "no",
	     mediamgr_route_name(wanted_route),
	     mediamgr_route_name(cur_route));

	if (wanted_route != cur_route)
		satisfy_route(wanted_route);
}


static void update_route(struct mm *mm, enum mm_route_update_event event)
{
	enum mediamgr_auplay cur_route = mm_platform_get_route(); 
	enum mediamgr_auplay wanted_route = MEDIAMGR_AUPLAY_UNKNOWN;
	struct le *le;
	
	if (!mm) {
		warning("mediamgr: update_route: no mm\n");
		return;
	}

	info("mediamgr: update_route mm=%p event=%s cur_route=%s "
	     "loudspeaker=%s hs=%d bt=%d\n",
	     mm, event_name(event), mediamgr_route_name(cur_route),
	     mm->router.prefer_loudspeaker ? "yes" : "no",
	     mm->router.wired_hs_is_connected,
	     mm->router.bt_device_is_connected);
	
	switch (event) {
	case MM_HEADSET_PLUGGED:
		mm->router.wired_hs_is_connected = true;
		mm->router.prefer_loudspeaker = false;
		wanted_route = MEDIAMGR_AUPLAY_HEADSET;
		break;

	case MM_HEADSET_UNPLUGGED:
		mm->router.wired_hs_is_connected = false;
		if (mm->router.bt_device_is_connected)
			wanted_route = MEDIAMGR_AUPLAY_BT;
		if (mm->call_state == MEDIAMGR_STATE_INVIDEOCALL)
			mm->router.prefer_loudspeaker = true;
		break;

	case MM_BT_DEVICE_CONNECTED:
		mm->router.bt_device_is_connected = true;
		if (mm->call_state == MEDIAMGR_STATE_INCALL ||
		    mm->call_state == MEDIAMGR_STATE_INVIDEOCALL) {
            
			/* Always switch to BT when plugged in */
			wanted_route = MEDIAMGR_AUPLAY_BT;
		}
		mm->router.prefer_loudspeaker = false;
		break;

	case MM_BT_DEVICE_DISCONNECTED:
		mm->router.bt_device_is_connected = false;
		if (mm->router.wired_hs_is_connected)
			wanted_route = MEDIAMGR_AUPLAY_HEADSET;
		if (mm->call_state == MEDIAMGR_STATE_INVIDEOCALL)
			mm->router.prefer_loudspeaker = true;
		break;

	case MM_DEVICE_CHANGED:
		if (cur_route == MEDIAMGR_AUPLAY_SPEAKER &&
		    !mm->router.prefer_loudspeaker) {
			wanted_route = MEDIAMGR_AUPLAY_EARPIECE;
		}
		else if (cur_route == MEDIAMGR_AUPLAY_EARPIECE &&
			 mm->router.prefer_loudspeaker) {
			wanted_route = MEDIAMGR_AUPLAY_SPEAKER;
		}
		else
			wanted_route = cur_route;
		break;
		
	default:
		warning("mediamgr: illegal event: %d for system route\n",
			event);
		break;
	}

	mm->router.cur_route = cur_route;
	
	if (wanted_route == MEDIAMGR_AUPLAY_UNKNOWN)
		wanted_route = get_wanted_route(mm);

	if (cur_route != wanted_route)
		satisfy_route(wanted_route);
	else {
		LIST_FOREACH(&g_mm->mml, le) {
			struct mediamgr *mgr = le->data;
			enum mediamgr_auplay route = cur_route;

			if (mgr->route_changed_h) {
				mgr->route_changed_h(route,
						     mgr->route_changed_arg);
			}
		}
		mm_platform_confirm_route(cur_route);
	}
}


static void set_sys_state(struct mm *mm, enum mm_sys_state state)
{
	enum mm_sys_state old_state = mm->sys_state;

	info("mediamgr: set_sys_state: %s -> %s\n",
	     mm_sys_state_name(old_state), mm_sys_state_name(state));

	mm->sys_state = state;
}


static void enter_incoming(struct mm *mm)
{
	info("mediamgr: enter_incoming: sys_state=%s\n",
	     mm_sys_state_name(mm->sys_state));
	
	switch (mm->sys_state) {
	case MM_SYS_STATE_NORMAL:
		mm_platform_incoming();
		break;

	default:
		break;
	}
}


static void enter_call(struct mm *mm)
{
	info("mediamgr: enter_call: sys_state=%s\n",
	     mm_sys_state_name(mm->sys_state));
	
	switch (mm->sys_state) {
	case MM_SYS_STATE_NORMAL:
	case MM_SYS_STATE_INCOMING:
	case MM_SYS_STATE_EXITING_CALL:
		set_sys_state(mm, MM_SYS_STATE_ENTERING_CALL);
		mm_platform_enter_call();
		break;

	case MM_SYS_STATE_ENTERING_CALL:
	case MM_SYS_STATE_INCALL:
		mm_platform_enter_call();
		break;
		
	default:
		break;
	}
}


static void exit_call(struct mm *mm)
{
	info("mediamgr: exit_call: sys_state=%s\n",
	     mm_sys_state_name(mm->sys_state));
	
	switch (mm->sys_state) {
	case MM_SYS_STATE_INCALL:
	case MM_SYS_STATE_INCOMING:
	case MM_SYS_STATE_ENTERING_CALL:
		set_sys_state(mm, MM_SYS_STATE_EXITING_CALL);
		mm_platform_exit_call();
		break;

	default:
		break;
	}
}


void mediamgr_enter_call(struct mediamgr *mediamgr)
{
	if (!mediamgr)
		return;

	mediamgr_post_media_cmd(mediamgr->mm, MM_MARSHAL_ENTER_CALL, NULL);
}


void mediamgr_exit_call(struct mediamgr *mediamgr)
{
	if (!mediamgr)
		return;

	mediamgr_post_media_cmd(mediamgr->mm, MM_MARSHAL_EXIT_CALL, NULL);
}

static bool is_internal(const char *name)
{
	return streq("ringing_from_me", name) ||
		streq("ringing_from_me_video", name) ||
		streq("ready_to_talk", name) ||
		streq("talk_later", name);
}

void mediamgr_play_media(struct mediamgr *mediamgr, const char *media_name)
{
	int err = 0;
	
	if (!mediamgr)
		return;

	if (is_internal(media_name)) {		
		info("mediamgr_play_media: ignoring internal media %s\n",
		     media_name);
		return;
	}

	err = mediamgr_post_media_cmd(mediamgr->mm,
				      MM_MARSHAL_PLAY_MEDIA,
				      media_name);
	if (err)
		error("mediamgr_play_media failed: %m\n", err);
}


void mediamgr_pause_media(struct mediamgr *mediamgr, const char *media_name)
{
	if (!mediamgr)
		return;

	if (is_internal(media_name)) {
		info("mediamgr_pause_media: ignoring internal media %s\n",
		     media_name);
		return;
	}
    
	if (mediamgr_post_media_cmd(mediamgr->mm, MM_MARSHAL_PAUSE_MEDIA,
					media_name) != 0) {
		error("mediamgr_pause_media failed \n");
	}
}


void mediamgr_stop_media(struct mediamgr *mediamgr, const char *media_name)
{
	if (!mediamgr)
		return;

	if (is_internal(media_name)) {
		info("mediamgr_stop_media: ignoring internal media %s\n",
		     media_name);
		return;
	}
    
	if (mediamgr_post_media_cmd(mediamgr->mm, MM_MARSHAL_STOP_MEDIA,
				    media_name) != 0) {
		error("mediamgr_stop_media failed \n");
	}
}

void mediamgr_set_call_state_mm(struct mm *mm, enum mediamgr_state state)
{
	struct mm_message *elem;

	if (!mm)
		return;

	info("mediamgr_set_call_state: %s->%s\n",
	     mmstate_name(mm->call_state), mmstate_name(state));
    
	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_set_call_state failed \n");
		return;
	}
	elem->state_elem.state = state;
	if (mqueue_push(mm->mq, MM_MARSHAL_CALL_STATE, elem) != 0) {
		error("mediamgr_set_call_state failed \n");
	}
}


void mediamgr_set_call_state(struct mediamgr *mediamgr,
			     enum mediamgr_state state)
{
	mediamgr_set_call_state_mm(mediamgr_get(mediamgr), state);
}

void mediamgr_register_route_change_h(struct mediamgr *mediamgr,
				      mediamgr_route_changed_h *handler,
				      void *arg)
{
	if (!mediamgr) {
		error("mediamgr_register_route_change_h failed no mm \n");
		return;
	}

	mediamgr->route_changed_h   = handler;
	mediamgr->route_changed_arg = arg;
}


void mediamgr_enable_speaker_mm(struct mm *mm, bool enable)
{
	struct mm_message *elem;
	int err = 0;

	if (!mm)
		return;

	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_enable_speaker failed \n");
		return;
	}
	elem->bool_elem.val = enable;
	err = mqueue_push(mm->mq, MM_MARSHAL_ENABLE_SPEAKER, elem); 
	if (err) {
		error("mediamgr_enable_speaker: failed: %m\n", err);
	}
}

void mediamgr_enable_speaker(struct mediamgr *mediamgr, bool enable)
{
	if (!mediamgr)
		return;

	info("mediamgr_enable_speaker: enable=%s\n",
	     enable ? "true" : "false");

	mediamgr_enable_speaker_mm(mediamgr->mm, enable);
}


void mediamgr_headset_connected(struct mm *mm, bool connected)
{
	struct mm_message *elem;

	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_headset_connected failed \n");
		return;
	}
	elem->bool_elem.val = connected;
	if (mqueue_push(mm->mq, MM_MARSHAL_HEADSET_CONNECTED, elem) != 0) {
		error("mediamgr_headset_connected failed \n");
	}
}


void mediamgr_bt_device_connected(struct mm *mm, bool connected)
{
	struct mm_message *elem;

	if (!mm)
		return;
	
	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_bt_device_connected failed \n");
		return;
	}
	elem->bool_elem.val = connected;
	if (mqueue_push(mm->mq, MM_MARSHAL_BT_DEVICE_CONNECTED, elem) != 0) {
		error("mediamgr_bt_device_connected failed \n");
	}
}

void mediamgr_device_changed(struct mm *mm)
{
	if (!mm)
		return;
    
	if (mqueue_push(mm->mq, MM_MARSHAL_DEVICE_CHANGED, NULL) != 0) {
		error("mediamgr_device_changed failed \n");
	}
}

void mediamgr_register_media(struct mediamgr *mediamgr,
			     const char *media_name,
			     void* media_object,
			     bool mixing,
			     bool incall,
			     int intensity,
			     int priority,
			     bool is_call_media)
{
	struct mm_message *elem;
	struct mm *mm;

	if (!mediamgr)
		return;

	mm = mediamgr->mm;
	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_register_media failed \n");
		return;
	}
	str_ncpy(elem->register_media_elem.media_name,
		 media_name,
		 sizeof(elem->register_media_elem.media_name));
	elem->register_media_elem.media_object = media_object;
	elem->register_media_elem.mixing = mixing;
	elem->register_media_elem.incall = incall;
	elem->register_media_elem.intensity = intensity;
	elem->register_media_elem.priority = priority;
	elem->register_media_elem.is_call_media = is_call_media;
	if (mqueue_push(mm->mq, MM_MARSHAL_REGISTER_MEDIA, elem) != 0) {
		error("mediamgr_register_media failed \n");
	}
}


void mediamgr_unregister_media(struct mediamgr *mediamgr,
			       const char *media_name)
{
	struct mm_message *elem;
	struct mm *mm;

	if (!mediamgr)
		return;

	mm = mediamgr->mm;	
	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_unregister_media failed \n");
		return;
	}
	debug("%s: \n", __FUNCTION__);
	strncpy(elem->register_media_elem.media_name, media_name,
		sizeof(elem->register_media_elem.media_name) - 1);
	elem->register_media_elem.media_object = NULL;
	if (mqueue_push(mm->mq, MM_MARSHAL_DEREGISTER_MEDIA, elem) != 0) {
		error("mediamgr_unregister_media failed \n");
	}
}


void mediamgr_set_sound_mode(struct mediamgr *mediamgr,
			     enum mediamgr_sound_mode mode)
{
	struct mm_message *elem;
	struct mm *mm;
	int intensity = MM_INTENSITY_THRES_NONE;

	if (!mediamgr)
		return;

	info("mediamgr: set_sound_mode: set mode to %u\n", intensity);
	
	mm = mediamgr->mm;
	switch (mode) {

	case MEDIAMGR_SOUND_MODE_ALL:
		intensity =  MM_INTENSITY_THRES_ALL;
		break;

	case MEDIAMGR_SOUND_MODE_SOME:
		intensity =  MM_INTENSITY_THRES_SOME;
		break;

	case MEDIAMGR_SOUND_MODE_NONE:
		intensity =  MM_INTENSITY_THRES_NONE;
		break;
	}

	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_set_sound_mode failed \n");
		return;
	}
	elem->set_intensity_elem.intensity = intensity;
	if (mqueue_push(mm->mq, MM_MARSHAL_SET_INTENSITY, elem) != 0) {
		error("mediamgr_set_sound_mode failed \n");
	}
}

void mediamgr_set_user_starts_audio(struct mediamgr *mediamgr, bool enable)
{
	struct mm_message *elem;
	struct mm *mm;
    
	if (!mediamgr)
		return;
    
	mm = mediamgr->mm;
    
	elem = mem_zalloc(sizeof(struct mm_message), NULL);
	if (!elem) {
		error("mediamgr_enable_speaker failed \n");
		return;
	}
	elem->bool_elem.val = enable;
	if (mqueue_push(mm->mq, MM_MARSHAL_SET_USER_START_AUDIO, elem) != 0) {
		error("mediamgr_set_user_starts_audio failed \n");
	}
}

enum mediamgr_auplay mediamgr_get_route(const struct mediamgr *mediamgr)
{
	return mm_platform_get_route();
}

#if 0
static void stop_media(struct mm *mm, const char *mname)
{
	struct sound *curr_sound;

	curr_sound = dict_lookup(mm->sounds, mname);
	if (!curr_sound) {
		error("%s: couldn't find media %s\n", __FUNCTION__, mname);
	} else {
		mm_platform_stop_sound(curr_sound);
	}
}

static bool is_media_playing(struct mm *mm, const char *mname)
{
	struct sound *curr_sound;
    
	curr_sound = dict_lookup(mm->sounds, mname);
	if (!curr_sound) {
		error("%s: couldn't find media %s\n", __FUNCTION__, mname);
		return false;
	} else {
		return mm_platform_is_sound_playing(curr_sound);
	}
}
#endif


static void set_state(struct mm *mm, enum mediamgr_state state)
{
	enum mediamgr_state old_state = mm->call_state;

	info("mediamgr: set_state: %s -> %s\n",
	     mmstate_name(old_state), mmstate_name(state));

	mm->call_state = state;
}


static void sys_incoming_handler(struct mm *mm)
{
	info("mediamgr: sys_incoming: call_state=%s\n",
	     mmstate_name(mm->call_state));

	set_sys_state(mm, MM_SYS_STATE_INCOMING);
	
	switch (mm->call_state) {
	case MEDIAMGR_STATE_INCOMING_AUDIO_CALL:
		/* Let "ringing_from_them" be played by UI
		 * play_sound(mm, "ringing_from_them", false);
		 */
		break;
		
	case MEDIAMGR_STATE_INCOMING_VIDEO_CALL:
		/* Let "ringing_from_them" be played by UI
		 * play_sound(mm, "ringing_from_them_video", false);
		 */
		break;

	default:
		break;
	}	
}


static void sys_entered_call_handler(struct mm *mm)
{
	info("mediamgr: sys_entered_call: sys_state=%s call_state=%s\n",
	     mm_sys_state_name(mm->sys_state), mmstate_name(mm->call_state));

	if (mm->sys_state == MM_SYS_STATE_INCALL) {
		if (mm->call_state == MEDIAMGR_STATE_INCALL
		 || mm->call_state == MEDIAMGR_STATE_INVIDEOCALL) {
			info("mediamgr: sys_entered call: incall aio=%p\n",
			     mm->aio);
			if (NULL == mm->aio)
				incall_action(mm);
			return;
		}
	}
		
	if (mm->sys_state == MM_SYS_STATE_INCALL
	    && mm->call_state != MEDIAMGR_STATE_RESUME) {		
		info("mediamgr: already INCALL and not resuming\n");
		return;
	}

	if (mm->sys_state != MM_SYS_STATE_ENTERING_CALL) {
		info("mediamgr: not requested to enter call, ignoring\n");
		return;
	}

	set_sys_state(mm, MM_SYS_STATE_INCALL);

	switch (mm->call_state) {
	case MEDIAMGR_STATE_OUTGOING_AUDIO_CALL:
		play_sound(mm, "ringing_from_me", false, true);
		break;
		
	case MEDIAMGR_STATE_OUTGOING_VIDEO_CALL:
		if (!mm->router.bt_device_is_connected
		    && !mm->router.wired_hs_is_connected) {
			enable_speaker(mm, true);
		}
		play_sound(mm, "ringing_from_me_video", false, true);
		break;

	case MEDIAMGR_STATE_INCALL:
	case MEDIAMGR_STATE_INVIDEOCALL:
		incall_action(mm);
		break;

	case MEDIAMGR_STATE_RESUME:
		if (mm->aio)
			fire_callback(mm);
		else {
			mediamgr_post_media_cmd(mm,
						MM_MARSHAL_AUDIO_ALLOC,
						NULL);
		}
		set_state(mm, mm->hold_state);
		break;

	case MEDIAMGR_STATE_NORMAL:
		exit_call(mm);
		break;

	default:
		break;
	}	
}

static void sys_left_call_handler(struct mm *mm)
{
	info("mediamgr: sys_left_call: call_state=%s sys_state=%s\n",
	     mmstate_name(mm->call_state), mm_sys_state_name(mm->sys_state));

	if (mm->sys_state == MM_SYS_STATE_ENTERING_CALL) {
		info("mediamgr: sys_left_call: ignoring since we are "
		     "entering call\n");
	}
	else {
		set_sys_state(mm, MM_SYS_STATE_NORMAL);
		if (mm->call_state == MEDIAMGR_STATE_NORMAL) {
			mm->router.prefer_loudspeaker = false;
			update_route(mm, MM_DEVICE_CHANGED);
		}
	}
}


static void audio_alloc(struct mm *mm)
{

	if (mm->aio) {
		voe_deregister_adm();
		mm->aio = mem_deref(mm->aio);
	}
	if (mm->aio) {
		info("mediamgr: audio already allocated\n");
		return;
	}

	info("mediamgr: allocating audio\n");
	audio_io_alloc(&mm->aio, AUDIO_IO_MODE_NORMAL);
	voe_register_adm(mm->aio);
	mm->alloc_pending = false;

	info("mediamgr: audio_alloc: fire_callback\n");	
	fire_callback(mm);

}


static void audio_release(struct mm *mm)
{
	info("mediamgr: audio_release: aio=%p\n", mm->aio);
	
	if (mm->aio) {
		voe_deregister_adm();
		mm->aio = mem_deref(mm->aio);
	}	
}


void mediamgr_audio_release_mm(struct mm *mm)
{
	mediamgr_post_media_cmd(mm, MM_MARSHAL_AUDIO_RELEASE, NULL);
}


void mediamgr_audio_release(struct mediamgr *mediamgr)
{
	if (!mediamgr)
		return;
	
	mediamgr_audio_release_mm(mediamgr->mm);
}


static void audio_reset(struct mm *mm)
{
	info("mediamgr: audio_reset: aio=%p\n", mm->aio);
	
	if (mm->aio) {
		audio_io_reset(mm->aio);
	}
}

void mediamgr_audio_reset_mm(struct mm *mm)
{
	mediamgr_post_media_cmd(mm, MM_MARSHAL_AUDIO_RESET, NULL);
}


void mediamgr_audio_reset(struct mediamgr *mediamgr)
{
	if (!mediamgr)
		return;

	if (mediamgr->mm) {
		mediamgr_audio_reset_mm(mediamgr->mm);
	//	mediamgr->mm->should_reset = true;
	}
}

static void call_state_handler(struct mm *mm, enum mediamgr_state new_state)
{
	enum mediamgr_state old_state = mm->call_state;

	info("mediamgr: call_state_handler: %s->%s\n",
	     mmstate_name(old_state), mmstate_name(new_state));

	if (new_state == old_state) {
		if (new_state == MEDIAMGR_STATE_INVIDEOCALL ||
		    new_state == MEDIAMGR_STATE_INCALL) {
			if (mm->aio)
				fire_callback(mm);
		}

		return;
	}

	switch (new_state) {
	case MEDIAMGR_STATE_NORMAL:
		set_state(mm, new_state);
		stop_all_media(mm);
		audio_release(mm);
		msystem_set_muted(false);		
		switch (mm->sys_state) {
		case MM_SYS_STATE_NORMAL:
			break;
			
		case MM_SYS_STATE_INCALL:
			if (old_state == MEDIAMGR_STATE_INCALL ||
			    old_state == MEDIAMGR_STATE_INVIDEOCALL) {
				play_sound(mm, "talk_later", true, false);
			}
			exit_call(mm);
			break;

		default:
			exit_call(mm);			
			break;
		}
		mm->router.prefer_loudspeaker = false;
		break;

	case MEDIAMGR_STATE_OUTGOING_AUDIO_CALL:
		switch(old_state) {
		case MEDIAMGR_STATE_NORMAL:
			set_state(mm, new_state);
			enter_call(mm);
			break;

		case MEDIAMGR_STATE_RESUME:
			mm->hold_state = MEDIAMGR_STATE_OUTGOING_AUDIO_CALL;
			enter_call(mm);
			break;

		case MEDIAMGR_STATE_HOLD:
			if (mm->sys_state == MM_SYS_STATE_INCALL)
				play_sound(mm, "ringing_from_me", false, true);
			break;

		default:
			break;
		}
		break;
				
	case MEDIAMGR_STATE_OUTGOING_VIDEO_CALL:
		mm->router.prefer_loudspeaker = true;
		switch(old_state) {
		case MEDIAMGR_STATE_NORMAL:
			set_state(mm, new_state);
			enter_call(mm);
			break;
			
		case MEDIAMGR_STATE_RESUME:
			mm->hold_state = MEDIAMGR_STATE_OUTGOING_VIDEO_CALL;
			enter_call(mm);
			break;

		case MEDIAMGR_STATE_HOLD:
			if (mm->sys_state == MM_SYS_STATE_INCALL) {
				if (!mm->router.bt_device_is_connected
				    && !mm->router.wired_hs_is_connected) {
					enable_speaker(mm, true);
				}
				play_sound(mm, "ringing_from_me_video",
					   false, true);
			}
			break;

		default:
			break;
		}
		break;

	case MEDIAMGR_STATE_INCOMING_AUDIO_CALL:
		if (old_state == MEDIAMGR_STATE_NORMAL) {
			set_state(mm, new_state);
			enter_incoming(mm);
		}
		else if (old_state == MEDIAMGR_STATE_RESUME) {
			mm->hold_state = MEDIAMGR_STATE_INCOMING_AUDIO_CALL;
			enter_incoming(mm);
		}
		break;

	case MEDIAMGR_STATE_INCOMING_VIDEO_CALL:
		if (old_state == MEDIAMGR_STATE_NORMAL)
			set_state(mm, new_state);
		else if (old_state == MEDIAMGR_STATE_RESUME)
			mm->hold_state = MEDIAMGR_STATE_INCOMING_VIDEO_CALL;
		enter_incoming(mm);
		break;
		
	case MEDIAMGR_STATE_INCALL:
	case MEDIAMGR_STATE_INVIDEOCALL:
		set_state(mm, new_state);
		if (old_state == MEDIAMGR_STATE_INCALL) {
			if (new_state == MEDIAMGR_STATE_INVIDEOCALL)
				set_video_route(mm);
			if (mm->aio)
				fire_callback(mm);
		}
		else if (old_state == MEDIAMGR_STATE_INVIDEOCALL) {
			if (mm->aio)
				fire_callback(mm);
		}
		else {
			handle_incall(mm, old_state == MEDIAMGR_STATE_HOLD);
		}
		break;

	case MEDIAMGR_STATE_ROAMING:
		break;

	case MEDIAMGR_STATE_HOLD:
		mm->hold_state = old_state;
		set_state(mm, new_state);
		fire_callback(mm);
		break;

	case MEDIAMGR_STATE_RESUME:
		if (old_state == MEDIAMGR_STATE_HOLD) {
			set_state(mm, new_state);
			if (mm->hold_state == MEDIAMGR_STATE_INCALL
			    || mm->hold_state == MEDIAMGR_STATE_INVIDEOCALL) {
				if (mm->sys_state == MM_SYS_STATE_INCALL) {
					if (mm->aio)
						fire_callback(mm);
					set_state(mm, mm->hold_state);
				}
				else {
					enter_call(mm);
				}
			}
		}
		break;
		
	case MEDIAMGR_STATE_UNKNOWN:
		break;

	default:
		break;
	}
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct mm *mm = arg;
	struct mm_message *msg = data;
	struct sound *curr_sound;
    
	switch ((mm_marshal_id)id) {
            
	case MM_MARSHAL_EXIT:
		re_cancel();
		break;

	case MM_MARSHAL_PLAY_MEDIA:
		play_sound(mm, msg->media_elem.media_name, false, false);
		break;
		
	case MM_MARSHAL_PAUSE_MEDIA: {
		const char *mname = msg->media_elem.media_name;

		curr_sound = dict_lookup(mm->sounds, mname);
		if (!curr_sound) {
			error("%s: couldn't find media %s\n",
			      __FUNCTION__, mname);
		}
		else {
			mm_platform_pause_sound(curr_sound);
		}
	}
		break;

	case MM_MARSHAL_STOP_MEDIA: {
		const char *mname = msg->media_elem.media_name;

		curr_sound = dict_lookup(mm->sounds, mname);
		if (!curr_sound) {
			error("%s: couldn't find media %s\n",
			      __FUNCTION__, mname);
		}
		else {
			mm_platform_stop_sound(curr_sound);
		}
	}
		break;

	case MM_MARSHAL_CALL_STATE: {
		call_state_handler(mm, msg->state_elem.state);
	}
	break;
            
	case MM_MARSHAL_ENABLE_SPEAKER: {
		bool enable = msg->bool_elem.val;

		enable_speaker(g_mm, enable);
	}
	break;

	case MM_MARSHAL_HEADSET_CONNECTED: {
		bool connected = msg->bool_elem.val;

		update_route(g_mm, connected ? MM_HEADSET_PLUGGED
			                     : MM_HEADSET_UNPLUGGED);
	}
	break;

	case MM_MARSHAL_BT_DEVICE_CONNECTED: {
		bool connected = msg->bool_elem.val;
		
		update_route(g_mm, connected ? MM_BT_DEVICE_CONNECTED
			                     : MM_BT_DEVICE_DISCONNECTED);
	}
	break;
	
	case MM_MARSHAL_DEVICE_CHANGED: {
		update_route(g_mm, MM_DEVICE_CHANGED);
	}
	break;
            
	case MM_MARSHAL_REGISTER_MEDIA: {
		const char *mname = msg->register_media_elem.media_name;
		void *mobject = msg->register_media_elem.media_object;
		bool mixing = msg->register_media_elem.mixing;
		bool incall = msg->register_media_elem.incall;
		int intensity = msg->register_media_elem.intensity;
		int priority = msg->register_media_elem.priority;
		int is_call_media = msg->register_media_elem.is_call_media;

		mm_platform_registerMedia(mm->sounds, mname, mobject, mixing,
					  incall, intensity, priority,
					  is_call_media);
	}
	break;

	case MM_MARSHAL_DEREGISTER_MEDIA: {
		const char *mname = msg->register_media_elem.media_name;

		mm_platform_unregisterMedia(mm->sounds, mname);
	}
	break;
            
	case MM_MARSHAL_SET_INTENSITY: {
		mm->intensity_thres = msg->set_intensity_elem.intensity;
	}
	break;
            
	case MM_MARSHAL_SET_USER_START_AUDIO: {
		mm->user_starts_audio = msg->bool_elem.val;
	}
	break;

	case MM_MARSHAL_ENTER_CALL:
		enter_call(mm);
		break;		

	case MM_MARSHAL_AUDIO_ALLOC:
		audio_alloc(mm);
		break;

	case MM_MARSHAL_AUDIO_RELEASE:
		audio_release(mm);
		break;

	case MM_MARSHAL_AUDIO_RESET:
		audio_reset(mm);
		break;
		
	case MM_MARSHAL_EXIT_CALL:
		exit_call(mm);
		break;

	case MM_MARSHAL_SYS_INCOMING:
		sys_incoming_handler(mm);
		break;
		
	case MM_MARSHAL_SYS_ENTERED_CALL:
		sys_entered_call_handler(mm);
		break;

	case MM_MARSHAL_SYS_LEFT_CALL:
		sys_left_call_handler(mm);
		break;

	case MM_MARSHAL_INVOKE_INCOMINGH:
		if (msg->incomingh.incomingh) {
			msg->incomingh.incomingh(msg->incomingh.convid,
						 msg->incomingh.msg_time,
						 msg->incomingh.userid,
						 msg->incomingh.video_call,
						 msg->incomingh.should_ring,
						 msg->incomingh.arg);
		}
		break;

	case MM_MARSHAL_START_RECORDING:
		mm_platform_start_recording(msg->rec_elem);
		break;

	case MM_MARSHAL_STOP_RECORDING:
		mm_platform_stop_recording();
		break;
	}
    
	mem_deref(data);
}


static void *mediamgr_thread(void *arg)
{
	struct mm *mm = arg;
	int err;

	mm->started = true;
	
#ifdef MM_USE_THREAD
	info("mediamgr: thread started\n");
	err = re_thread_init();
	if (err) {
		warning("mediamgr_thread: re_thread_init failed (%m)\n", err);
		goto out;
	}
#else
	info("mediamgr: NO thread started\n");
	re_thread_enter();
#endif

	err = mqueue_alloc(&mm->mq, mqueue_handler, mm);
	if (err) {
		error("mediamgr_thread: cannot allocate mqueue (%m)\n", err);
		goto out;
	}

	info("mediamgr_thread: read %d sounds\n", dict_count(mm->sounds));

	mm->sys_state = MM_SYS_STATE_NORMAL;
	if (mm_platform_init(mm, mm->sounds) != 0) {
		error("mediamgr_thread: failed to init platform\n");
		goto out;
	}
	     
#ifdef MM_USE_THREAD
	re_main(NULL);
	info("mediamgr_thread: thread exiting\n");

	re_thread_close();
#else
	re_tread_leave();
#endif

out:
	return NULL;
}


void mediamgr_sys_entered_call(struct mm *mm)
{
	info("mediamgr: posting entered_call\n");

	mediamgr_post_media_cmd(mm, MM_MARSHAL_SYS_ENTERED_CALL, NULL);
}


void mediamgr_sys_incoming(struct mm *mm)
{
	mediamgr_post_media_cmd(mm, MM_MARSHAL_SYS_INCOMING, NULL);
}


void mediamgr_sys_left_call(struct mm *mm)
{
	mediamgr_post_media_cmd(mm, MM_MARSHAL_SYS_LEFT_CALL, NULL);
}


static void invoke_destructor(void *arg)
{
	struct mm_message *elem = arg;
	
	mem_deref(elem->incomingh.convid);
	mem_deref(elem->incomingh.userid);
}


int mediamgr_invoke_incomingh(struct mediamgr *mediamgr,
			      mediamgr_incoming_h *incomingh,
			      const char *convid, uint32_t msg_time,
			      const char *userid, int video_call,
			      int should_ring,
			      void *arg)
{

	struct mm_message *elem = NULL;

	elem = mem_zalloc(sizeof(*elem), invoke_destructor);
	if (!elem)
		return ENOMEM;

	str_dup(&elem->incomingh.convid, convid);
	str_dup(&elem->incomingh.userid, userid);
	elem->incomingh.incomingh = incomingh;
	elem->incomingh.msg_time = msg_time;
	elem->incomingh.video_call = video_call;
	elem->incomingh.should_ring = should_ring;
	elem->incomingh.arg = arg;

	return mqueue_push(mediamgr->mm->mq, MM_MARSHAL_INVOKE_INCOMINGH, elem);
}

void mediamgr_hold_and_resume(struct mm *mm)
{
	if (!mm)
		return;
	
	if (mm->call_state == MEDIAMGR_STATE_INCALL
	    || mm->call_state == MEDIAMGR_STATE_INVIDEOCALL) {
		mediamgr_set_call_state_mm(mm,
					   MEDIAMGR_STATE_HOLD);
		mediamgr_set_call_state_mm(mm,
					   MEDIAMGR_STATE_RESUME);
	}
}


bool mediamgr_get_speaker(struct mm *mm)
{
	bool speaker;

	speaker = mm ? mm->router.prefer_loudspeaker : false;

	info("mediamgr(%p): get_speaker: speaker=%s\n",
	     mm, speaker ? "yes": "no");

	return speaker;
}


void mediamgr_override_speaker_mm(struct mm *mm, bool speaker)
{
	info("mediamgr(%p): override_speaker: speaker=%s\n",
	     mm, speaker ? "yes" : "no");
	
	if (!mm)
		return;

	mm->router.prefer_loudspeaker = speaker;
}

static bool reset_sound_handler(char *key, void *val, void *arg)
{
	struct mm *mm = arg;	
	struct sound *snd = val;

	if (!snd)
		return false;

	(void)mm;

	mm_platform_reset_sound(snd);

	return false;
}


void mediamgr_reset_sounds(struct mm *mm)
{
	if (!mm)
		return;

	dict_apply(mm->sounds, reset_sound_handler, mm);
}


void mediamgr_start_recording(struct mediamgr *mediamgr,
			      mediamgr_start_rec_h *rech, void *arg)
{
	struct mm_message *elem;
	
	if (!mediamgr || !mediamgr->mm)
		return;

#if 1
	elem = mem_zalloc(sizeof(*elem), NULL);
	if (!elem) {
		error("mediamgr_start_recording: elem failed\n");
		return;
	}
	elem->rec_elem = mem_zalloc(sizeof(*(elem->rec_elem)), NULL);
	if (!elem->rec_elem) {
		error("mediamgr_start_recording: elem->rech failed\n");
		goto out;
	}
		
	elem->rec_elem->rech = rech;
	elem->rec_elem->arg = arg;
	if (mqueue_push(mediamgr->mm->mq,
			MM_MARSHAL_START_RECORDING, elem) != 0) {
		error("mediamgr_set_call_state failed \n");
		goto out;
	}
	return;

 out:
	mem_deref(elem->rec_elem);
#endif
	  
	  //mm_platform_start_recording();
}


void mediamgr_stop_recording(struct mediamgr *mediamgr)
{
	if (!mediamgr)
		return;

	mediamgr_post_media_cmd(mediamgr->mm, MM_MARSHAL_STOP_RECORDING, NULL);
}

bool mediamgr_should_reset(struct mm *mm)
{
	if (mm && mm->should_reset) {
		mm->should_reset = false;
		return true;
	}

	return false;
}
