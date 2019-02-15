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


#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

struct wcall;
struct avs_vidframe;

#define WCALL_VERSION_3 3


struct wcall_member {
	char *userid;
	int audio_estab;
	int video_recv;
};

struct wcall_members {
	struct wcall_member *membv;
	size_t membc;
};
	
	
/* This will be called when the calling system is ready for calling.
 * The version parameter specifies the config obtained version to use for
 * calling.
 */
typedef void (wcall_ready_h)(int version, void *arg);

typedef void (wcall_shutdown_h)(void *wuser, void *arg);

/* Send calling message otr data */
typedef int (wcall_send_h)(void *ctx, const char *convid,
			   const char *userid_self, const char *clientid_self,
			   const char *userid_dest, const char *clientid_dest,
			   const uint8_t *data, size_t len,
			   int transient /*bool*/,
			   void *arg);

/* Incoming call */
typedef void (wcall_incoming_h)(const char *convid, uint32_t msg_time,
				const char *userid, int video_call /*bool*/,
				int should_ring /*bool*/,
				void *arg);

/* Missed Incoming call */
typedef void (wcall_missed_h)(const char *convid, uint32_t msg_time,
			      const char *userid, int video_call /*bool*/,
			      void *arg);

/* Network quality info */
#define WCALL_QUALITY_NORMAL 1
#define WCALL_QUALITY_MEDIUM 2
#define WCALL_QUALITY_POOR   3
typedef void (wcall_network_quality_h)(const char *convid,
				       const char *userid,
				       int quality, /*  WCALL_QUALITY_ */
				       int rtt, /* round trip time in ms */
				       int uploss, /* upstream pkt loss % */
				       int downloss, /* dnstream pkt loss % */
				       void *arg); 


/**
 * Callback used to inform user that a call was answered
 *
 * @param convid   Conversation id on which call was answered
 * @param arg      User context passed to wcall_init
 *
 * NOTE: Only relevant for one-to-one calls
 */
typedef void (wcall_answered_h)(const char *convid, void *arg);
	
/* Call established (with media) */
typedef void (wcall_estab_h)(const char *convid,
			   const char *userid, void *arg);

/** 
 * Callback used to inform the user that the participant list
 * in a group call has changed. Use the wcall_get_members to get the list.
 * When done processing the list be sure to call wcall_free_members.
 *
 * @param convid   Conversation id on which participant list has changed
 * @param arg      User context passed to wcall_set_group_changed_handler
 */

typedef void (wcall_group_changed_h)(const char *convid, void *arg);

/* All media has been stopped */
typedef void (wcall_media_stopped_h)(const char *convid, void *arg);
	
	
/* Data channel established */
typedef void (wcall_data_chan_estab_h)(const char *convid,
				       const char *userid, void *arg);

	

#define WCALL_REASON_NORMAL             0
#define WCALL_REASON_ERROR              1
#define WCALL_REASON_TIMEOUT            2
#define WCALL_REASON_LOST_MEDIA         3
#define WCALL_REASON_CANCELED           4
#define WCALL_REASON_ANSWERED_ELSEWHERE 5
#define WCALL_REASON_IO_ERROR           6
#define WCALL_REASON_STILL_ONGOING      7
#define WCALL_REASON_TIMEOUT_ECONN      8
#define WCALL_REASON_DATACHANNEL        9
#define WCALL_REASON_REJECTED          10
    
const char *wcall_reason_name(int reason);


/* Call terminated */
typedef void (wcall_close_h)(int reason, const char *convid, uint32_t msg_time,
			   const char *userid, void *arg);

/* Call metrics */
typedef void (wcall_metrics_h)(const char *convid,
    const char *metrics_json, void *arg);

/* Log handler */
#define WCALL_LOG_LEVEL_DEBUG 0
#define WCALL_LOG_LEVEL_INFO  1
#define WCALL_LOG_LEVEL_WARN  2
#define WCALL_LOG_LEVEL_ERROR 3

typedef void (wcall_log_h)(int level, const char *msg, void *arg);
    
/* Video receive state */
#define	WCALL_VIDEO_STATE_STOPPED     0
#define	WCALL_VIDEO_STATE_STARTED     1
#define	WCALL_VIDEO_STATE_BAD_CONN    2
#define	WCALL_VIDEO_STATE_PAUSED      3
#define	WCALL_VIDEO_STATE_SCREENSHARE 4

