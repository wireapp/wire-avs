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

struct {
	struct mqueue *mq;
} wcall_marshal;


enum mq_event {
	WCALL_MEV_START,
	WCALL_MEV_ANSWER,
	WCALL_MEV_REJECT,
	WCALL_MEV_END,
	WCALL_MEV_RESP,
	WCALL_MEV_RECV_MSG,
	WCALL_MEV_VIDEO_STATE_HANDLER,
	WCALL_MEV_VIDEO_SET_ACTIVE,
	WCALL_MEV_MCAT_CHANGED,
	WCALL_MEV_NETWORK_CHANGED,
};


struct mq_data {
	struct wcall *wcall;
	
	union {
		struct {
			bool has_video;
			bool group;
		} start;

		struct {
			bool group;
		} answer;

		struct {
			bool group;
		} reject;
		
		struct {
			bool group;
		} end;

		struct {
			bool active;
		} video_set_active;

		struct {
			enum mediamgr_state state;
		} mcat_changed;
		
		struct {
			struct econn_message *msg;
			uint32_t curr_time; /* timestamp in seconds */
			uint32_t msg_time;  /* timestamp in seconds */
			char *convid;
			char *userid;
			char *clientid;
		} recv_msg;

		struct {
			int status;
			char *reason;
			void *arg;
		} resp;
	} u;
};


static void mqueue_handler(int id, void *data, void *arg)
{
	struct mq_data *md = data;
	int err;

	(void)arg;

	switch (id) {

	case WCALL_MEV_RECV_MSG:
		wcall_i_recv_msg(md->u.recv_msg.msg,
				 md->u.recv_msg.curr_time,
				 md->u.recv_msg.msg_time,
				 md->u.recv_msg.convid,
				 md->u.recv_msg.userid,
				 md->u.recv_msg.clientid);
		break;

	case WCALL_MEV_RESP:
		wcall_i_resp(md->u.resp.status,
			     md->u.resp.reason,
			     md->u.resp.arg);
		break;

	case WCALL_MEV_START:
		 err = wcall_i_start(md->wcall,
				     md->u.start.has_video,
				     md->u.start.group);
		if (err) {
			warning("wcall: wcall_start failed (%m)\n", err);
			wcall_i_end(md->wcall, md->u.start.group);
		}
		break;

	case WCALL_MEV_ANSWER:
		err = wcall_i_answer(md->wcall, md->u.answer.group);
		if (err) {
			warning("wcall: wcall_answer failed (%m)\n", err);
			wcall_i_end(md->wcall, md->u.answer.group);
		}
		break;

	case WCALL_MEV_REJECT:
		wcall_i_reject(md->wcall, md->u.reject.group);
		break;

	case WCALL_MEV_END:
		wcall_i_end(md->wcall, md->u.end.group);
		break;

	case WCALL_MEV_VIDEO_SET_ACTIVE:
		wcall_i_set_video_send_active(md->wcall,
					      md->u.video_set_active.active);
		break;

	case WCALL_MEV_MCAT_CHANGED:
		wcall_i_mcat_changed(md->u.mcat_changed.state);
		break;

	case WCALL_MEV_NETWORK_CHANGED:
		wcall_i_network_changed();
		break;

	default:
		warning("wcall: marshal: unknown event: %d\n", id);
		break;
	}

	mem_deref(md);
}


static void recv_msg_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.recv_msg.msg);
	mem_deref(md->u.recv_msg.convid);
	mem_deref(md->u.recv_msg.userid);
	mem_deref(md->u.recv_msg.clientid);
}


static void resp_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.resp.reason);
}


static void md_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->wcall);
}


int wcall_marshal_init(void)
{
	int err;
	
	err = mqueue_alloc(&wcall_marshal.mq, mqueue_handler, NULL);
	if (err)
		goto out;

 out:
	return err;
}


