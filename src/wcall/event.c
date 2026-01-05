/*
 * Wire
 * Copyright (C) 2025 Wire Swiss GmbH
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

#include <pthread.h>
#include <unistd.h>

#include <re/re.h>
#include <avs.h>

#include <avs_wcall.h>
#include "wcall.h"

#define EVENT_AGE_VALID 60 /* max event age in seconds */

enum call_event_state {
	CALL_EVENT_STATE_IDLE     = 0,
	CALL_EVENT_STATE_INCOMING = 1,
	CALL_EVENT_STATE_MISSED   = 2,
	CALL_EVENT_STATE_CLOSED   = 3
};

struct call_event_instance {
	WUSER_HANDLE wuser;
	char *userid;
	char *clientid;

	bool processing;

	wcall_incoming_h *incomingh;
	wcall_missed_h *missedh;
	wcall_close_h *closeh;

	void *arg;
	
	struct list eventl;
};

struct call_event {
	char *convid;
	char *userid;
	char *clientid;
	
	enum call_event_state state;
	uint32_t msg_time;
	int conv_type;
	int reason;

	struct le le;
};

static struct {
	uint32_t wuser_index;
	struct list instances;
} calling_event;


static struct call_event_instance *wuser2evinst(WUSER_HANDLE wuser)
{
	bool found = false;
	struct call_event_instance *inst = NULL;
	struct le *le;

	if ((wuser & WU_MAGIC) != WU_MAGIC)
		return NULL;
	
	//lock_write_get(calling.lock);
	for (le = calling_event.instances.head; le && !found; le = le->next) {
		inst = le->data;

		found = inst->wuser == wuser;
	}
	//lock_rel(calling.lock);

	return found ? inst : NULL;

}

static void event_destructor(void *arg)
{
	struct call_event *ev = arg;

	mem_deref(ev->convid);
	mem_deref(ev->userid);
	mem_deref(ev->clientid);

	list_unlink(&ev->le);
}

static void queue_event(struct call_event_instance *inst,
			const char *convid,
			const char *userid,
			const char *clientid,
			uint32_t msg_time,
			int conv_type,
			enum call_event_state state,
			int reason)
{
	struct call_event *ev;
	
	if (!inst)
		return;

	ev = mem_zalloc(sizeof(*ev), event_destructor);
	if (!ev)
		return;

	str_dup(&ev->convid, convid);
	str_dup(&ev->userid, userid);
	str_dup(&ev->clientid, clientid);	
	ev->state = state;
	ev->msg_time = msg_time;
	ev->conv_type = conv_type;
	ev->reason = reason;

	list_append(&inst->eventl, &ev->le, ev);
}

static bool queue_find(struct call_event_instance *inst,
		       const char *convid,
		       const char *userid,
		       const char *clientid,
		       enum call_event_state state)
{
	bool found = false;
	struct le *le;

	if (!inst)
		return false;

	for (le = inst->eventl.head; le != NULL && !found; le = le->next) {
		struct call_event *ev = le->data;

		found = streq(ev->convid, convid)
			&& streq(ev->userid, userid)
			&& streq(ev->clientid, clientid)
			&& (ev->state == state);

		if (!found)
			le = le->next;
	}

	return found;
}


static void inst_destructor(void *arg)
{
	struct call_event_instance *inst = arg;

	mem_deref(inst->userid);
	mem_deref(inst->clientid);
}


AVS_EXPORT
WUSER_HANDLE wcall_event_create(const char *userid,
				const char *clientid,
				wcall_incoming_h *incomingh,
				wcall_missed_h *missedh,
				wcall_close_h *closeh,
				void *arg)
{
	bool found = false;
	struct le *le = calling_event.instances.head;
	struct call_event_instance *inst = NULL;

	while(le && !found) {
		inst = le->data;

		found = streq(inst->userid, userid)
		     && streq(inst->clientid, clientid);
	}
	if (found)
		return inst ? inst->wuser : WUSER_INVALID_HANDLE;

	inst = mem_zalloc(sizeof(*inst), inst_destructor);
	if (!inst) {
		error("event: cannot create instance\n");
		return WUSER_INVALID_HANDLE;
	}
	inst->wuser = wcall_create_wuser(&calling_event.wuser_index);

	str_dup(&inst->userid, userid);
	str_dup(&inst->clientid, clientid);
	inst->incomingh = incomingh;
	inst->missedh = missedh;
	inst->closeh = closeh;
	inst->arg = arg;

	return inst->wuser;
}

