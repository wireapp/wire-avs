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

#define WUSER_HANDLE uint32_t
#define WUSER_INVALID_HANDLE ((WUSER_HANDLE)0)
	
#define WCALL_VERSION_3 3

#define WCALL_ERROR_UNKNOWN_PROTOCOL 1000

struct wcall_member {
	char *userid;
	char *clientid;
	int audio_state;
	int video_recv;
	int muted;
};

struct wcall_members {
	struct wcall_member *membv;
	size_t membc;
};

struct wcall_client {
	char *userid;
	char *clientid;
};

/* Returns a constant string of the current AVS library version in the
 * format <major>.<minor>.<build> e.g. 5.1.25 for a 5.1 build,
 * 0.0.3000 for AVS master. Local builds have 0.0.snapshot
 */
const char *wcall_library_version(void);

/* This will be called when the calling system is ready for calling.
 * The version parameter specifies the config obtained version to use for
 * calling.
 */
typedef void (wcall_ready_h)(int version, void *arg);

typedef void (wcall_shutdown_h)(WUSER_HANDLE wuser, void *arg);

/* Send calling message otr data */
typedef int (wcall_send_h)(void *ctx, const char *convid,
			   const char *userid_self, const char *clientid_self,
			   const char *targets /*optional*/,
			   const char *unused /*optional*/,
			   const uint8_t *data, size_t len,
			   int transient /*bool*/,
			   void *arg);

/* Send message to SFT */
typedef int (wcall_sft_req_h)(void *ctx, const char *url,
			      const uint8_t *data, size_t len,
			      void *arg);

/* Incoming call */
typedef void (wcall_incoming_h)(const char *convid, uint32_t msg_time,
				const char *userid, const char *clientid,
				int video_call /*bool*/,
				int should_ring /*bool*/,
				int conv_type, /*WCALL_CONV_TYPE...*/
				void *arg);

/* Missed Incoming call */
typedef void (wcall_missed_h)(const char *convid, uint32_t msg_time,
			      const char *userid, const char *clientid,
			      int video_call /*bool*/,
			      void *arg);

/* Network quality info */
#define WCALL_QUALITY_NORMAL          1
#define WCALL_QUALITY_MEDIUM          2
#define WCALL_QUALITY_POOR            3
#define WCALL_QUALITY_NETWORK_PROBLEM 4
#define WCALL_QUALITY_RECONNECTING    5

typedef void (wcall_network_quality_h)(const char *convid,
				       const char *userid,
				       const char *clientid,
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
			     const char *userid,
			     const char *clientid,
			     void *arg);

/** 
 * Callback used to inform the user that the participant list
 * in a group call has changed. Use the wcall_get_members to get the list.
 * When done processing the list be sure to call wcall_free_members.
 *
 * @param convid   Conversation id on which participant list has changed
 * @param arg      User context passed to wcall_set_group_changed_handler
 */

typedef void (wcall_group_changed_h)(const char *convid, void *arg);

typedef void (wcall_participant_changed_h)(const char *convid,
					   const char *mjson, void *arg);
	
/* Media has been established */
typedef void (wcall_media_estab_h)(const char *convid,
				   void *peer,
				   const char *userid,
				   const char *clientid,
				   void *arg);
	
/* All media has been stopped */
typedef void (wcall_media_stopped_h)(const char *convid, void *arg);
	
	
/* Data channel established */
typedef void (wcall_data_chan_estab_h)(const char *convid,
				       const char *userid,
				       const char *clientid,
				       void *arg);
/* Mute handler */
typedef void (wcall_mute_h)(int muted, void *arg);

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
#define WCALL_REASON_OUTDATED_CLIENT   11
#define WCALL_REASON_NOONE_JOINED      12
#define WCALL_REASON_EVERYONE_LEFT     13

const char *wcall_reason_name(int reason);


/* Call terminated */
typedef void (wcall_close_h)(int reason,
			     const char *convid,
			     uint32_t msg_time,
			     const char *userid,
			     const char *clientid,
			     void *arg);
/* Call metrics */
typedef void (wcall_metrics_h)(const char *convid,
    const char *metrics_json, void *arg);

/* Log handler */
#define WCALL_LOG_LEVEL_DEBUG 0
#define WCALL_LOG_LEVEL_INFO  1
#define WCALL_LOG_LEVEL_WARN  2
#define WCALL_LOG_LEVEL_ERROR 3

typedef void (wcall_log_h)(int level,
			   const char *msg,
			   void *arg /*any*/);

