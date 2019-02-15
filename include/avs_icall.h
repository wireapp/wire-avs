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
#define ICALL_REASON_ANSWERED_ELSEWHERE 5
#define ICALL_REASON_STILL_ONGOING      7
#define ICALL_REASON_REJECTED          10

struct icall;
struct wcall_members;

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
typedef int  (icall_add_turnserver)(struct icall *icall, struct zapi_ice_server *srv);

typedef int  (icall_start)(struct icall *icall, enum icall_call_type call_type, bool audio_cbr,
			   void *extcodec_arg);
typedef int  (icall_answer)(struct icall *icall, enum icall_call_type call_type, bool audio_cbr,
			   void *extcodec_arg);
typedef void (icall_end)(struct icall *icall);
typedef int  (icall_media_start)(struct icall *icall);
typedef void (icall_media_stop)(struct icall *icall);
typedef int  (icall_set_vstate)(struct icall *icall, enum icall_vstate state);
typedef int  (icall_get_members)(struct icall *icall, struct wcall_members **mmp);
typedef int  (icall_msg_recv)(struct icall* icall,
			      uint32_t curr_time,
			      uint32_t msg_time,
			      const char *userid_sender,
			      const char *clientid_sender,
			      struct econn_message *msg);
typedef int  (icall_set_quality_interval)(struct icall *icall, uint64_t interval);
typedef int  (icall_debug)(struct re_printf *pf, const struct icall* icall);

/* Callbacks from icall */
typedef int  (icall_send_h)(const char *userid_sender,
			    struct econn_message *msg, void *arg);
typedef void (icall_start_h)(struct icall *icall,
			     uint32_t msg_time,
			     const char *userid_sender,
			     bool video,
			     bool should_ring,
			     void *arg);
typedef void (icall_answer_h)(void *arg);
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
typedef void (icall_leave_h)(int reason, uint32_t msg_time, void *arg);
typedef void (icall_group_changed_h)(void *arg);
typedef void (icall_close_h)(int err,
			     const char *metrics_json,
			     struct icall *icall,
			     uint32_t msg_time,
			     const char *userid,
			     const char *clientid,
			     void *arg);
typedef void (icall_metrics_h)(const char *metrics_json, void *arg);
typedef void (icall_vstate_changed_h)(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      enum icall_vstate state,
				      void *arg);
typedef void (icall_acbr_changed_h)(struct icall *icall, const char *userid,
				    const char *clientid, int enabled, void *arg);
typedef void (icall_quality_h)(struct icall *icall,
			       const char *userid,
			       int rtt, int uploss, int downloss,
			       void *arg);

struct icall {
	icall_add_turnserver		*add_turnserver;
	icall_start			*start;
	icall_answer			*answer;
	icall_end			*end;
	icall_media_start		*media_start;
	icall_media_stop		*media_stop;
	icall_set_vstate		*set_video_send_state;
	icall_msg_recv			*msg_recv;
	icall_get_members		*get_members;
	icall_set_quality_interval	*set_quality_interval;
	icall_debug			*debug;

	icall_send_h			*sendh;
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

	void				*arg;
}__attribute__ ((aligned (8)));

#define ICALL_CALL(icall, fn, ...) if(icall && (icall)->fn){icall->fn(icall, ##__VA_ARGS__);}
#define ICALL_CALLE(icall, fn, ...) (icall && (icall)->fn) ? icall->fn(icall, ##__VA_ARGS__) : 0

#define ICALL_CALL_CB(icall, fn, p0, ...) if(icall.fn){icall.fn(p0, ##__VA_ARGS__);}
#define ICALL_CALL_CBE(icall, fn, p0, ...) icall.fn ? icall.fn(p0, ##__VA_ARGS__) : 0

void icall_set_functions(struct icall *icall,
			 icall_add_turnserver		*add_turnserver,
			 icall_start			*start,
			 icall_answer			*answer,
			 icall_end			*end,
			 icall_media_start		*media_start,
			 icall_media_stop		*media_stop,
			 icall_set_vstate		*set_video_send_state,
			 icall_msg_recv			*msg_recv,
			 icall_get_members		*get_members,
			 icall_set_quality_interval	*set_quality_interval,
			 icall_debug			*debug);

void icall_set_callbacks(struct icall *icall,
			 icall_send_h		*sendh,
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
			 void			*arg);

const char *icall_vstate_name(enum icall_vstate state);

