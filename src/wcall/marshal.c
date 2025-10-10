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

#include <avs.h>
#include "avs_wcall.h"
#include "wcall.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

struct wcall_marshal {
	struct mqueue *mq;
	struct list mdl;
	struct lock *lock;
};


enum mq_event {
	WCALL_MEV_START,
	WCALL_MEV_ANSWER,
	WCALL_MEV_REJECT,
	WCALL_MEV_END,
	WCALL_MEV_RESP,
	WCALL_MEV_RECV_MSG,
	WCALL_MEV_CONFIG_UPDATE,
	WCALL_MEV_VIDEO_STATE_HANDLER,
	WCALL_MEV_VIDEO_SET_STATE,
	WCALL_MEV_MCAT_CHANGED,
	WCALL_MEV_AUDIO_ROUTE_CHANGED,
	WCALL_MEV_NETWORK_CHANGED,
	WCALL_MEV_INCOMING,
	WCALL_MEV_SFT_RESP,
	WCALL_MEV_SET_CLIENTS,
	WCALL_MEV_DCE_SEND,
	WCALL_MEV_DESTROY,
	WCALL_MEV_SET_MUTE,
	WCALL_MEV_REQ_VSTREAMS,
	WCALL_MEV_SET_EPOCH_INFO,
	WCALL_MEV_PROCESS_NOTIFICATIONS,
	WCALL_MEV_SET_DURATION,
};


struct mq_data {
	enum mq_event event;
	struct calling_instance *inst;
	char *convid;
	struct le le; /* member of marshaling list */
	
	union {
		struct {
			int call_type;
			int conv_type;
			bool audio_cbr;
		        bool meeting;
		} start;

		struct {
			int call_type;
			bool audio_cbr;
		} answer;

		struct {
		} reject;
		
		struct {
		} end;

		struct {
			int state;
		} video_set_state;

		struct {
			enum mediamgr_state state;
		} mcat_changed;

		struct {
			enum mediamgr_auplay new_route;
		} route_changed;
                    
		struct {
			struct econn_message *msg;
			uint32_t curr_time; /* timestamp in seconds */
			uint32_t msg_time;  /* timestamp in seconds */
			char *userid;
			char *clientid;
			int conv_type;
		        bool meeting;
		} recv_msg;


		struct {
			int err;
			char *json_str;
		} config_update;

		struct {
			int status;
			char *reason;
			void *arg;
		} resp;

		struct {
			int err;
			struct econn_message *msg;
			void *ctx;
		} sft_resp;

		struct {
			uint32_t msg_time;
			char *userid;
			char *clientid;
			int video_call;
			int should_ring;
			int conv_type;
		} incoming;

		struct {
			char *json;
		} set_clients;

		struct {
			struct mbuf *mb;
		} dce_send;

		struct {
			int muted;
		} set_mute;

		struct {
			int mode;
			char *json;
		} req_vstreams;

		struct {
			uint32_t epochid;
			char *clients_json;
			uint32_t key_size;
			uint8_t *key_data;
		} set_epoch_info;

		struct {
			bool processing;
		} process_notifications;

	        struct {
		        int duration;
	        } set_duration;
	} u;
};

static void md_destructor(void *arg)
{
	struct wcall_marshal *wm = NULL;
	struct mq_data *md = arg;

	wm = wcall_get_marshal(md->inst);
	if (!wm)
		return;

	lock_write_get(wm->lock);
	list_unlink(&md->le);
	lock_rel(wm->lock);
	
	switch (md->event) {
	case WCALL_MEV_ANSWER:
		break;

	case WCALL_MEV_START:
		break;

	case WCALL_MEV_RECV_MSG:
		mem_deref(md->u.recv_msg.msg);
		mem_deref(md->u.recv_msg.userid);
		mem_deref(md->u.recv_msg.clientid);
		break;

	case WCALL_MEV_CONFIG_UPDATE:
		mem_deref(md->u.config_update.json_str);
		break;

	case WCALL_MEV_RESP:
		mem_deref(md->u.resp.reason);
		break;

	case WCALL_MEV_SFT_RESP:
		mem_deref(md->u.sft_resp.msg);
		break;

	case WCALL_MEV_INCOMING:
		mem_deref(md->u.incoming.userid);
		mem_deref(md->u.incoming.clientid);
		break;

	case WCALL_MEV_DCE_SEND:
		mem_deref(md->u.dce_send.mb);
		break;

	case WCALL_MEV_SET_CLIENTS:
		mem_deref(md->u.set_clients.json);
		break;

	case WCALL_MEV_REQ_VSTREAMS:
		mem_deref(md->u.req_vstreams.json);
		break;

	case WCALL_MEV_SET_EPOCH_INFO:
		mem_deref(md->u.set_epoch_info.clients_json);
		mem_deref(md->u.set_epoch_info.key_data);
		break;

	default:
		break;
	}

	md->convid = mem_deref(md->convid);
}