void wcall_event_start(WUSER_HANDLE wuser)
{
	struct call_event_instance *inst;

	inst = wuser2evinst(wuser);
	if (!inst) {
		error("event(%p): start: cannot find wuser\n", wuser);
		return;
	}

	inst->processing = true;
}

int  wcall_event_process(WUSER_HANDLE wuser, 
			 const uint8_t *buf,
			 size_t len,
			 uint32_t curr_time, /* timestamp in seconds */
			 uint32_t msg_time,  /* timestamp in seconds */
			 const char *convid,
			 const char *userid,
			 const char *clientid,
			 int conv_type)
{
	struct call_event_instance *inst;
	struct econn_message *msg;
	int err = 0;

	if (!buf || len == 0 || !convid || !userid || !clientid)
		return EINVAL;
	
	inst = wuser2evinst(wuser);
	if (!inst) {
		error("event(%p): process: cannot find wuser\n", wuser);
		return ENOENT;
	}

	if (!inst->processing) {
		warning("event(%p): process: not processing events\n", inst);
		return EAGAIN;
	}

	err = econn_message_decode(&msg, curr_time, msg_time,
				   (const char *)buf, len);
	if (err == EPROTONOSUPPORT) {
		warning("wcall(%p): event_process: uknown message type, "
			"ask user to update client\n", inst);
		return WCALL_ERROR_UNKNOWN_PROTOCOL;
	}
	else if (err) {
		warning("wcall(%p): event_process: failed to decode\n", inst);
		return err;
	}

	switch(msg->msg_type) {
	case ECONN_SETUP:
	case ECONN_GROUP_START:
	case ECONN_CONF_START:
		if (msg->age > EVENT_AGE_VALID)
			return ETIMEDOUT;
		
		if (econn_message_isrequest(msg)) {
			queue_event(inst, convid,
				    userid, clientid,
				    msg->time,
				    conv_type,
				    CALL_EVENT_STATE_INCOMING,
				    WCALL_REASON_NORMAL);
		}
		else {
			queue_event(inst, convid,
				    userid, clientid,
				    msg->time,
				    conv_type,
				    CALL_EVENT_STATE_CLOSED,
				    WCALL_REASON_ANSWERED_ELSEWHERE);
		}
		break;

	case ECONN_CONF_END:
		if (econn_message_isrequest(msg)) {
			/* do we have a pending inconing? if we do,
			 * queue missed, otherwise queue closeh
			 */
			
			if (queue_find(inst, convid,
				       userid, clientid,
				       CALL_EVENT_STATE_INCOMING)) {
				queue_event(inst,
					    convid,
					    userid, clientid,
					    msg->time,
					    conv_type,
					    CALL_EVENT_STATE_MISSED,
					    WCALL_REASON_NORMAL);
			}
			else {
				queue_event(inst,
					    convid,
					    userid, clientid,
					    msg->time,
					    conv_type,
					    CALL_EVENT_STATE_CLOSED,
					    WCALL_REASON_NORMAL);
			}
		}
		break;

	case ECONN_REJECT:
		queue_event(inst,
			    convid,
			    userid, clientid,
			    msg->time,
			    conv_type,
			    CALL_EVENT_STATE_CLOSED,
			    WCALL_REASON_REJECTED);
		break;

	default:
		break;
		
	}

	return 0;
}

void wcall_event_end(WUSER_HANDLE wuser)
{
	struct call_event_instance *inst;
	struct le *le = NULL;

	inst = wuser2evinst(wuser);
	if (!inst) {
		error("event(%p): end: cannot find wuser\n", wuser);
		return;
	}

	if (!inst->processing) {
		return;		
	}

	le = inst->eventl.head;
	while(le) {
		struct call_event *ev = le->data;

		le = le->next;

		if (!ev)
			continue;
		
		switch (ev->state) {
		case CALL_EVENT_STATE_INCOMING:
			if (inst->incomingh) {
				inst->incomingh(ev->convid, ev->msg_time,
						ev->userid, ev->clientid,
						false, /* video-call */
						true,  /* should_ring */
						ev->conv_type,
						inst->arg);
			}
			break;

		case CALL_EVENT_STATE_MISSED:
			if (inst->missedh) {
				inst->missedh(ev->convid,
					      ev->msg_time,
					      ev->userid,
					      ev->clientid,
					      false, /* video-call */
					      inst->arg);
			}
			break;

		case CALL_EVENT_STATE_CLOSED:
			if (inst->closeh) {
				inst->closeh(ev->reason,
					     ev->convid,
					     ev->msg_time,
					     ev->userid,
					     ev->clientid,
					     inst->arg);
			}
			break;

		default:
			break;
		}

		mem_deref(ev);
	}

	inst->processing = false;
}
