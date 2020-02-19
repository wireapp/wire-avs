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

#define ICALL_REASON_NORMAL             0
#define ICALL_REASON_ERROR              1
#define ICALL_REASON_TIMEOUT            2
#define ICALL_REASON_LOST_MEDIA         3
#define ICALL_REASON_CANCELED           4
#define ICALL_REASON_ANSWERED_ELSEWHERE 5
#define ICALL_REASON_IO_ERROR           6
#define ICALL_REASON_STILL_ONGOING      7
#define ICALL_REASON_TIMEOUT_ECONN      8
#define ICALL_REASON_DATACHANNEL        9
#define ICALL_REASON_REJECTED          10

struct icall;
struct econn_message;
struct wcall_members;
struct econn_message;


enum icall_call_type {
	ICALL_CALL_TYPE_NORMAL       = 0,
	ICALL_CALL_TYPE_VIDEO        = 1,
	ICALL_CALL_TYPE_FORCED_AUDIO = 2
};

enum icall_conv_type {
	ICALL_CONV_TYPE_ONEONONE   = 0,
	ICALL_CONV_TYPE_GROUP      = 1,
	ICALL_CONV_TYPE_CONFERENCE = 2
};

enum icall_vstate {
	ICALL_VIDEO_STATE_STOPPED     = 0,
	ICALL_VIDEO_STATE_STARTED     = 1,
	ICALL_VIDEO_STATE_BAD_CONN    = 2,
	ICALL_VIDEO_STATE_PAUSED      = 3,
	ICALL_VIDEO_STATE_SCREENSHARE = 4,
};

/* Calls into icall */
typedef int  (icall_add_turnserver)(struct icall *icall,
				    struct zapi_ice_server *srv);

typedef int  (icall_set_sft)(struct icall *icall,
			     const char *sft_url);
typedef int  (icall_start)(struct icall *icall,
			   enum icall_call_type call_type, bool audio_cbr);
typedef int  (icall_answer)(struct icall *icall,
			    enum icall_call_type call_type, bool audio_cbr);
typedef void (icall_end)(struct icall *icall);
typedef int  (icall_media_start)(struct icall *icall);
typedef void (icall_media_stop)(struct icall *icall);
typedef int  (icall_set_media_laddr)(struct icall *icall, struct sa *laddr);
typedef int  (icall_set_vstate)(struct icall *icall, enum icall_vstate state);
typedef int  (icall_get_members)(struct icall *icall,
				 struct wcall_members **mmp);
typedef int  (icall_msg_recv)(struct icall* icall,
			      uint32_t curr_time,
			      uint32_t msg_time,
			      const char *userid_sender,
			      const char *clientid_sender,
			      struct econn_message *msg);
typedef int  (icall_sft_msg_recv)(struct icall* icall,
				  int status,
			          struct econn_message *msg);
typedef int  (icall_set_quality_interval)(struct icall *icall,
					  uint64_t interval);
typedef int  (icall_dce_send)(struct icall *icall, struct mbuf *mb);
typedef void (icall_set_clients)(const struct icall* icall, struct list *clientl);
typedef int  (icall_debug)(struct re_printf *pf, const struct icall* icall);
typedef int  (icall_stats)(struct re_printf *pf, const struct icall* icall);

/* Callbacks from icall */
typedef int  (icall_sft_h)(struct icall *icall,
			   const char *url,
			   struct econn_message *msg,
			   void *arg);
typedef int  (icall_send_h)(struct icall *icall,
			    const char *userid_sender,
			    struct econn_message *msg,
			    void *arg);
typedef void (icall_start_h)(struct icall *icall,
			     uint32_t msg_time,
			     const char *userid_sender,
			     const char *clientid_sender,
			     bool video,
			     bool should_ring,
			     enum icall_conv_type call_type,
			     void *arg);
typedef void (icall_answer_h)(struct icall *icall,
			      void *arg);
typedef void (icall_media_estab_h)(struct icall *icall,
				   const char *userid,
				   const char *clientid,
				   bool update,
				   void *arg);
typedef void (icall_audio_estab_h)(struct icall *icall,
				   const char *userid,
				   const char *clientid,
				   bool update,
				   void *arg);
typedef void (icall_datachan_estab_h)(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      bool update,
				      void *arg);
typedef void (icall_media_stopped_h)(struct icall *icall, void *arg);
typedef void (icall_leave_h)(struct icall *icall,
			     int reason,
			     uint32_t msg_time,
			     void *arg);
typedef void (icall_group_changed_h)(struct icall *icall,
				     void *arg);