static struct mq_data *md_new(struct calling_instance *inst,
			      const char *convid,
			      enum mq_event event)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return NULL;

	if (convid) {
		err = str_dup(&md->convid, convid);
		if (err) {
			warning("marshall: unable to copy convid\n");
			goto out;
		}
	}

	md->inst = inst;
	md->event = event;

out:
	if (err)
		md = mem_deref(md);

	return md;
}


static void mqueue_handler(int id, void *data, void *arg)
{
	struct mq_data *md = data;
	struct wcall *wcall = NULL;
	int err = 0;

	(void)arg;

	if (md->convid)
		wcall = wcall_lookup(md->inst, md->convid);

	switch (id) {

	case WCALL_MEV_RECV_MSG:
		wcall_i_recv_msg(md->inst,
				 md->u.recv_msg.msg,
				 md->u.recv_msg.curr_time,
				 md->u.recv_msg.msg_time,
				 md->convid,
				 md->u.recv_msg.userid,
				 md->u.recv_msg.clientid,
				 md->u.recv_msg.conv_type,
				 md->u.recv_msg.meeting);
		break;

	case WCALL_MEV_CONFIG_UPDATE:
		wcall_i_config_update(md->inst,
				      md->u.config_update.err,
				      md->u.config_update.json_str);
		break;

	case WCALL_MEV_RESP:
		wcall_i_resp(md->inst,
			     md->u.resp.status,
			     md->u.resp.reason,
			     md->u.resp.arg);
		break;

	case WCALL_MEV_SFT_RESP:
		wcall_i_sft_resp(md->inst,
				 md->u.sft_resp.err,
				 md->u.sft_resp.msg,
				 md->u.sft_resp.ctx);
		break;

	case WCALL_MEV_START:
		if (!wcall) {
			if (!md->convid) {
				err = ENOENT;
				goto out;
			}

			err = wcall_add(md->inst,
					&wcall,
					md->convid,
					md->u.start.conv_type,
					md->u.start.meeting);
			if (err || !wcall)
				goto out;
		}
		{
		       struct duration_entry *dent;
		       dent = wcall_duration_lookup(md->inst, md->convid);
		       if (dent) {
			       wcall_i_set_duration(wcall, dent->duration);
			       mem_deref(dent);
		       }
		}
		err = wcall_i_start(wcall,
				    md->u.start.call_type,
				    md->u.start.conv_type,
				    md->u.start.audio_cbr,
				    md->u.start.meeting);
		if (err) {
			warning("wcall: wcall_start failed (%m)\n", err);
			wcall_i_end(wcall);
		}
		break;

	case WCALL_MEV_ANSWER:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		err = wcall_i_answer(wcall,
				     md->u.answer.call_type,
				     md->u.answer.audio_cbr);
		if (err) {
			warning("wcall: wcall_answer failed (%m)\n", err);
			wcall_i_end(wcall);
		}
		break;

	case WCALL_MEV_REJECT:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_reject(wcall);
		break;

	case WCALL_MEV_END:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_end(wcall);
		break;

	case WCALL_MEV_VIDEO_SET_STATE:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_set_video_send_state(wcall,
					     md->u.video_set_state.state);
		break;

	case WCALL_MEV_MCAT_CHANGED:
		wcall_i_mcat_changed(md->inst, md->u.mcat_changed.state);
		break;
            
	case WCALL_MEV_AUDIO_ROUTE_CHANGED:
		wcall_i_audio_route_changed(md->u.route_changed.new_route);
		break;

	case WCALL_MEV_NETWORK_CHANGED:
		wcall_i_network_changed();
		break;

	case WCALL_MEV_INCOMING:
		wcall_i_invoke_incoming_handler(md->convid,
						md->u.incoming.msg_time,
						md->u.incoming.userid,
						md->u.incoming.clientid,
						md->u.incoming.video_call,
						md->u.incoming.should_ring,
						md->u.incoming.conv_type,
						md->inst);
		break;

	case WCALL_MEV_DCE_SEND:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_dce_send(wcall, md->u.dce_send.mb);
		break;

	case WCALL_MEV_SET_CLIENTS:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_set_clients_for_conv(wcall,
					     md->u.set_clients.json);
		break;

	case WCALL_MEV_DESTROY:
		wcall_i_destroy(md->inst);
		break;

	case WCALL_MEV_SET_MUTE:
		wcall_i_set_mute(md->u.set_mute.muted);
		break;

	case WCALL_MEV_SET_DURATION:
	        if (wcall) {
	    	        wcall_i_set_duration(wcall, md->u.set_duration.duration);
	        }
		else {
		        wcall_duration_add(md->inst, md->convid, md->u.set_duration.duration);
		}	  
		break;

	case WCALL_MEV_REQ_VSTREAMS:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_request_video_streams(wcall,
					      md->u.req_vstreams.mode,
					      md->u.req_vstreams.json);
		break;

	case WCALL_MEV_SET_EPOCH_INFO:
		if (!wcall) {
			err = ENOENT;
			goto out;
		}
		wcall_i_set_epoch_info(wcall,
				       md->u.set_epoch_info.epochid,
				       md->u.set_epoch_info.clients_json,
				       md->u.set_epoch_info.key_data,
				       md->u.set_epoch_info.key_size);
		break;

	case WCALL_MEV_PROCESS_NOTIFICATIONS:
		wcall_i_process_notifications(md->inst,
					      md->u.process_notifications.processing);
		break;

	default:
		warning("wcall: marshal: unknown event: %d\n", id);
		break;
	}

out:
	if (err)
		warning("wcall: mqueue_handler error (%m)\n", err);

	mem_deref(md);
}


