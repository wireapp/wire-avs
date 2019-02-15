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



enum egcall_state {
	EGCALL_STATE_NONE = 0,
	EGCALL_STATE_IDLE,
	EGCALL_STATE_OUTGOING,
	EGCALL_STATE_INCOMING,
	EGCALL_STATE_ANSWERED,
	EGCALL_STATE_ACTIVE,
	EGCALL_STATE_TERMINATING,
};

#define EGCALL_REASON_NORMAL             0
#define EGCALL_REASON_ANSWERED_ELSEWHERE 5
#define EGCALL_REASON_STILL_ONGOING      7
#define EGCALL_REASON_REJECTED          10

struct egcall;

typedef void (egcall_start_h)(uint32_t msg_time, const char *userid_sender,
			      bool video, bool should_ring, void *arg);
typedef void (egcall_answer_h)(void *arg);
typedef void (egcall_leave_h)(int reason, uint32_t msg_time, void *arg);
typedef void (egcall_group_changed_h)(void *arg);

typedef void (egcall_close_h)(int err, uint32_t msg_time, void *arg);
typedef void (egcall_metrics_h)(const char *metrics_json, void *arg);

const char *egcall_state_name(enum egcall_state state);

int egcall_alloc(struct egcall **egcallp,
		 const struct ecall_conf *conf,		 
		 const char *convid,
		 const char *userid_self,
		 const char *clientid);

struct icall *egcall_get_icall(struct egcall* egcall);

int egcall_set_turnserver(struct icall *icall,
			  const struct sa *addr,
			  int proto, bool secure,
			  const char *username,
			  const char *credential);

int egcall_add_turnserver(struct icall *icall,
			  struct zapi_ice_server *srv);

int egcall_start(struct icall *icall,
		 enum icall_call_type call_type,
		 bool audio_cbr,
		 void *extcodec_arg);

int egcall_answer(struct icall *icall,
		  enum icall_call_type call_type,
		  bool audio_cbr,
		  void *extcodec_arg);

void egcall_end(struct icall *icall);

int egcall_msg_recv(struct icall *icall,
		     uint32_t curr_time,
		     uint32_t msg_time,
		     const char *userid_sender,
		     const char *clientid_sender,
		     struct econn_message *msg);

int egcall_media_start(struct icall *icall);
void egcall_media_stop(struct icall *icall);

int egcall_set_video_send_state(struct icall *icall, enum icall_vstate state);

struct wcall_members;
int egcall_get_members(struct icall *icall, struct wcall_members **mmp);

int egcall_set_quality_interval(struct icall *icall, uint64_t interval);

int egcall_debug(struct re_printf *pf, const struct icall *arg);

