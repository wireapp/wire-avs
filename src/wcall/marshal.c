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

struct wcall_marshal {
	struct mqueue *mq;
	struct list mdl;
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
	WCALL_MEV_DESTROY,
};


struct mq_data {
	enum mq_event event;
	void *wuser;
	struct wcall *wcall;
	struct le le; /* member of marshaling list */
	
	union {
		struct {
			int call_type;
			int conv_type;
			bool audio_cbr;
			void *extcodec_arg;
		} start;

		struct {
			int call_type;
			bool audio_cbr;
			void *extcodec_arg;
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
			char *convid;
			char *userid;
			char *clientid;
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
			char *convid;
			uint32_t msg_time;
			char *userid;
			int video_call;
			int should_ring;
		} incoming;
	} u;
};

static void md_destructor(void *arg)
{
	struct mq_data *md = arg;

	list_unlink(&md->le);
	
	switch (md->event) {
	case WCALL_MEV_RECV_MSG:
		mem_deref(md->u.recv_msg.msg);
		mem_deref(md->u.recv_msg.convid);
		mem_deref(md->u.recv_msg.userid);
		mem_deref(md->u.recv_msg.clientid);
		break;

	case WCALL_MEV_CONFIG_UPDATE:
		mem_deref(md->u.config_update.json_str);
		break;

	case WCALL_MEV_RESP:
		mem_deref(md->u.resp.reason);
		break;

	case WCALL_MEV_INCOMING:
		mem_deref(md->u.incoming.convid);
		mem_deref(md->u.incoming.userid);
		break;

	default:
		break;
	}

	md->wcall = mem_deref(md->wcall);	
}


static struct mq_data *md_new(void *wuser, struct wcall *wcall,
			     enum mq_event event)
{
	struct mq_data *md;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return NULL;

	md->wuser = wuser;
	md->wcall = mem_ref(wcall);
	md->event = event;

	return md;
}

static void mqueue_handler(int id, void *data, void *arg)
{
	struct mq_data *md = data;
	int err;

	(void)arg;

	switch (id) {

	case WCALL_MEV_RECV_MSG:
		wcall_i_recv_msg(md->wuser,
				 md->u.recv_msg.msg,
				 md->u.recv_msg.curr_time,
				 md->u.recv_msg.msg_time,
				 md->u.recv_msg.convid,
				 md->u.recv_msg.userid,
				 md->u.recv_msg.clientid);
		break;

	case WCALL_MEV_CONFIG_UPDATE:
		wcall_i_config_update(md->wuser,
				      md->u.config_update.err,
				      md->u.config_update.json_str);
		break;

	case WCALL_MEV_RESP:
		wcall_i_resp(md->wuser,
			     md->u.resp.status,
			     md->u.resp.reason,
			     md->u.resp.arg);
		break;

	case WCALL_MEV_START:
		 err = wcall_i_start(md->wcall,
				     md->u.start.call_type,
				     md->u.start.conv_type,
				     md->u.start.audio_cbr,
				     md->u.start.extcodec_arg);
		if (err) {
			warning("wcall: wcall_start failed (%m)\n", err);
			wcall_i_end(md->wcall);
		}
		break;

	case WCALL_MEV_ANSWER:
		err = wcall_i_answer(md->wcall,
				     md->u.answer.call_type,
				     md->u.answer.audio_cbr,
				     md->u.answer.extcodec_arg);
		if (err) {
			warning("wcall: wcall_answer failed (%m)\n", err);
			wcall_i_end(md->wcall);
		}
		break;

	case WCALL_MEV_REJECT:
		wcall_i_reject(md->wcall);
		break;

	case WCALL_MEV_END:
		wcall_i_end(md->wcall);
		break;

	case WCALL_MEV_VIDEO_SET_STATE:
		wcall_i_set_video_send_state(md->wcall,
					      md->u.video_set_state.state);
		break;

	case WCALL_MEV_MCAT_CHANGED:
		wcall_i_mcat_changed(md->wuser, md->u.mcat_changed.state);
		break;
            
	case WCALL_MEV_AUDIO_ROUTE_CHANGED:
		wcall_i_audio_route_changed(md->u.route_changed.new_route);
		break;

	case WCALL_MEV_NETWORK_CHANGED:
		wcall_i_network_changed();
		break;

	case WCALL_MEV_INCOMING:
		wcall_i_invoke_incoming_handler(md->u.incoming.convid,
				    md->u.incoming.msg_time,
				    md->u.incoming.userid,
				    md->u.incoming.video_call,
				    md->u.incoming.should_ring,
				    md->wuser);
		break;


	case WCALL_MEV_DESTROY:
		wcall_i_destroy(md->wuser);
		break;

	default:
		warning("wcall: marshal: unknown event: %d\n", id);
		break;
	}

	mem_deref(md);
}