static void wm_destructor(void *arg)
{
	struct wcall_marshal *wmarsh = arg;
	uint32_t count = 0;

	wmarsh->mq = mem_deref(wmarsh->mq);

	lock_write_get(wmarsh->lock);
	count = list_count(&wmarsh->mdl);
	lock_rel(wmarsh->lock);

	info("wcall: marshal(%p): flush pending events: %u\n",
	      wmarsh, count);
	list_flush(&wmarsh->mdl);

	wmarsh->lock = mem_deref(wmarsh->lock);
}


int wcall_marshal_alloc(struct wcall_marshal **wmp)
{
	struct wcall_marshal *wmarsh;
	int err;

	wmarsh = mem_zalloc(sizeof(*wmarsh), wm_destructor);
	if (!wmarsh)
		return ENOMEM;

	err = mqueue_alloc(&wmarsh->mq, mqueue_handler, NULL);
	if (err)
		goto out;

	list_init(&wmarsh->mdl);

	err = lock_alloc(&wmarsh->lock);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(wmarsh);
	else
		*wmp = wmarsh;

	return err;
}


static int md_enqueue(struct mq_data *md)
{
	struct wcall_marshal *wm = NULL;
	int err = 0;

	wm = wcall_get_marshal(md->inst);
	if (wm == NULL) {
		err = ENOSYS;
		goto out;
	}

	lock_write_get(wm->lock);
	list_append(&wm->mdl, &md->le, md);
	lock_rel(wm->lock);

	if (WCALL_MODE_MARSHAL == wcall_get_mode()) {
		err = mqueue_push(wm->mq, md->event, md);
		if (err)
			goto out;
	}
	else {
		mqueue_handler(md->event, md, NULL);
	}

 out:
	return err;
	
	
}