/* Audio call-progress states */
/* Audio is in the proess of connecting */	
#define WCALL_AUDIO_STATE_CONNECTING       0
/* Audio has been established -- audio media flowing */	
#define WCALL_AUDIO_STATE_ESTABLISHED      1
/* No relay candidate -- audio MAY still connect */	
#define WCALL_AUDIO_STATE_NETWORK_PROBLEM  2 

/* Video receive state */
#define	WCALL_VIDEO_STATE_STOPPED     0
#define	WCALL_VIDEO_STATE_STARTED     1
#define	WCALL_VIDEO_STATE_BAD_CONN    2
#define	WCALL_VIDEO_STATE_PAUSED      3
#define	WCALL_VIDEO_STATE_SCREENSHARE 4

/**
 * Callback used to inform user that received video has started or stopped
 *
 * @param state    New video state (one of WCALL_VIDEO_STATE_x above).
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (wcall_video_state_change_h)(const char *convid, 
					  const char *userid,
					  const char *clientid,
					  int state,
					  void *arg);

/**
 * Callback used to inform user that received video frame has
 * changed size.
 * @paran w        New video width
 * @param h        New video height
 * @param userid   User ID for the participant whose video to render
 * @param clientid Client ID for the participant whose video to render
 * @param arg      The handler argument passed to the callback
 */
typedef void (wcall_video_size_h)(int w,
				  int h,
				  const char *userid,
				  const char *clientid,
				  void *arg);
	
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
 * @param clientid Client ID for the participant whose video to render
 * @param arg      The handler argument passed to the callback
 */
typedef int (wcall_render_frame_h)(struct avs_vidframe *frame,
				   const char *userid,
				   const char *clientid,
				   void *arg);

/**
 * Callback used to inform user that call uses CBR (in both directions)
 */
typedef void (wcall_audio_cbr_change_h)(const char *userid,
					const char *clientid,
					int enabled,
					void *arg);

typedef int (wcall_config_req_h)(WUSER_HANDLE wuser, void *arg);	

#define WCALL_ENV_DEFAULT 0
#define WCALL_ENV_FIREFOX 1
	
int wcall_init(int env);
void wcall_close(void);