/**
 * Callback used to inform user that received video has started or stopped
 *
 * @param state    New video state start/stopped
 * @param reason   Reason (when stopping), normal/low bandwidth etc.
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (wcall_video_state_change_h)(const char *userid, int state, void *arg);

/**
 * Callback used to inform user that received video frame has
 * changed size.
 * @paran w        New video width
 * @param h        New video height
 * @param userid   User ID for the participant whose video to render
 * @param arg      The handler argument passed to the callback
 */
typedef void (wcall_video_size_h)(int w, int h, const char *userid, void *arg);
	
/**
 * Callback used to render frames
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to render the frame. Note the vidframe stuct and its contents are valid
 * only until the function returns. You need to copy to texture or normal RAM before
 * returning.
 *
 * @param frame    Pointer to the frame object to render
 * @param userid   User ID for the participant whose video to render
 * @param arg      The handler argument passed to the callback
 */
typedef int (wcall_render_frame_h)(struct avs_vidframe *frame, const char *userid, void *arg);

/**
 * Callback used to inform user that call uses CBR (in both directions)
 */
typedef void (wcall_audio_cbr_change_h)(const char *userid, int enabled, void *arg);

typedef int (wcall_config_req_h)(void *wuser, void *arg);	

int wcall_init(void);
void wcall_close(void);

void *wcall_create(const char *userid,
	       const char *clientid,
	       wcall_ready_h *readyh,
	       wcall_send_h *sendh,
	       wcall_incoming_h *incomingh,
	       wcall_missed_h *missedh,
	       wcall_answered_h *answerh,
	       wcall_estab_h *estabh,
	       wcall_close_h *closeh,
	       wcall_metrics_h *metricsh,
	       wcall_config_req_h *cfg_reqh,
	       wcall_audio_cbr_change_h *acbrh,
	       wcall_video_state_change_h *vstateh,
	       void *arg);

void *wcall_create_ex(const char *userid,
		      const char *clientid,
		      int use_mediamgr /*bool*/,
		      wcall_ready_h *readyh,
		      wcall_send_h *sendh,
		      wcall_incoming_h *incomingh,
		      wcall_missed_h *missedh,
		      wcall_answered_h *answerh,
		      wcall_estab_h *estabh,
		      wcall_close_h *closeh,
		      wcall_metrics_h *metricsh,
		      wcall_config_req_h *cfg_reqh,
		      wcall_audio_cbr_change_h *acbrh,
		      wcall_video_state_change_h *vstateh,
		      void *arg);

void wcall_set_shutdown_handler(void *wuser,
				wcall_shutdown_h *shuth, void *arg);
void wcall_destroy(void *wuser);
	

void wcall_set_trace(void *wuser, int trace);


#define WCALL_CALL_TYPE_NORMAL          0
#define WCALL_CALL_TYPE_VIDEO           1
#define WCALL_CALL_TYPE_FORCED_AUDIO    2

#define WCALL_CONV_TYPE_ONEONONE        0
#define WCALL_CONV_TYPE_GROUP           1
#define WCALL_CONV_TYPE_CONFERENCE      2

/* Returns 0 if successfull
 * Set call_type and conv_type from defines above.
 * Set audio_cbr to 0 for false, non-zero for true.
 */
int wcall_start(void *wuser, const char *convid,
		int call_type, /*WCALL_CALL_TYPE...*/
		int conv_type, /*WCALL_CONV_TYPE...*/
		int audio_cbr /*bool*/);

int wcall_start_ex(void *wuser, const char *convid,
		   int call_type, /*WCALL_CALL_TYPE...*/
		   int conv_type, /*WCALL_CONV_TYPE...*/
		   int audio_cbr /*bool*/,
		   void *extcodec_arg);
	
/* Returns 0 if successfull */
int wcall_answer(void *wuser, const char *convid,
		 int call_type, /*WCALL_CALL_TYPE...*/
		 int audio_cbr /*bool*/);

int wcall_answer_ex(void *wuser, const char *convid,
		    int call_type, /*WCALL_CALL_TYPE...*/
		    int audio_cbr /*bool*/,
		    void *extcodec_arg);


