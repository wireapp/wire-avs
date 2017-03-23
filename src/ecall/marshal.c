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

struct ecall_marshal {
	struct mqueue *mq;
};


enum mq_event {
	ECALL_MEV_TRANSP_RECV = 1,
	ECALL_MEV_MSG_RECV,
	ECALL_MEV_START,
	ECALL_MEV_ANSWER,
	ECALL_MEV_RESTART,
	ECALL_MEV_END,
	ECALL_MEV_VIDEO_SEND_ACTIVE,
	ECALL_MEV_MEDIA_START,
	ECALL_MEV_MEDIA_STOP,
};


struct mq_data {
	union {
		struct {
			struct ecall *ecall;
			uint64_t curr_time;
			uint64_t msg_time;
			char *convid;
			char *userid;
			char *clientid;
			char *msg;
		} transp_recv;

		struct {
			struct ecall *ecall;
			uint64_t curr_time;
			uint64_t msg_time;
			char *convid;
			char *userid;
			char *clientid;
			struct econn_message *msg;
		} msg_recv;

		struct {
			struct ecall *ecall;
		} start;

		struct {
			struct ecall *ecall;
		} answer;

		struct {
			struct ecall *ecall;
		} end;

		struct {
			struct ecall *ecall;
		} restart;		

		struct {
			struct ecall *ecall;
			bool active;
		} video_send_active;

		struct {
			struct ecall *ecall;
		} audio_send_cbr;
        
		struct {
			struct ecall *ecall;
		} media_start;
		struct {
			struct ecall *ecall;
		} media_stop;
	} u;
};


static void mqueue_handler(int id, void *data, void *arg)
{
	struct ecall_marshal *em = arg;
	struct mq_data *md = data;
	int err;

	(void)em;

	switch (id) {

	case ECALL_MEV_TRANSP_RECV:
		ecall_transp_recv(md->u.transp_recv.ecall,
				  md->u.transp_recv.curr_time,
				  md->u.transp_recv.msg_time,
				  /* md->u.transp_recv.convid, */
				  md->u.transp_recv.userid,
				  md->u.transp_recv.clientid,
				  md->u.transp_recv.msg);
		break;

	case ECALL_MEV_MSG_RECV:
		ecall_msg_recv(md->u.msg_recv.ecall,
			       md->u.msg_recv.curr_time,
			       md->u.msg_recv.msg_time,
			       md->u.msg_recv.userid,
			       md->u.msg_recv.clientid,
			       md->u.msg_recv.msg);
		break;
		

	case ECALL_MEV_START:
		err = ecall_start(md->u.start.ecall);
		if (err) {
			warning("ecall: ecall_start failed (%m)\n", err);
			ecall_end(md->u.start.ecall);
		}
		break;

	case ECALL_MEV_ANSWER:
		err = ecall_answer(md->u.answer.ecall);
		if (err) {
			warning("ecall: ecall_answer failed (%m)\n", err);
			ecall_end(md->u.answer.ecall);
		}
		break;

	case ECALL_MEV_RESTART:
		err = ecall_restart(md->u.restart.ecall);
		if (err) {
			warning("ecall: ecall_restart failed (%m)\n", err);
		}
		break;		

	case ECALL_MEV_END:
		ecall_end(md->u.end.ecall);
		break;

	case ECALL_MEV_VIDEO_SEND_ACTIVE:
		err = ecall_set_video_send_active(
					md->u.video_send_active.ecall,
					md->u.video_send_active.active);
		if (err) {
			warning("ecall: set_video_send_active failed (%m)\n",
				err);
		}
		break;
        
	case ECALL_MEV_MEDIA_START:
		err = ecall_media_start(md->u.media_start.ecall);
		if (err) {
			warning("ecall: ecall_media_start failed (%m)\n", err);
			ecall_end(md->u.answer.ecall);
		}
		break;

	case ECALL_MEV_MEDIA_STOP:
		ecall_media_stop(md->u.media_stop.ecall);
		break;

	default:
		warning("ecall: marshal: unknown event: %d\n", id);
		break;
	}

	mem_deref(md);
}


static void marshal_destructor(void *arg)
{
	struct ecall_marshal *em = arg;

	mem_deref(em->mq);
}


int ecall_marshal_alloc(struct ecall_marshal **emp)
{
	struct ecall_marshal *em;
	int err = 0;

	if (!emp)
		return EINVAL;

	em = mem_zalloc(sizeof(*em), marshal_destructor);
	if (!em)
		return ENOMEM;

	err = mqueue_alloc(&em->mq, mqueue_handler, em);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(em);
	else
		*emp = em;

	return err;
}