AVS_EXPORT
int  wcall_recv_msg(WUSER_HANDLE wuser, const uint8_t *buf, size_t len,
		    uint32_t curr_time,
		    uint32_t msg_time,
		    const char *convid,
		    const char *userid,
		    const char *clientid,
		    int conv_type,
		    int meeting)
{
	struct calling_instance *inst;
	struct econn_message *msg = NULL;
	struct mq_data *md = NULL;
	int err = 0;

	if (!buf || len == 0 || !convid || !userid || !clientid)
		return EINVAL;

	err = econn_message_decode(&msg, curr_time, msg_time,
				   (const char *)buf, len);
	if (err == EPROTONOSUPPORT) {
		warning("wcall: recv_msg: uknown message type, ask user to update client\n");
		return WCALL_ERROR_UNKNOWN_PROTOCOL;
	}
	else if (err) {
		warning("wcall: recv_msg: failed to decode\n");
		return err;
	}

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: recv_msg: invalid wuser: 0x%08X\n", wuser);
		return EINVAL;
	}		

	md = md_new(inst, convid, WCALL_MEV_RECV_MSG);
	if (!md) {
		mem_deref(msg);
		return ENOMEM;
	}

	md->u.recv_msg.msg = msg;
	md->u.recv_msg.curr_time = curr_time;
	md->u.recv_msg.msg_time = msg_time;
	md->u.recv_msg.conv_type = conv_type;
	md->u.recv_msg.meeting = meeting == 1;
	err = str_dup(&md->u.recv_msg.userid, userid);
	err |= str_dup(&md->u.recv_msg.clientid, clientid);

	if (err)
		goto out;

	err = md_enqueue(md);
	if (err)
		goto out;
			    

 out:
	if (err)
		mem_deref(md);

	return err;
}

AVS_EXPORT
void wcall_config_update(WUSER_HANDLE wuser, int err, const char *json_str)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;

	info("wcall(%p): config_update: err=%d json=%zu bytes\n",
	     wuser, err, str_len(json_str));

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: config_update: invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	
	md = md_new(inst, NULL, WCALL_MEV_CONFIG_UPDATE); 
	if (!md)
		return;

	str_dup(&md->u.config_update.json_str, json_str);
	md->u.config_update.err = err;

	md_enqueue(md);
}


AVS_EXPORT
void wcall_resp(WUSER_HANDLE wuser, int status, const char *reason, void *arg)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: resp: invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	
	md = md_new(inst, NULL, WCALL_MEV_RESP);
	if (!md) {
		warning("wcall: resp: failed to alloc md\n");
		return;
	}

	md->u.resp.arg = arg;
	md->u.resp.status = status;
	err = str_dup(&md->u.resp.reason, reason);
	if (err)
		md->u.resp.reason = NULL;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}


AVS_EXPORT
void wcall_sft_resp(WUSER_HANDLE wuser,
		    int perr, const uint8_t *buf, size_t len, void *ctx)
{
	struct calling_instance *inst;
	struct econn_message *msg = NULL;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: sft_resp: invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	
	if (!ctx)
		return;

	if (buf && len > 0) {
		err = econn_message_decode(&msg, 0, 0, (const char *)buf, len);
		if (err) {
			warning("wcall: recv_msg: failed to decode\n");
			return;
		}
	}

	md = md_new(inst, NULL, WCALL_MEV_SFT_RESP);
	if (!md)
		return;

	md->u.sft_resp.err = perr;
	md->u.sft_resp.msg = msg;
	md->u.sft_resp.ctx = ctx;

	if (err)
		goto out;

	err = md_enqueue(md);
	if (err)
		goto out;
 out:
	if (err)
		mem_deref(md);
}


AVS_EXPORT
int wcall_start(WUSER_HANDLE wuser,
		const char *convid,
		int call_type,
		int conv_type,
		int audio_cbr /*bool */,
		int meeting /*bool */)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return EINVAL;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: start_ex: invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_START);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	md->u.start.call_type = call_type;
	md->u.start.conv_type = conv_type;
	md->u.start.audio_cbr = (bool)audio_cbr;
	md->u.start.meeting = meeting == 1;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

 out:
	return err;
}