/* Async response from send handler.
 * The ctx parameter MUST be the same context provided in the
 * call to the send_handler callback
 */
void wcall_resp(void *wuser, int status, const char *reason, void *ctx);

void wcall_config_update(void *wuser, int err, const char *json_str);
	
/* An OTR call-type message has been received,
 * msg_time is the backend timestamp of when the message was received
 * curr_time is the timestamp (synced as close as possible)
 * to the backend time when this function is called.
 */
void wcall_recv_msg(void *wuser, const uint8_t *buf, size_t len,
		    uint32_t curr_time, /* timestamp in seconds */
		    uint32_t msg_time,  /* timestamp in seconds */
		    const char *convid,
		    const char *userid,
		    const char *clientid);

/* End the call in the conversation associated to
 * the conversation id in the convid parameter.
 */
void wcall_end(void *wuser, const char *convid);

/* Reject a call */
int wcall_reject(void *wuser, const char *convid);

int wcall_is_video_call(void *wuser, const char *convid /*bool*/);

void wcall_set_media_stopped_handler(void *wuser,
				     wcall_media_stopped_h *mstoph);
void wcall_set_data_chan_estab_handler(void *wuser,
				       wcall_data_chan_estab_h *dcestabh);

/* Start/stop sending video to the remote side.
 */
void wcall_set_video_send_state(void *wuser, const char *convid, int state);

void wcall_set_video_handlers(wcall_render_frame_h *render_frame_h,
			      wcall_video_size_h *size_h,
			      void *arg);

void wcall_network_changed(void *wuser);

void wcall_set_group_changed_handler(void *wuser, wcall_group_changed_h *chgh,
				     void *arg);

int wcall_set_network_quality_handler(void *wuser,
				      wcall_network_quality_h *netqh,
				      int interval, /* in s */
				      void *arg);

void wcall_set_log_handler(wcall_log_h *logh, void *arg);

struct re_printf;
int  wcall_debug(struct re_printf *pf, const void *id);


#define WCALL_STATE_NONE         0 /* There is no call */
#define WCALL_STATE_OUTGOING     1 /* Outgoing call is pending */
#define WCALL_STATE_INCOMING     2 /* Incoming call is pending */
#define WCALL_STATE_ANSWERED     3 /* Call has been answered, but no media */
#define WCALL_STATE_MEDIA_ESTAB  4 /* Call has been answered, with media */
#define WCALL_STATE_TERM_LOCAL   6 /* Call was locally terminated */
#define WCALL_STATE_TERM_REMOTE  7 /* Call was remotely terminated */
#define WCALL_STATE_UNKNOWN      8 /* Unknown */

/**
 * Callback used to inform user that call state has changed
 *
 * @param convid   Conversation id on whchstate has changed
 * @param state    New call state according to above
 * @param arg      User context passed to wcall_init
 */
typedef void (wcall_state_change_h)(const char *convid, int state, void *arg);

	
void wcall_set_state_handler(void *wuser, wcall_state_change_h *stateh);
int  wcall_get_state(void *wuser, const char *convid);
const char *wcall_state_name(int st);

/**
 * Syncronously call the callback handler for each WCALL state that is not IDLE
 */
void wcall_iterate_state(void *wuser, wcall_state_change_h *stateh, void *arg);	
	
struct ecall;
struct ecall *wcall_ecall(void *wuser, const char *convid);

void wcall_propsync_request(void *wuser, const char *convid);


/**
 * Returns the members of a group conversation.
 *
 * Make sure to always call wcall_free_members after done with the
 * returned members.
 *
 * @param convid  conversation id for which to return members for
 *
 */
struct wcall_members *wcall_get_members(void *wuser, const char *convid);
void wcall_free_members(struct wcall_members *members);
	
void wcall_enable_privacy(void *wuser, int enabled);

struct mediamgr *wcall_mediamgr(void *wuser);

void wcall_handle_frame(struct avs_vidframe *frame);


/*
 * Netprobe
 */

typedef void (wcall_netprobe_h)(int err,
				uint32_t rtt_avg,
				size_t n_pkt_sent,
				size_t n_pkt_recv,
				void *arg);


int wcall_netprobe(void *id, size_t pkt_count, uint32_t pkt_interval_ms,
		   wcall_netprobe_h *netprobeh, void *arg);


#ifdef __cplusplus
}
#endif