static void wm_destructor(void *arg)
{
	struct wcall_marshal *wmarsh = arg;
	size_t n;
	
	wmarsh->mq = mem_deref(wmarsh->mq);
	
	n = list_count(&wmarsh->mdl);
	if (!list_isempty(&wmarsh->mdl)) {
		debug("wcall: marshal(%p): flush pending events: %u\n",
		      wmarsh, list_count(&wmarsh->mdl));
		list_flush(&wmarsh->mdl);
	}
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

 out:
	if (err)
		mem_deref(wmarsh);
	else
		*wmp = wmarsh;

	return err;
}


static int md_enqueue(struct mq_data *md)
{
	struct wcall_marshal *wm;
	int err;
	
	wm = wcall_get_marshal(md->wuser);
	if (wm == NULL) {
		err = ENOSYS;
		goto out;
	}

	list_append(&wm->mdl, &md->le, md);
	err = mqueue_push(wm->mq, md->event, md);
	if (err)
		goto out;

 out:
	return err;
	
	
}


AVS_EXPORT
void wcall_recv_msg(void *wuser, const uint8_t *buf, size_t len,
		    uint32_t curr_time,
		    uint32_t msg_time,
		    const char *convid,
		    const char *userid,
		    const char *clientid)
{
	struct econn_message *msg;
	struct mq_data *md = NULL;
	int err = 0;

	if (!buf || len == 0 || !convid || !userid || !clientid)
		return;

	err = econn_message_decode(&msg, curr_time, msg_time,
				   (const char *)buf, len);
	if (err) {
		warning("wcall: recv_msg: failed to decode\n");
		return;
	}

	md = md_new(wuser, NULL, WCALL_MEV_RECV_MSG);
	if (!md)
		return;

	md->u.recv_msg.msg = msg;
	md->u.recv_msg.curr_time = curr_time;
	md->u.recv_msg.msg_time = msg_time;
	err = str_dup(&md->u.recv_msg.convid, convid);
	err |= str_dup(&md->u.recv_msg.userid, userid);
	err |= str_dup(&md->u.recv_msg.clientid, clientid);

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
void wcall_config_update(void *wuser, int err, const char *json_str)
{
	struct mq_data *md = NULL;

	info("wcall(%p): config_update: err=%d json=%zu bytes\n",
	     wuser, err, str_len(json_str));

	md = md_new(wuser, NULL, WCALL_MEV_CONFIG_UPDATE); 
	if (!md)
		return;

	str_dup(&md->u.config_update.json_str, json_str);
	md->u.config_update.err = err;

	md_enqueue(md);
}


AVS_EXPORT
void wcall_resp(void *wuser, int status, const char *reason, void *arg)
{
	struct mq_data *md = NULL;
	int err = 0;

	if (!wuser) {
		warning("wcall_resp: no wcall user\n");
		return;
	}
	
	md = md_new(wuser, NULL, WCALL_MEV_RESP);
	if (!md) {
		err = ENOMEM;
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
int wcall_start_ex(void *wuser, const char *convid,
		   int call_type,
		   int conv_type,
		   int audio_cbr /*bool */,
		   void *extcodec_arg)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	bool added = false;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	wcall = wcall_lookup(wuser, convid);
	if (!wcall) {
		err = wcall_add(wuser, &wcall, convid, conv_type);
		if (err)
			goto out;
		added = true;
	}
	md = md_new(wuser, wcall, WCALL_MEV_START);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	md->u.start.call_type = call_type;
	md->u.start.conv_type = conv_type;
	md->u.start.audio_cbr = (bool)audio_cbr;
	md->u.start.extcodec_arg = extcodec_arg;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

 out:
	if (err) {
		if (added)
			mem_deref(wcall);
	}
	
	return err;
}


AVS_EXPORT
int wcall_start(void *wuser, const char *convid,
		int call_type,
		int conv_type,
		int audio_cbr /*bool*/)
{
	return wcall_start_ex(wuser, convid, call_type, conv_type, audio_cbr,
			      NULL);
}


AVS_EXPORT 
int wcall_answer_ex(void *wuser, const char *convid,
		    int call_type,
		    int audio_cbr/* bool */,
		    void *extcodec_arg)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	wcall = wcall_lookup(wuser, convid);
	if (!wcall)
		return EPROTO;

	md = md_new(wuser, wcall, WCALL_MEV_ANSWER);

	md->wuser = wuser;
	md->u.answer.call_type = call_type;
	md->u.answer.audio_cbr = audio_cbr;
	md->u.answer.extcodec_arg = extcodec_arg;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);

	return err;
}


