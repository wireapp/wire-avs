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
	MARSHAL_MEDIA_HANDLERS,
	MARSHAL_MEDIA_ESTAB_HANDLER,
	MARSHAL_CONF_POS_HANDLER,
	MARSHAL_LOG_HANDLERS,
	MARSHAL_RESP,
	MARSHAL_EVENT,
	MARSHAL_ACQUIRE,
	MARSHAL_RELEASE,
	MARSHAL_SET_ACTIVE,
	MARSHAL_USER_ADD,
	MARSHAL_ACCESS_TOKEN,
	MARSHAL_SELF_USERID,
	MARSHAL_MCAT,
	MARSHAL_HAS_MEDIA,
	MARSHAL_AUSRC,
	MARSHAL_AUPLAY,
	MARSHAL_NETWORK,
	MARSHAL_SET_MUTE,
	MARSHAL_GET_MUTE,
	MARSHAL_CONVLOG,
	MARSHAL_ENABLE_METRICS,
	MARSHAL_ENABLE_LOGGING,
	MARSHAL_SET_SESSID,
	MARSHAL_INTERRUPTION,
	MARSHAL_BACKGROUND,
    MARSHAL_VM_START_RECORD,
    MARSHAL_VM_STOP_RECORD,
    MARSHAL_VM_START_PLAY,
    MARSHAL_VM_STOP_PLAY,
#if HAVE_VIDEO
	MARSHAL_SET_VIDEO_PREVIEW,
	MARSHAL_SET_VIDEO_VIEW,
	MARSHAL_GET_VIDEO_CAPTURE_DEVICES,
	MARSHAL_SET_VIDEO_CAPTURE_DEVICE,
	MARSHAL_CAN_SEND_VIDEO,
	MARSHAL_IS_SENDING_VIDEO,
	MARSHAL_SET_VIDEO_SEND_STATE,
	MARSHAL_SET_VIDEO_HANDLERS,
#endif
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

struct marshal_media_handlers_elem {
	struct marshal_elem a;

	flowmgr_mcat_chg_h *cath;
	flowmgr_volume_h *volh;
	void *arg;
};

struct marshal_media_estab_handler_elem {
	struct marshal_elem a;

	flowmgr_media_estab_h *mestabh;
	void *arg;
};

struct marshal_conf_pos_handler_elem {
	struct marshal_elem a;

	flowmgr_conf_pos_h *conf_posh;
	void *arg;
};

struct marshal_log_handlers_elem {
	struct marshal_elem a;

	flowmgr_log_append_h *appendh;
	flowmgr_log_upload_h *uploadh;
	void *arg;
};

struct marshal_resp_elem {
	struct marshal_elem a;

	int status;
	const char *reason;
	const char *ctype;
	const char *content;
	size_t clen;
	struct rr_resp *rr;
};

struct marshal_event_elem {
	struct marshal_elem a;

	bool *hp;
	const char *ctype;
	const char *content;
	size_t clen;
};

struct marshal_acquire_elem {
	struct marshal_elem a;

	struct flowmgr *fm;
	const char *convid;
	const char *sessid;
	flowmgr_netq_h *qh;
	void *arg;
};

struct marshal_release_elem {
	struct marshal_elem a;

	const char *convid;
};

struct marshal_setactive_elem {
	struct marshal_elem a;

	const char *convid;
	bool active;
};

struct marshal_mcat_elem {
	struct marshal_elem a;
	
	const char *convid;
	enum flowmgr_mcat cat;
};

struct marshal_has_media {
	struct marshal_elem a;
	
	const char *convid;
	bool has_media;
};

struct marshal_ausrc_elem {
	struct marshal_elem a;

	enum flowmgr_ausrc asrc;
};

struct marshal_auplay_elem {
	struct marshal_elem a;

	enum flowmgr_auplay aplay;
};

struct marshal_mute_elem {
	struct marshal_elem a;
	
	bool *mute;
};


struct marshal_convlog {
	struct marshal_elem a;
	