static void transp_recv_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.transp_recv.ecall);
	mem_deref(md->u.transp_recv.convid);
	mem_deref(md->u.transp_recv.userid);
	mem_deref(md->u.transp_recv.clientid);
	mem_deref(md->u.transp_recv.msg);
}


static void msg_recv_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.msg_recv.ecall);
	mem_deref(md->u.msg_recv.convid);
	mem_deref(md->u.msg_recv.userid);
	mem_deref(md->u.msg_recv.clientid);
	mem_deref(md->u.msg_recv.msg);
}



static void start_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.start.ecall);
}


static void media_start_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.media_start.ecall);
}


static void media_stop_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.media_stop.ecall);
}


static void answer_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.answer.ecall);
}


static void restart_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.answer.ecall);
}


static void end_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.end.ecall);
}


static void video_send_active_destructor(void *arg)
{
	struct mq_data *md = arg;

	mem_deref(md->u.video_send_active.ecall);
}

int marshal_ecall_transp_recv(struct ecall_marshal *em,
			      struct ecall *ecall,
			      uint64_t curr_time,
			      uint64_t msg_time,
			      const char *convid,
			      const char *userid,
			      const char *clientid,
			      const char *msg)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), transp_recv_destructor);

	md->u.transp_recv.ecall = mem_ref(ecall);
	md->u.transp_recv.curr_time = curr_time;
	md->u.transp_recv.msg_time = msg_time;
	err = str_dup(&md->u.transp_recv.convid, convid);
	err |= str_dup(&md->u.transp_recv.userid, userid);
	err |= str_dup(&md->u.transp_recv.clientid, clientid);
	err |= str_dup(&md->u.transp_recv.msg, msg);

	if (err)
		goto out;

	err = mqueue_push(em->mq, ECALL_MEV_TRANSP_RECV, md);

 out:
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_msg_recv(struct ecall_marshal *em,
			   struct ecall *ecall,
			   uint64_t curr_time,
			   uint64_t msg_time,
			   const char *convid,
			   const char *userid,
			   const char *clientid,
			   struct econn_message *msg)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), msg_recv_destructor);

	md->u.msg_recv.ecall = mem_ref(ecall);
	md->u.msg_recv.curr_time = curr_time;
	md->u.msg_recv.msg_time = msg_time;
	err = str_dup(&md->u.msg_recv.convid, convid);
	err |= str_dup(&md->u.msg_recv.userid, userid);
	err |= str_dup(&md->u.msg_recv.clientid, clientid);
	md->u.msg_recv.msg = msg;

	if (err)
		goto out;

	err = mqueue_push(em->mq, ECALL_MEV_MSG_RECV, md);

 out:
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_start(struct ecall_marshal *em,
			struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), start_destructor);
	if (!md)
		return ENOMEM;

	md->u.start.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_START, md);
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_answer(struct ecall_marshal *em,
			 struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), answer_destructor);
	if (!md)
		return ENOMEM;

	md->u.answer.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_ANSWER, md);
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_restart(struct ecall_marshal *em,
			  struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), restart_destructor);
	if (!md)
		return ENOMEM;

	md->u.restart.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_RESTART, md);
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_end(struct ecall_marshal *em,
		      struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), end_destructor);
	if (!md)
		return ENOMEM;

	md->u.end.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_END, md);
	if (err)
		mem_deref(md);

	return err;
}

int  marshal_ecall_set_video_send_active(struct ecall_marshal *em,
					 struct ecall *ecall,
					 bool active)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), video_send_active_destructor);
	if (!md)
		return ENOMEM;

	md->u.video_send_active.ecall = mem_ref(ecall);
	md->u.video_send_active.active = active;

	err = mqueue_push(em->mq, ECALL_MEV_VIDEO_SEND_ACTIVE, md);
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_media_start(struct ecall_marshal *em, struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), media_start_destructor);
	if (!md)
		return ENOMEM;

	md->u.media_start.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_MEDIA_START, md);
	if (err)
		mem_deref(md);

	return err;
}


int marshal_ecall_media_stop(struct ecall_marshal *em, struct ecall *ecall)
{
	struct mq_data *md;
	int err = 0;

	if (!em)
		return EINVAL;

	md = mem_zalloc(sizeof(*md), media_stop_destructor);
	if (!md)
		return ENOMEM;

	md->u.media_stop.ecall = mem_ref(ecall);

	err = mqueue_push(em->mq, ECALL_MEV_MEDIA_STOP, md);
	if (err)
		mem_deref(md);

	return err;
}