AVS_EXPORT 
int wcall_answer(WUSER_HANDLE wuser,
		 const char *convid,
		 int call_type,
		 int audio_cbr/* bool */)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: answer_ex: invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_ANSWER);
	md->u.answer.call_type = call_type;
	md->u.answer.audio_cbr = audio_cbr;

	err = md_enqueue(md);
	if (err) {
		goto out;
	}


out:
	if (err)
		mem_deref(md);

	return err;
}


AVS_EXPORT
void wcall_end(WUSER_HANDLE wuser, const char *convid)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid) {
		err = EINVAL;
		goto out;
	}
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: end: invalid handle: 0x%08X\n",
			wuser);
		return;
	}

	md = md_new(inst, convid, WCALL_MEV_END);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

 out:
	if (err)
		warning("wcall: end failed err=%m\n", err);
}


AVS_EXPORT
int wcall_reject(WUSER_HANDLE wuser, const char *convid)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: reject: invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_REJECT);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

 out:
	if (err)
		warning("wcall: end failed err=%m\n", err);

	return err;
}


AVS_EXPORT
void wcall_set_video_send_state(WUSER_HANDLE wuser,
				const char *convid, int state)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;
	
	if (!convid)
		return;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_video_send_state: "
			"invalid handle: 0x%08X\n",
			wuser);
		return;
	}

	md = md_new(inst, convid, WCALL_MEV_VIDEO_SET_STATE);
	if (!md)
		return;

	md->u.video_set_state.state = state;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);	
}


AVS_EXPORT
void wcall_dce_send(WUSER_HANDLE wuser, const char *convid, struct mbuf *mb)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: dce_send invalid handle: 0x%08X\n",
			wuser);
		return;
	}

	md = md_new(inst, convid, WCALL_MEV_DCE_SEND);
	if (!md)
		return;

	md->u.dce_send.mb = mem_ref(mb);

	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}

void wcall_mcat_changed(struct calling_instance *inst,
			enum mediamgr_state state)
{
	
	struct mq_data *md = NULL;
	int err = 0;

	if (!inst)
		return;
	
	md = md_new(inst, NULL, WCALL_MEV_MCAT_CHANGED);
	if (!md)
		return;
    
	md->u.mcat_changed.state = state;

	info("wcall_mcat_changed: inst=%p state=%d\n", inst, (int)state);
	
	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}

void wcall_audio_route_changed(	struct calling_instance *inst,
			       enum mediamgr_auplay new_route)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = md_new(inst, NULL, WCALL_MEV_AUDIO_ROUTE_CHANGED);
	if (!md)
		return;

	md->u.route_changed.new_route = new_route;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}


AVS_EXPORT
void wcall_network_changed(WUSER_HANDLE wuser)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: network_changed: invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	
	md = md_new(inst, NULL, WCALL_MEV_NETWORK_CHANGED);
	if (!md)
		return;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);	
}


AVS_EXPORT
void wcall_invoke_incoming_handler(const char *convid,
			           uint32_t msg_time,
			           const char *userid,
			           const char *clientid,
			           int video_call,
			           int should_ring,
				   int conv_type,
			           void *arg)
{
	struct calling_instance *inst = arg;
	struct mq_data *md = NULL;
	int err = 0;

	if (!inst || !convid || !userid)
		return;

	md = md_new(inst, convid, WCALL_MEV_INCOMING);
	if (!md)
		return;

	md->u.incoming.msg_time = msg_time;
	md->u.incoming.video_call = video_call;
	md->u.incoming.should_ring = should_ring;
	md->u.incoming.conv_type = conv_type;
	err = str_dup(&md->u.incoming.userid, userid);
	err |= str_dup(&md->u.incoming.clientid, clientid);

	if (err)
		goto out;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(md);
}


AVS_EXPORT
int wcall_set_clients_for_conv(WUSER_HANDLE wuser,
			       const char *convid,
			       const char *json)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid) {
		warning("wcall_set_clients_for_conv: no convid set\n");
		return EINVAL;
	}
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_clients_for_conv: "
			"invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}


	if (!json) {
		warning("wcall_set_clients_for_conv: no clients for conv\n");
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_SET_CLIENTS);
	if (!md)
		return ENOMEM;

	err = str_dup(&md->u.set_clients.json, json);
	if (err)
		goto out;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(md);

	return err;
}