	const char *convid;
	const char *msg;
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

struct marshal_interruption_elem {
	struct marshal_elem a;

	const char *convid;
	bool interrupted;
};

struct marshal_background_elem {
	struct marshal_elem a;

	enum media_bg_state bgst;
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


struct marshal_vm_start_record_elem {
	struct marshal_elem a;
    const char *fileNameUTF8;
};

struct marshal_vm_stop_record_elem {
    struct marshal_elem a;
};

struct marshal_vm_start_play_elem {
    struct marshal_elem a;
    const char *fileNameUTF8;
    int start_time_ms;
    flowmgr_vm_play_status_h *handler;
    void *arg;
};

struct marshal_vm_stop_play_elem {
    struct marshal_elem a;
};

#if HAVE_VIDEO
struct marshal_video_preview_elem {
	struct marshal_elem a;

	const char *convid;
	void *view;
};

struct marshal_video_view_elem {
	struct marshal_elem a;

	const char *convid;
	const char *partid;
	void *view;
};

struct marshal_video_capture_list_elem {
	struct marshal_elem a;

	struct list **devlist;
};

struct marshal_video_capture_device_elem {
	struct marshal_elem a;

	const char *convid;
	const char *devid;
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


struct marshal_video_handlers_elem {
	struct marshal_elem a;

	flowmgr_create_preview_h *create_preview_handler;
	flowmgr_release_preview_h *release_preview_handler;
	flowmgr_create_view_h *create_view_handler;
	flowmgr_release_view_h *release_view_handler;
	void *arg;
};

#endif

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

	case MARSHAL_MEDIA_HANDLERS: {
		struct marshal_media_handlers_elem *mhe = data;

		flowmgr_set_media_handlers(me->fm, mhe->cath,
					   mhe->volh, mhe->arg);
		break;
	}

	case MARSHAL_MEDIA_ESTAB_HANDLER: {
		struct marshal_media_estab_handler_elem *mme = data;

		flowmgr_set_media_estab_handler(me->fm, mme->mestabh,
						mme->arg);
		break;
	}

	case MARSHAL_CONF_POS_HANDLER: {
		struct marshal_conf_pos_handler_elem *cpe = data;

		flowmgr_set_conf_pos_handler(me->fm, cpe->conf_posh,
					     cpe->arg);
		break;
	}
		
	case MARSHAL_LOG_HANDLERS: {
		struct marshal_log_handlers_elem *mle = data;

		flowmgr_set_log_handlers(me->fm, mle->appendh, mle->uploadh,
					 mle->arg);
		break;
	}
		
	case MARSHAL_RESP: {
		struct marshal_resp_elem *mre = data;

		me->ret = flowmgr_resp(me->fm, mre->status, mre->reason,
				       mre->ctype, mre->content, mre->clen,
				       mre->rr);
		break;
	}

	case MARSHAL_EVENT: {
		struct marshal_event_elem *mee = data;

		me->ret = flowmgr_process_event(mee->hp, me->fm,
						mee->ctype, mee->content,
						mee->clen);		
		break;
	}

	case MARSHAL_ACQUIRE: {
		struct marshal_acquire_elem *mae = data;

		me->ret = flowmgr_acquire_flows(me->fm,
						mae->convid,
						mae->sessid,
						mae->qh, mae->arg);
		break;
	}
	
	case MARSHAL_RELEASE: {
		struct marshal_release_elem *mre = data;
		
		flowmgr_release_flows(me->fm, mre->convid);
		break;
	}

	case MARSHAL_SET_ACTIVE: {
		struct marshal_setactive_elem *mre = data;

		flowmgr_set_active(me->fm, mre->convid, mre->active);
		break;
	}

	case MARSHAL_MCAT: {
		struct marshal_mcat_elem *mme = data;

		flowmgr_mcat_changed(me->fm, mme->convid, mme->cat);
		break;
	}

