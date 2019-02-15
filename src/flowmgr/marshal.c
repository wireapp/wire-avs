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
#include <stdlib.h>
#include <unistd.h>

#include <re/re.h>
#include <avs.h>

#include "flowmgr.h"


struct {
	struct mqueue *mq;
} marshal = {
	.mq = NULL
};


enum marshal_id {
	MARSHAL_ALLOC,
	MARSHAL_START,
	MARSHAL_FREE,
	MARSHAL_AUPLAY,
	MARSHAL_SET_MUTE,
	MARSHAL_GET_MUTE,
	MARSHAL_CAN_SEND_VIDEO,
	MARSHAL_IS_SENDING_VIDEO,
	MARSHAL_SET_VIDEO_SEND_STATE,
	MARSHAL_SET_AUDIO_STATE_HANDLER,
};

struct marshal_elem {
	int id;
	struct flowmgr *fm;
	bool handled;
	int ret;
};

struct marshal_alloc_elem {
	struct marshal_elem a;

	struct flowmgr **fmp;
	flowmgr_req_h *reqh;
	flowmgr_err_h *errh;
	void *arg;
};

struct marshal_setactive_elem {
	struct marshal_elem a;

	const char *convid;
	bool active;
};

struct marshal_auplay_elem {
	struct marshal_elem a;

	enum flowmgr_auplay aplay;
};

struct marshal_mute_elem {
	struct marshal_elem a;
	
	bool *mute;
};


struct marshal_enable_elem {
	struct marshal_elem a;
	
	bool enable;
};

struct marshal_sessid_elem {
	struct marshal_elem a;
	
	const char *convid;
	const char *sessid;
};

struct marshal_useradd_elem {
	struct marshal_elem a;

	const char *convid;
	const char *userid;
	const char *name;
};


struct marshal_accesstoken_elem {
	struct marshal_elem a;

	const char *token;
	const char *type;
};	


struct marshal_selfuserid_elem {
	struct marshal_elem a;

	const char *userid;
};

struct marshal_video_capture_list_elem {
	struct marshal_elem a;

	struct list **devlist;
};

struct marshal_video_state_elem {
	struct marshal_elem a;

	const char *convid;
	enum flowmgr_video_send_state state;
};

struct marshal_video_can_send_elem {
	struct marshal_elem a;
	
	const char *convid;
};

struct marshal_video_is_sending_elem {
	struct marshal_elem a;
	
	const char *convid;
	const char *partid;
};

struct marshal_audio_state_handler_elem {
	struct marshal_elem a;
    
	flowmgr_audio_state_change_h *audio_state_change_handler;
	void *arg;
};


static void mqueue_handler(int id, void *data, void *arg)
{
	struct marshal_elem *me = data;

	(void)arg;

	switch((enum marshal_id)id) {
	case MARSHAL_ALLOC: {
		struct marshal_alloc_elem *mae = data;

		me->ret = flowmgr_alloc(&me->fm, mae->reqh, mae->errh,
					mae->arg);
		break;
	}

	case MARSHAL_START: {
		me->ret = flowmgr_start();
		break;
	}

	case MARSHAL_FREE: {
		flowmgr_free(me->fm);
		break;
	}

	case MARSHAL_AUPLAY: {
		struct marshal_auplay_elem *mae = data;
		
		me->ret = flowmgr_auplay_changed(me->fm, mae->aplay);
		break;
	}

	case MARSHAL_SET_MUTE: {
		struct marshal_mute_elem *mme = data;

		me->ret = flowmgr_set_mute(me->fm, *mme->mute);
		break;
	}

	case MARSHAL_GET_MUTE: {
		struct marshal_mute_elem *mme = data;

		me->ret = flowmgr_get_mute(me->fm, mme->mute);
		break;
	}

	case MARSHAL_SET_VIDEO_SEND_STATE: {
		struct marshal_video_state_elem *mse = data;

		flowmgr_set_video_send_state(me->fm, mse->convid, mse->state);
		break;
	}

	case MARSHAL_CAN_SEND_VIDEO: {
		struct marshal_video_can_send_elem *mse = data;

		me->ret = flowmgr_can_send_video(me->fm, mse->convid);
		break;
	}

	case MARSHAL_IS_SENDING_VIDEO: {
		struct marshal_video_is_sending_elem *mse = data;

		me->ret = flowmgr_is_sending_video(me->fm,
						   mse->convid, mse->partid);
		break;
	}
		
	case MARSHAL_SET_AUDIO_STATE_HANDLER: {
		struct marshal_audio_state_handler_elem *mse = data;
            
		flowmgr_set_audio_state_handler(me->fm,
			mse->audio_state_change_handler,
			mse->arg);
		break;
	}
            
	}

	me->handled = true;
}