AVS_EXPORT
void wcall_set_mute(WUSER_HANDLE wuser, int muted)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_mute: "
			"invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	
	md = md_new(inst, NULL, WCALL_MEV_SET_MUTE);
	if (!md)
		return;
    
	md->u.set_mute.muted = muted;

	info("wcall_set_mute: inst=%p muted=%d\n", inst, muted);
	
	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}


AVS_EXPORT
int wcall_request_video_streams(WUSER_HANDLE wuser,
				const char *convid,
				int mode,
				const char *json)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid) {
		warning("wcall: request_video_streams: no convid set\n");
		return EINVAL;
	}
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: request_video_streams: "
			"invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}

	if (!json) {
		warning("wcall: request_video_streams: no json\n");
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_REQ_VSTREAMS);
	if (!md)
		return ENOMEM;

	err = str_dup(&md->u.req_vstreams.json, json);
	if (err)
		goto out;

	md->u.req_vstreams.mode = mode;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(md);

	return err;
}


AVS_EXPORT
int wcall_set_epoch_info(WUSER_HANDLE wuser,
			 const char *convid,
			 uint32_t epochid,
			 const char *clients_json,
			 const char *key_base64)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	size_t key_size = 0;
	size_t b64_size = 0;
	int err = 0;

	if (!convid) {
		warning("wcall: set_epoch_info: no convid set\n");
		return EINVAL;
	}
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_epoch_info: "
			"invalid handle: 0x%08X\n",
			wuser);
		return EINVAL;
	}

	if (!key_base64) {
		warning("wcall: set_epoch_info: no key set\n");
		return EINVAL;
	}

	b64_size = str_len(key_base64);
	if (b64_size < 1 || b64_size > 1368) {
		warning("wcall: set_epoch_info: encoded key size invalid\n");
		return EINVAL;
	}

	md = md_new(inst, convid, WCALL_MEV_SET_EPOCH_INFO);
	if (!md)
		return ENOMEM;

	key_size = b64_size;
	md->u.set_epoch_info.key_data = mem_zalloc(key_size, NULL);
	if (!md->u.set_epoch_info.key_data) {
		err = ENOMEM;
		goto out;
	}

	err = base64_decode(key_base64,
			    b64_size,
			    md->u.set_epoch_info.key_data,
			    &key_size);
	
	if (err) {
		warning("wcall: set_epoch_info: error b64 decoding key (%m)\n", err);
		err = EINVAL;
		goto out;
	}

	if (key_size < 1 || key_size > 1024) {
		warning("wcall: set_epoch_info: decoded key size %zu invalid\n", key_size);
		err = EINVAL;
		goto out;
	}

	err = str_dup(&md->u.set_epoch_info.clients_json, clients_json);
	if (err) 
		goto out;

	md->u.set_epoch_info.epochid = epochid;
	md->u.set_epoch_info.key_size = key_size;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(md);

	return err;
}

AVS_EXPORT
void wcall_set_duration(WUSER_HANDLE wuser, const char *convid, int duration)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_duration: "
			"invalid handle: 0x%08X\n",
			wuser);
		return;
	}
	if (!convid) {
		warning("wcall: set_duration: no convid set\n");
		return;
	}

	md = md_new(inst, convid, WCALL_MEV_SET_DURATION);
	if (!md)
		return;

	md->u.set_duration.duration = duration;

	info("wcall_set_duration: inst=%p duration=%d\n", inst, duration);

	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}



AVS_EXPORT
int wcall_process_notifications(WUSER_HANDLE wuser, int processing)
{
	struct calling_instance *inst;
	struct mq_data *md = NULL;
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: process_notifications: invalid wuser: 0x%08X\n", wuser);
		return EINVAL;
	}

	md = md_new(inst, NULL, WCALL_MEV_PROCESS_NOTIFICATIONS);
	if (!md)
		return ENOMEM;

	md->u.process_notifications.processing = (bool)processing;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

	return err;
}


void wcall_marshal_destroy(struct calling_instance *inst)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = md_new(inst, NULL, WCALL_MEV_DESTROY);
	if (!md)
		return;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	return;
}