void wcall_marshal_close(void)
{
	wcall_marshal.mq = mem_deref(wcall_marshal.mq);
}


AVS_EXPORT
void wcall_recv_msg(const uint8_t *buf, size_t len,
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
	
	md = mem_zalloc(sizeof(*md), recv_msg_destructor);
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

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_RECV_MSG, md);

 out:
	if (err)
		mem_deref(md);
}


AVS_EXPORT
void wcall_resp(int status, const char *reason, void *arg)
{
	struct mq_data *md = NULL;
	int err = 0;

	if (!wcall_marshal.mq)
		return;

	md = mem_zalloc(sizeof(*md), resp_destructor);
	if (!md) {
		err = ENOMEM;
		return;
	}

	md->u.resp.arg = arg;
	md->u.resp.status = status;
	err = str_dup(&md->u.resp.reason, reason);
	if (err)
		md->u.resp.reason = NULL;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_RESP, md);
	if (err)
		mem_deref(md);
}


AVS_EXPORT
int wcall_start(const char *convid,
		int is_video_call /* bool */,
		int group /* bool */)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	bool added = false;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	if (!wcall_marshal.mq)
		return ENOSYS;

	wcall = wcall_lookup(convid);
	if (!wcall) {
		err = wcall_add(&wcall, convid, (bool)group);
		if (err)
			goto out;
		added = true;
	}
	
	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	md->wcall = mem_ref(wcall);
	md->u.start.has_video = (bool)is_video_call;
	md->u.start.group = (bool)group;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_START, md);
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
int wcall_answer(const char *convid, int group /* bool */)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return EINVAL;
	
	if (!wcall_marshal.mq)
		return ENOSYS;

	wcall = wcall_lookup(convid);
	if (!wcall)
		return EPROTO;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return ENOMEM;

	md->wcall = mem_ref(wcall);
	md->u.answer.group = (bool)group;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_ANSWER, md);
	if (err)
		mem_deref(md);

	return err;
}


AVS_EXPORT
void wcall_end(const char *convid, int group /* bool */) 
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	wcall = wcall_lookup(convid);	
	if (!wcall)
		return;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	md->wcall = mem_ref(wcall);
	md->u.end.group = (bool)group;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_END, md);
	if (err)
		mem_deref(md);

 out:
	if (err)
		warning("wcall: end failed err=%m\n", err);
}


AVS_EXPORT
int wcall_reject(const char *convid, int group /* bool */)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	wcall = wcall_lookup(convid);	
	if (!wcall)
		return EPROTO;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md) {
		err = ENOMEM;
		goto out;
	}

	md->wcall = mem_ref(wcall);
	md->u.reject.group = (bool)group;
	
	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_REJECT, md);
	if (err)
		mem_deref(md);

 out:
	if (err)
		warning("wcall: end failed err=%m\n", err);

	return err;
}


AVS_EXPORT
void wcall_set_video_send_active(const char *convid, int active /*bool*/)
{
	struct wcall *wcall;
	struct mq_data *md = NULL;
	int err = 0;

	if (!convid)
		return;

	wcall = wcall_lookup(convid);
	if (!wcall)
		return;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return;

	md->wcall = mem_ref(wcall);
	md->u.video_set_active.active = active;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_VIDEO_SET_ACTIVE, md);
	if (err)
		mem_deref(md);	
}


void wcall_mcat_changed(enum mediamgr_state state)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return;
	
	md->u.mcat_changed.state = state;

	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_MCAT_CHANGED, md);
	if (err)
		mem_deref(md);	
}


AVS_EXPORT
void wcall_network_changed(void)
{
	struct mq_data *md = NULL;
	int err = 0;

	md = mem_zalloc(sizeof(*md), md_destructor);
	if (!md)
		return;
	
	err = mqueue_push(wcall_marshal.mq, WCALL_MEV_NETWORK_CHANGED, md);
	if (err)
		mem_deref(md);	
}