WUSER_HANDLE wcall_create(const char *userid,
			  const char *clientid,
			  wcall_ready_h *readyh,
			  wcall_send_h *sendh,
			  wcall_sft_req_h *sfth,
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

WUSER_HANDLE wcall_create_ex(const char *userid,
			     const char *clientid,
			     int use_mediamgr /*bool*/,
			     const char *msys_name,
			     wcall_ready_h *readyh,
			     wcall_send_h *sendh,
			     wcall_sft_req_h *sfth,
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

void wcall_set_shutdown_handler(WUSER_HANDLE wuser,
				wcall_shutdown_h *shuth, void *arg);
void wcall_destroy(WUSER_HANDLE wuser);
	

void wcall_set_trace(WUSER_HANDLE wuser, int trace);


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
int wcall_start(WUSER_HANDLE wuser, const char *convid,
		int call_type, /*WCALL_CALL_TYPE...*/
		int conv_type, /*WCALL_CONV_TYPE...*/
		int audio_cbr /*bool*/);

/* Returns 0 if successfull
 * Set call_type from defines above.
 * Set audio_cbr to 0 for false, non-zero for true.
 */
int wcall_answer(WUSER_HANDLE wuser, const char *convid,
		 int call_type, /*WCALL_CALL_TYPE...*/
		 int audio_cbr /*bool*/);

/* Async response from send handler.
 * The ctx parameter MUST be the same context provided in the
 * call to the send_handler callback
 */
void wcall_resp(WUSER_HANDLE wuser, int status, const char *reason, void *ctx);

void wcall_config_update(WUSER_HANDLE wuser, int err, const char *json_str);

void wcall_sft_resp(WUSER_HANDLE wuser,
		    int perr, const uint8_t *buf, size_t len, void *ctx);

/* An OTR call-type message has been received,
 * msg_time is the backend timestamp of when the message was received
 * curr_time is the timestamp (synced as close as possible)
 * to the backend time when this function is called.
 */
int  wcall_recv_msg(WUSER_HANDLE wuser, const uint8_t *buf, size_t len,
		    uint32_t curr_time, /* timestamp in seconds */
		    uint32_t msg_time,  /* timestamp in seconds */
		    const char *convid,
		    const char *userid,
		    const char *clientid);

/* End the call in the conversation associated to
 * the conversation id in the convid parameter.
 */
void wcall_end(WUSER_HANDLE wuser, const char *convid);

/* Reject a call */
int wcall_reject(WUSER_HANDLE wuser, const char *convid);

int wcall_is_video_call(WUSER_HANDLE wuser, const char *convid /*bool*/);

void wcall_set_media_estab_handler(WUSER_HANDLE wuser,
				   wcall_media_estab_h *mestabh);
void wcall_set_media_stopped_handler(WUSER_HANDLE wuser,
				     wcall_media_stopped_h *mstoph);
void wcall_set_data_chan_estab_handler(WUSER_HANDLE wuser,
				       wcall_data_chan_estab_h *dcestabh);

struct mbuf;
void wcall_dce_send(WUSER_HANDLE wuser, const char *convid, struct mbuf *mb);
	
	

/* Start/stop sending video to the remote side.
 */
void wcall_set_video_send_state(WUSER_HANDLE wuser,
				const char *convid, int state);

void wcall_set_video_handlers(wcall_render_frame_h *render_frame_h,
			      wcall_video_size_h *size_h,
			      void *arg);

void wcall_network_changed(WUSER_HANDLE wuser);

void wcall_set_group_changed_handler(WUSER_HANDLE wuser,
				     wcall_group_changed_h *chgh,
				     void *arg);

void wcall_set_participant_changed_handler(WUSER_HANDLE wuser,
					   wcall_participant_changed_h *chgh,
					   void *arg);
	

int wcall_set_network_quality_handler(WUSER_HANDLE wuser,
				      wcall_network_quality_h *netqh,
				      int interval, /* in s */
				      void *arg);

void wcall_set_log_handler(wcall_log_h *logh, void *arg);

struct sa;
void wcall_set_media_laddr(WUSER_HANDLE wuser, struct sa *laddr);

int wcall_get_mute(WUSER_HANDLE wuser);
void wcall_set_mute(WUSER_HANDLE wuser, int muted);
void wcall_set_mute_handler(WUSER_HANDLE wuser, wcall_mute_h *muteh, void *arg);
	
struct re_printf;
int  wcall_debug(struct re_printf *pf, WUSER_HANDLE wuser);
int  wcall_stats(struct re_printf *pf, WUSER_HANDLE wuser);


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

	
void wcall_set_state_handler(WUSER_HANDLE wuser, wcall_state_change_h *stateh);
int  wcall_get_state(WUSER_HANDLE wuser, const char *convid);
const char *wcall_state_name(int st);

/**
 * Syncronously call the callback handler for each WCALL state that is not IDLE
 */
void wcall_iterate_state(WUSER_HANDLE wuser,
			 wcall_state_change_h *stateh, void *arg);	
	
struct ecall;
struct ecall *wcall_ecall(WUSER_HANDLE wuser, const char *convid);

void wcall_propsync_request(WUSER_HANDLE wuser, const char *convid);


/**
 * Returns the members of a group conversation.
 *
 * Make sure to always call wcall_free_members after done with the
 * returned members.
 *
 * @param convid  conversation id for which to return members for
 *
 */
struct wcall_members *wcall_get_members(WUSER_HANDLE wuser, const char *convid);
void wcall_free_members(struct wcall_members *members);

void wcall_enable_privacy(WUSER_HANDLE wuser, int enabled);

struct mediamgr *wcall_mediamgr(WUSER_HANDLE wuser);

void wcall_handle_frame(struct avs_vidframe *frame);

int wcall_set_proxy(const char *host, int port);

/**
 * Client list management (conference calls).
 */
typedef void (wcall_req_clients_h)(WUSER_HANDLE wuser,
				   const char *convid, void *arg);

void wcall_set_req_clients_handler(WUSER_HANDLE wuser,
				   wcall_req_clients_h *reqch);
int wcall_set_clients_for_conv(WUSER_HANDLE wuser,
			       const char *convid,
			       const char *json);

/**
 * Active speaker (conference calls).
 */
typedef void (wcall_active_speaker_h)(WUSER_HANDLE wuser,
				      const char *convid,
				      const char *json_levels, void *arg);
	
void wcall_set_active_speaker_handler(WUSER_HANDLE wuser,
				      wcall_active_speaker_h *activeh);

#define WCALL_VSTREAMS_LIST 0
/**
 * Video stream selection (conference calls).
 */
int wcall_request_video_streams(WUSER_HANDLE wuser,
				const char *convid,
				int mode,
				const char *json);

/*
 * Netprobe
 */

typedef void (wcall_netprobe_h)(int err,
				uint32_t rtt_avg,
				size_t n_pkt_sent,
				size_t n_pkt_recv,
				void *arg);


int wcall_netprobe(WUSER_HANDLE wuser,
		   size_t pkt_count, uint32_t pkt_interval_ms,
		   wcall_netprobe_h *netprobeh, void *arg);


void wcall_thread_main(int *error, int *initialized);
int wcall_run(void);
void wcall_poll(void);
int wcall_setup(void);

#ifdef __cplusplus
}
#endif