AVS_EXPORT 
int wcall_answer(void *wuser, const char *convid,
		 int call_type, int audio_cbr /*bool*/)
{
	return wcall_answer_ex(wuser, convid, call_type, audio_cbr, NULL);
}


AVS_EXPORT
void wcall_end(void *wuser, const char *convid)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	wcall = wcall_lookup(wuser, convid);	
	if (!wcall)
		return;

	md = md_new(wuser, wcall, WCALL_MEV_END);
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
int wcall_reject(void *wuser, const char *convid)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	wcall = wcall_lookup(wuser, convid);	
	if (!wcall)
		return EPROTO;

	md = md_new(wuser, wcall, WCALL_MEV_REJECT);
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
void wcall_set_video_send_state(void *wuser, const char *convid, int state)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return;

	wcall = wcall_lookup(wuser, convid);
	if (!wcall)
		return;

	md = md_new(wuser, wcall, WCALL_MEV_VIDEO_SET_STATE);
	if (!md)
		return;

	md->u.video_set_state.state = state;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);	
}

void wcall_mcat_changed(void *wuser, enum mediamgr_state state)
{
	struct mq_data *md = NULL;
	int err = 0;
    
	md = md_new(wuser, NULL, WCALL_MEV_MCAT_CHANGED);
	if (!md)
		return;
    
	md->u.mcat_changed.state = state;

	info("wcall_mcat_changed: wuser=%p state=%d\n", wuser, (int)state);
	
	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}

void wcall_audio_route_changed(void *wuser, enum mediamgr_auplay new_route)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = md_new(wuser, NULL, WCALL_MEV_AUDIO_ROUTE_CHANGED);
	if (!md)
		return;

	md->u.route_changed.new_route = new_route;

	err = md_enqueue(md);
	if (err)
		mem_deref(md);
}


AVS_EXPORT
void wcall_network_changed(void *wuser)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = md_new(wuser, NULL, WCALL_MEV_NETWORK_CHANGED);
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
			           int video_call,
			           int should_ring,
			           void *wuser)
{
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid || !userid)
		return;

	md = md_new(wuser, NULL, WCALL_MEV_INCOMING);
	if (!md)
		return;

	md->u.incoming.msg_time = msg_time;
	md->u.incoming.video_call = video_call;
	md->u.incoming.should_ring = should_ring;
	err = str_dup(&md->u.incoming.convid, convid);
	err |= str_dup(&md->u.incoming.userid, userid);

	if (err)
		goto out;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(md);
}

void wcall_marshal_destroy(void *id)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = md_new(id, NULL, WCALL_MEV_DESTROY);
	if (!md)
		return;

	err = md_enqueue(md);
	if (err)
		goto out;

 out:
	return;
}