	case MARSHAL_USER_ADD: {
		struct marshal_useradd_elem *mue = data;

		flowmgr_user_add(me->fm, mue->convid, mue->userid, mue->name);
		break;
	}

		
	case MARSHAL_ACCESS_TOKEN: {
		struct marshal_accesstoken_elem *mae = data;

		flowmgr_refresh_access_token(me->fm, mae->token, mae->type);
		break;
	}

		
	case MARSHAL_SELF_USERID: {
		struct marshal_selfuserid_elem *mse = data;

		flowmgr_set_self_userid(me->fm, mse->userid);
		break;
	}
		

	case MARSHAL_HAS_MEDIA: {
		struct marshal_has_media *mhm = data;

		me->ret = flowmgr_has_media(me->fm, mhm->convid,
					    &mhm->has_media);
		break;
	}


	case MARSHAL_NETWORK: {
		flowmgr_network_changed(me->fm);
		break;
	}
		
	case MARSHAL_AUSRC: {
		struct marshal_ausrc_elem *mae = data;
		
		me->ret = flowmgr_ausrc_changed(me->fm, mae->asrc);
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

	case MARSHAL_CONVLOG: {
		struct marshal_convlog *mcl = data;
		
		me->ret = flowmgr_append_convlog(me->fm, mcl->convid, mcl->msg);
		break;
	}
		
	case MARSHAL_ENABLE_METRICS: {
		struct marshal_enable_elem *mee = data;

		flowmgr_enable_metrics(me->fm, mee->enable);
		break;
	}

	case MARSHAL_ENABLE_LOGGING: {
		struct marshal_enable_elem *mee = data;

		flowmgr_enable_logging(me->fm, mee->enable);
		break;
	}

	case MARSHAL_SET_SESSID: {
		struct marshal_sessid_elem *mse = data;

		flowmgr_set_sessid(me->fm, mse->convid, mse->sessid);
		break;
	}

	case MARSHAL_INTERRUPTION: {
		struct marshal_interruption_elem *mie = data;

		me->ret = flowmgr_interruption(me->fm, mie->convid,
					       mie->interrupted);
		break;
	}

	case MARSHAL_BACKGROUND: {
		struct marshal_background_elem *mbe = data;

		flowmgr_background(me->fm, mbe->bgst);
		break;
	}
		
	case MARSHAL_VM_START_RECORD: {
		struct marshal_vm_start_record_elem *mie = data;

		me->ret = flowmgr_vm_start_record(me->fm, mie->fileNameUTF8);

		break;
	}

	case MARSHAL_VM_STOP_RECORD: {
		me->ret = flowmgr_vm_stop_record(me->fm);

		break;
	}

	case MARSHAL_VM_START_PLAY: {
		struct marshal_vm_start_play_elem *mie = data;
            
		me->ret = flowmgr_vm_start_play(me->fm, mie->fileNameUTF8, mie->start_time_ms, mie->handler, mie->arg);

		break;
	}
            
	case MARSHAL_VM_STOP_PLAY: {
		me->ret = flowmgr_vm_stop_play(me->fm);

		break;
	}

#if HAVE_VIDEO
	case MARSHAL_SET_VIDEO_PREVIEW: {
		struct marshal_video_preview_elem *mse = data;

		flowmgr_set_video_preview(me->fm, mse->convid, mse->view);
		break;
	}

	case MARSHAL_SET_VIDEO_VIEW: {
		struct marshal_video_view_elem *mse = data;

		flowmgr_set_video_view(me->fm, mse->convid, mse->partid, mse->view);
		break;
	}

	case MARSHAL_GET_VIDEO_CAPTURE_DEVICES: {
		struct marshal_video_capture_list_elem *mse = data;

		flowmgr_get_video_capture_devices(me->fm, mse->devlist);
		break;
	}

	case MARSHAL_SET_VIDEO_CAPTURE_DEVICE: {
		struct marshal_video_capture_device_elem *mse = data;

		flowmgr_set_video_capture_device(me->fm, mse->convid, mse->devid);
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
		

	case MARSHAL_SET_VIDEO_HANDLERS: {
		struct marshal_video_handlers_elem *mse = data;

		flowmgr_set_video_handlers(me->fm, 
			mse->create_preview_handler,
			mse->release_preview_handler,
			mse->create_view_handler,
			mse->release_view_handler,
			mse->arg);
		break;
	}
#endif
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



void marshal_flowmgr_set_media_handlers(struct flowmgr *fm,
					flowmgr_mcat_chg_h *cath,  
					flowmgr_volume_h *volh, void *arg)
{
	struct marshal_media_handlers_elem me;

	me.a.id = MARSHAL_MEDIA_HANDLERS;
	me.a.fm = fm;

	me.cath = cath;
	me.volh = volh;
	me.arg = arg;

	marshal_send(&me);
}


void marshal_flowmgr_set_media_estab_handler(struct flowmgr *fm,
					     flowmgr_media_estab_h *mestabh,
					     void *arg)
{
	struct marshal_media_estab_handler_elem me;

	me.a.id = MARSHAL_MEDIA_ESTAB_HANDLER;
	me.a.fm = fm;

	me.mestabh = mestabh;
	me.arg = arg;

	marshal_send(&me);
}


void marshal_flowmgr_set_conf_pos_handler(struct flowmgr *fm,
					  flowmgr_conf_pos_h *conf_posh,
 					  void *arg)
{
	struct marshal_conf_pos_handler_elem me;

	me.a.id = MARSHAL_CONF_POS_HANDLER;
	me.a.fm = fm;

	me.conf_posh = conf_posh;
	me.arg = arg;

	marshal_send(&me);
}


void marshal_flowmgr_set_log_handlers(struct flowmgr *fm,
				      flowmgr_log_append_h *appendh,
				      flowmgr_log_upload_h *uploadh,
				      void *arg)
{
	struct marshal_log_handlers_elem me;

	me.a.id = MARSHAL_LOG_HANDLERS;
	me.a.fm = fm;

	me.appendh = appendh;
	me.uploadh = uploadh;
	me.arg = arg;

	marshal_send(&me);
}


void marshal_flowmgr_free(struct flowmgr *fm)
{
	struct marshal_elem me;

	me.id = MARSHAL_FREE;
	me.fm = fm;

	marshal_send(&me);
}


int marshal_flowmgr_resp(struct flowmgr *fm, int status, const char *reason,
			 const char *ctype, const char *content, size_t clen,
			 struct rr_resp *rr)
{
	struct marshal_resp_elem me;

	me.a.id = MARSHAL_RESP;
	me.a.fm = fm;

	me.status = status;
	me.reason = reason;
	me.ctype = ctype;
	me.content = content;
	me.clen = clen;
	me.rr = rr;

	marshal_send(&me);

	return me.a.ret;
}


int marshal_flowmgr_process_event(bool *hp, struct flowmgr *fm,
				  const char *ctype, const char *content,
				  size_t clen)
{
	struct marshal_event_elem me;

	me.a.id = MARSHAL_EVENT;
	me.a.fm = fm;

	me.hp = hp;
	me.ctype = ctype;
	me.content = content;
	me.clen = clen;

	marshal_send(&me);

	return me.a.ret;
}


int marshal_flowmgr_acquire_flows(struct flowmgr *fm, const char *convid,
				  const char *sessid,
				  flowmgr_netq_h *qh, void *arg)
{
	struct marshal_acquire_elem me;

	me.a.id = MARSHAL_ACQUIRE;
	me.a.fm = fm;

	me.convid = convid;
	me.sessid = sessid;
	me.qh = qh;
	me.arg = arg;

	marshal_send(&me);

	return me.a.ret;	
}


void marshal_flowmgr_release_flows(struct flowmgr *fm, const char *convid)
{
	struct marshal_release_elem me;

	me.a.id = MARSHAL_RELEASE;
	me.a.fm = fm;

	me.convid = convid;

	marshal_send(&me);
}


void marshal_flowmgr_set_active(struct flowmgr *fm, const char *convid,
				bool active)
{
	struct marshal_setactive_elem me;

	me.a.id = MARSHAL_SET_ACTIVE;
	me.a.fm = fm;

	me.convid = convid;
	me.active = active;

	marshal_send(&me);
}


void marshal_flowmgr_user_add(struct flowmgr *fm, const char *convid,
			      const char *userid, const char *name)
{
	struct marshal_useradd_elem me;

	me.a.id = MARSHAL_USER_ADD;
	me.a.fm = fm;

	me.convid = convid;
	me.userid = userid;
	me.name = name;

	marshal_send(&me);
}


void marshal_flowmgr_refresh_access_token(struct flowmgr *fm,
					  const char *token,
					  const char *type)
{
	struct marshal_accesstoken_elem me;

	me.a.id = MARSHAL_ACCESS_TOKEN;
	me.a.fm = fm;

	me.token = token;
	me.type = type;

	marshal_send(&me);
}


void marshal_flowmgr_set_self_userid(struct flowmgr *fm,
				     const char *userid)
{
	struct marshal_selfuserid_elem me;

	me.a.id = MARSHAL_SELF_USERID;
	me.a.fm = fm;

	me.userid = userid;

	marshal_send(&me);
}


void marshal_flowmgr_mcat_changed(struct flowmgr *fm, const char *convid,
				  enum flowmgr_mcat cat)
{
	struct marshal_mcat_elem me;

	me.a.id = MARSHAL_MCAT;
	me.a.fm = fm;

	me.convid = convid;
	me.cat = cat;
	
	marshal_send(&me);
}


bool marshal_flowmgr_has_media(struct flowmgr *fm, const char *convid,
			       bool *has_media)
{
	struct marshal_has_media me;

	me.a.id = MARSHAL_HAS_MEDIA;
	me.a.fm = fm;

	me.convid = convid;
	
	marshal_send(&me);

	*has_media = me.has_media;
	
	return me.a.ret;
}


int marshal_flowmgr_ausrc_changed(struct flowmgr *fm, enum flowmgr_ausrc asrc)
{
	struct marshal_ausrc_elem me;

	me.a.id = MARSHAL_AUSRC;
	me.a.fm = fm;

	me.asrc = asrc;
	
	marshal_send(&me);

	return me.a.ret;
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


int  marshal_flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
				    const char *msg)
{
	struct marshal_convlog me;

	me.a.id = MARSHAL_CONVLOG;
	me.a.fm = fm;

	me.convid = convid;
	me.msg = msg;

	marshal_send(&me);

	return me.a.ret;
}


void marshal_flowmgr_enable_metrics(struct flowmgr *fm, bool metrics)
{
	struct marshal_enable_elem me;

	me.a.id = MARSHAL_ENABLE_METRICS;
	me.a.fm = fm;

	me.enable = metrics;
	
	marshal_send(&me);	
}


void marshal_flowmgr_enable_logging(struct flowmgr *fm, bool logging)
{
	struct marshal_enable_elem me;

	me.a.id = MARSHAL_ENABLE_LOGGING;
	me.a.fm = fm;

	me.enable = logging;
	
	marshal_send(&me);
}


void marshal_flowmgr_set_sessid(struct flowmgr *fm, const char *convid,
				const char *sessid)
{
	struct marshal_sessid_elem me;

	me.a.id = MARSHAL_SET_SESSID;
	me.a.fm = fm;

	me.convid = convid;
	me.sessid = sessid;

	marshal_send(&me);
}

int marshal_flowmgr_interruption(struct flowmgr *fm, const char *convid,
				  bool interrupted)
{
	struct marshal_interruption_elem me;

	me.a.id = MARSHAL_INTERRUPTION;
	me.a.fm = fm;

	me.convid = convid;
	me.interrupted = interrupted;

	marshal_send(&me);

	return me.a.ret;	
}


void marshal_flowmgr_background(struct flowmgr *fm, enum media_bg_state bgst)
{
	struct marshal_background_elem me;

	me.a.id = MARSHAL_BACKGROUND;
	me.a.fm = fm;

	me.bgst = bgst;

	marshal_send(&me);
}


int marshal_flowmgr_vm_start_record(struct flowmgr *fm, const char fileNameUTF8[1024])
{
    struct marshal_vm_start_record_elem me;
    
    me.a.id = MARSHAL_VM_START_RECORD;
    me.a.fm = fm;
    
    me.fileNameUTF8 = fileNameUTF8;
    
    marshal_send(&me);
    
    return me.a.ret;
}

int marshal_flowmgr_vm_stop_record(struct flowmgr *fm)
{
    struct marshal_vm_stop_record_elem me;
    
    me.a.id = MARSHAL_VM_STOP_RECORD;
    me.a.fm = fm;
    
    marshal_send(&me);
    
    return me.a.ret;
}

int marshal_flowmgr_vm_start_play(struct flowmgr *fm, const char fileNameUTF8[1024], int start_time_ms, flowmgr_vm_play_status_h *handler, void *arg)

{
    struct marshal_vm_start_play_elem me;
    
    me.a.id = MARSHAL_VM_START_PLAY;
    me.a.fm = fm;
    me.fileNameUTF8 = fileNameUTF8;
    me.start_time_ms = start_time_ms;
    me.handler = handler;
    me.arg = arg;
    
    marshal_send(&me);
    
    return me.a.ret;
}

int marshal_flowmgr_vm_stop_play(struct flowmgr *fm)
{
    struct marshal_vm_stop_play_elem me;
    
    me.a.id = MARSHAL_VM_STOP_PLAY;
    me.a.fm = fm;
    
    marshal_send(&me);
    
    return me.a.ret;
}


void marshal_flowmgr_network_changed(struct flowmgr *fm)
{
	struct marshal_elem me;

	me.id = MARSHAL_NETWORK;
	me.fm = fm;

	marshal_send(&me);
}


#if HAVE_VIDEO

void marshal_flowmgr_set_video_preview(struct flowmgr *fm, const char *convid, void *view)
{
	struct marshal_video_preview_elem me;

	me.a.id = MARSHAL_SET_VIDEO_PREVIEW;
	me.a.fm = fm;

	me.convid = convid;
	me.view = view;

	marshal_send(&me);
}

void marshal_flowmgr_set_video_view(struct flowmgr *fm, const char *convid, const char *partid, void *view)
{
	struct marshal_video_view_elem me;

	me.a.id = MARSHAL_SET_VIDEO_VIEW;
	me.a.fm = fm;

	me.convid = convid;
	me.partid = partid;
	me.view = view;

	marshal_send(&me);
}


void marshal_flowmgr_get_video_capture_devices(struct flowmgr *fm, struct list **device_list)
{
	struct marshal_video_capture_list_elem me;

	me.a.id = MARSHAL_GET_VIDEO_CAPTURE_DEVICES;
	me.a.fm = fm;

	me.devlist = device_list;

	marshal_send(&me);
}


void marshal_flowmgr_set_video_capture_device(struct flowmgr *fm, const char *convid, const char *devid)
{
	struct marshal_video_capture_device_elem me;

	me.a.id = MARSHAL_SET_VIDEO_CAPTURE_DEVICE;
	me.a.fm = fm;

	me.convid = convid;
	me.devid = devid;

	marshal_send(&me);
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


void marshal_flowmgr_set_video_handlers(struct flowmgr *fm, 
					flowmgr_create_preview_h *create_preview_handler,
					flowmgr_release_preview_h *release_preview_handler,
					flowmgr_create_view_h *create_view_handler,
					flowmgr_release_view_h *release_view_handler,
					void *arg)
{
	struct marshal_video_handlers_elem me;

	me.a.id = MARSHAL_SET_VIDEO_HANDLERS;
	me.a.fm = fm;

	me.create_preview_handler = create_preview_handler;
	me.release_preview_handler = release_preview_handler;
	me.create_view_handler = create_view_handler;
	me.release_view_handler = release_view_handler;
	me.arg = arg;

	marshal_send(&me);
}

#endif