static void marshal_wait(struct marshal_elem *me)
{
	/* Using a semaphore would be more efficient,
	 * but the Darwin implementation of it is really
	 * not suitable for short-lived semaphores
	 */
	while(!me->handled)
		usleep(40000);
}


int marshal_init(void)
{
	int err;
        
	if (marshal.mq)
		return EALREADY;

	err = mqueue_alloc(&marshal.mq, mqueue_handler, NULL);

	return err;
}


void marshal_close(void)
{
	marshal.mq = mem_deref(marshal.mq);
}


static void marshal_send(void *arg)
{
	struct marshal_elem *me = arg;

	if (!marshal.mq) {
		warning("flowmgr: marshal_send: no mq\n");
		return;
	}

	me->handled = false;
	mqueue_push(marshal.mq, me->id, me);
	marshal_wait(me);
}


int marshal_flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
			  flowmgr_err_h *errh, void *arg)
{
	struct marshal_alloc_elem me;

	me.a.id = MARSHAL_ALLOC;
	me.a.fm = NULL;

	me.reqh = reqh;
	me.errh = errh;
	me.arg = arg;

	marshal_send(&me);

	if (me.a.ret == 0) {
		*fmp = me.a.fm;
	}

	return me.a.ret;
}


int marshal_flowmgr_start(void)
{
	struct marshal_elem me;

	me.id = MARSHAL_START;
	me.fm = NULL;

	marshal_send(&me);

	return me.ret;
}


void marshal_flowmgr_free(struct flowmgr *fm)
{
	struct marshal_elem me;

	me.id = MARSHAL_FREE;
	me.fm = fm;

	marshal_send(&me);
}


int marshal_flowmgr_auplay_changed(struct flowmgr *fm,
				   enum flowmgr_auplay aplay)
{
	struct marshal_auplay_elem me;

	me.a.id = MARSHAL_AUPLAY;
	me.a.fm = fm;

	me.aplay = aplay;
	
	marshal_send(&me);

	return me.a.ret;
}


int marshal_flowmgr_set_mute(struct flowmgr *fm, bool mute)
{
	struct marshal_mute_elem me;

	me.a.id = MARSHAL_SET_MUTE;
	me.a.fm = fm;

	me.mute = &mute;
	
	marshal_send(&me);

	return me.a.ret;
}


int marshal_flowmgr_get_mute(struct flowmgr *fm, bool *muted)
{
	struct marshal_mute_elem me;

	me.a.id = MARSHAL_GET_MUTE;
	me.a.fm = fm;

	me.mute = muted;
	
	marshal_send(&me);

	return me.a.ret;
}


void marshal_flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state)
{
	struct marshal_video_state_elem me;

	me.a.id = MARSHAL_SET_VIDEO_SEND_STATE;
	me.a.fm = fm;

	me.convid = convid;
	me.state = state;

	marshal_send(&me);
}

int marshal_flowmgr_can_send_video(struct flowmgr *fm, const char *convid)
{
	struct marshal_video_can_send_elem me;

	me.a.id = MARSHAL_CAN_SEND_VIDEO;
	me.a.fm = fm;

	me.convid = convid;

	marshal_send(&me);

	return me.a.ret;
}

int marshal_flowmgr_is_sending_video(struct flowmgr *fm,
				     const char *convid, const char *partid)
{
	struct marshal_video_is_sending_elem me;

	me.a.id = MARSHAL_IS_SENDING_VIDEO;
	me.a.fm = fm;

	me.convid = convid;
	me.partid = partid;

	marshal_send(&me);

	return me.a.ret;
}

void marshal_flowmgr_set_audio_state_handler(struct flowmgr *fm,
	flowmgr_audio_state_change_h *audio_state_change_handler,
	void *arg)
{
	struct marshal_audio_state_handler_elem me;
    
	me.a.id = MARSHAL_SET_AUDIO_STATE_HANDLER;
	me.a.fm = fm;
    
	me.audio_state_change_handler = audio_state_change_handler;
	me.arg = arg;
    
	marshal_send(&me);
}