typedef void (icall_close_h)(struct icall *icall,
			     int err,
			     const char *metrics_json,
			     uint32_t msg_time,
			     const char *userid,
			     const char *clientid,
			     void *arg);
typedef void (icall_metrics_h)(struct icall *icall,
			       const char *metrics_json,
			       void *arg);
typedef void (icall_vstate_changed_h)(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      enum icall_vstate state,
				      void *arg);
typedef void (icall_acbr_changed_h)(struct icall *icall, const char *userid,
				    const char *clientid, int enabled,
				    void *arg);
typedef void (icall_quality_h)(struct icall *icall,
			       const char *userid,
			       int rtt, int uploss, int downloss,
			       void *arg);

typedef void (icall_req_clients_h)(struct icall *icall, void *arg);

struct icall {
	icall_add_turnserver		*add_turnserver;
	icall_set_sft			*set_sft;
	icall_start			*start;
	icall_answer			*answer;
	icall_end			*end;
	icall_media_start		*media_start;
	icall_media_stop		*media_stop;
	icall_set_media_laddr           *set_media_laddr;
	icall_set_vstate		*set_video_send_state;
	icall_msg_recv			*msg_recv;
	icall_sft_msg_recv		*sft_msg_recv;
	icall_get_members		*get_members;
	icall_set_quality_interval	*set_quality_interval;
	icall_dce_send                  *dce_send;
	icall_set_clients		*set_clients;
	icall_debug			*debug;
	icall_stats			*stats;

	icall_send_h			*sendh;
	icall_sft_h			*sfth;
	icall_start_h			*starth;
	icall_answer_h			*answerh;
	icall_media_estab_h		*media_estabh;
	icall_audio_estab_h		*audio_estabh;
	icall_datachan_estab_h		*datachan_estabh;
	icall_media_stopped_h		*media_stoppedh;
	icall_group_changed_h		*group_changedh;
	icall_leave_h			*leaveh;
	icall_close_h			*closeh;
	icall_metrics_h			*metricsh;
	icall_vstate_changed_h		*vstate_changedh;
	icall_acbr_changed_h		*acbr_changedh;
	icall_quality_h			*qualityh;
	icall_req_clients_h		*req_clientsh;

	void				*arg;
}__attribute__ ((aligned (8)));

#define ICALL_CALL(icall, fn, ...) if(icall && (icall)->fn){icall->fn(icall, ##__VA_ARGS__);}
#define ICALL_CALLE(icall, fn, ...) (icall && (icall)->fn) ? icall->fn(icall, ##__VA_ARGS__) : 0

#define ICALL_CALL_CB(icall, fn, p0, ...) if(icall.fn){icall.fn(p0, ##__VA_ARGS__);}
#define ICALL_CALL_CBE(icall, fn, p0, ...) icall.fn ? icall.fn(p0, ##__VA_ARGS__) : 0

void icall_set_functions(struct icall *icall,
			 icall_add_turnserver		*add_turnserver,
			 icall_set_sft			*set_sft,
			 icall_start			*start,
			 icall_answer			*answer,
			 icall_end			*end,
			 icall_media_start		*media_start,
			 icall_media_stop		*media_stop,
			 icall_set_media_laddr          *set_media_laddr,
			 icall_set_vstate		*set_video_send_state,
			 icall_msg_recv			*msg_recv,
			 icall_sft_msg_recv		*sft_msg_recv,
			 icall_get_members		*get_members,
			 icall_set_quality_interval	*set_quality_interval,
			 icall_dce_send                 *dce_send,
			 icall_set_clients		*set_clients,
			 icall_debug			*debug,
			 icall_stats			*stats);

void icall_set_callbacks(struct icall *icall,
			 icall_send_h		*sendh,
			 icall_sft_h		*sfth,
			 icall_start_h		*starth,
			 icall_answer_h		*answerh,
			 icall_media_estab_h	*media_estabh,
			 icall_audio_estab_h	*audio_estabh,
			 icall_datachan_estab_h	*datachan_estabh,
			 icall_media_stopped_h	*media_stoppedh,
			 icall_group_changed_h	*group_changedh,
			 icall_leave_h		*leaveh,
			 icall_close_h		*closeh,
			 icall_metrics_h	*metricsh,
			 icall_vstate_changed_h	*vstate_changedh,
			 icall_acbr_changed_h	*acbr_changedh,
			 icall_quality_h	*qualityh,
			 icall_req_clients_h	*req_clientsh,
			 void			*arg);

const char *icall_vstate_name(enum icall_vstate state);

