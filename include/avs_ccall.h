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

struct ccall;

enum ccall_state {
	CCALL_STATE_NONE = 0,
	CCALL_STATE_IDLE,
	CCALL_STATE_INCOMING,
	CCALL_STATE_CONNSENT,
	CCALL_STATE_SETUPRECV,
	CCALL_STATE_CONNECTING,
	CCALL_STATE_CONNECTED,
	CCALL_STATE_ACTIVE,
	CCALL_STATE_TERMINATING,
	CCALL_STATE_WAITCONFIG,
	CCALL_STATE_WAITCONFIG_OUTGOING
};

int ccall_alloc(struct ccall **ccallp,
		const struct ecall_conf *conf,		 
		const char *convid,
		const char *userid_self,
		const char *clientid,
		bool is_mls_call);

struct icall *ccall_get_icall(struct ccall* ccall);

int ccall_set_config(struct ccall *ccall, struct config *cfg);

int  ccall_add_turnserver(struct icall *icall, struct zapi_ice_server *srv);

int  ccall_add_sft(struct icall *icall, const char *sft_url);

int  ccall_start(struct icall *icall,
		 enum icall_call_type call_type,
		 bool audio_cbr);

int  ccall_answer(struct icall *icall,
		  enum icall_call_type call_type,
		  bool audio_cbr);

void ccall_end(struct icall *icall);

void ccall_reject(struct icall *icall);

int  ccall_media_start(struct icall *icall);

void ccall_media_stop(struct icall *icall);

int  ccall_set_vstate(struct icall *icall, enum icall_vstate state);

int  ccall_get_members(struct icall *icall, struct wcall_members **mmp);

int  ccall_set_quality_interval(struct icall *icall, uint64_t interval);

int  ccall_update_mute_state(const struct icall* icall);

int  ccall_request_video_streams(struct icall *icall,
				 struct list *clientl,
				 enum icall_stream_mode mode);

int  ccall_msg_recv(struct icall* icall,
		    uint32_t curr_time,
		    uint32_t msg_time,
		    const char *userid_sender,
		    const char *clientid_sender,
		    struct econn_message *msg);

int  ccall_sft_msg_recv(struct icall* icall,
			int status,
		        struct econn_message *msg);

void ccall_set_clients(struct icall* icall,
		       struct list *clientl,
		       uint32_t epoch);

int  ccall_stats(struct re_printf *pf, const struct icall *icall);
int  ccall_stats_struct(const struct ccall *ccall,
		        struct iflow_stats *stats);

int  ccall_debug(struct re_printf *pf, const struct icall* icall);

int  ccall_activate(struct icall *icall, bool active);

const char *ccall_state_name(enum ccall_state state);

struct keystore *ccall_get_keystore(struct ccall *ccall);
