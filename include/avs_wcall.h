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

#define WCALL_VERSION_2 2
#define WCALL_VERSION_3 3

struct wcall_member {
	char *userid;
	int audio_estab;
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

/* Send calling message otr data */
typedef int (wcall_send_h)(void *ctx, const char *convid, const char *userid,
			   const char *clientid,
			   const uint8_t *data, size_t len,
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


/**
 * Callback used to inform user that a call was answered
 *
 * @param convid   Conversation id on which call was answered
 * @param arg      User context passed to wcall_init
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


#define WCALL_REASON_NORMAL             0
#define WCALL_REASON_ERROR              1
#define WCALL_REASON_TIMEOUT            2
#define WCALL_REASON_LOST_MEDIA         3
#define WCALL_REASON_CANCELED           4
#define WCALL_REASON_ANSWERED_ELSEWHERE 5
#define WCALL_REASON_IO_ERROR           6
#define WCALL_REASON_STILL_ONGOING      7
    
/* Call terminated */
typedef void (wcall_close_h)(int reason, const char *convid, uint32_t msg_time,
			   const char *userid, void *arg);

/* Call metrics */
typedef void (wcall_metrics_h)(const char *convid,
    const char *metrics_json, void *arg);
    
/* Video receive state */
#define	WCALL_VIDEO_RECEIVE_STOPPED  0
#define	WCALL_VIDEO_RECEIVE_STARTED  1
#define	WCALL_VIDEO_RECEIVE_BAD_CONN 2

/**
 * Callback used to inform user that received video has started or stopped
 *
 * @param state    New video state start/stopped
 * @param reason   Reason (when stopping), normal/low bandwidth etc.
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (wcall_video_state_change_h)(int state, void *arg);

/**
  * Callback used to inform user that other side is sending us cbr audio
 */
typedef void (wcall_audio_cbr_enabled_h)(void *arg);
    
int wcall_init(const char *userid,
	       const char *clientid,
	       wcall_ready_h *readyh,
	       wcall_send_h *sendh,
	       wcall_incoming_h *incomingh,
	       wcall_missed_h *missedh,
	       wcall_answered_h *answerh,
	       wcall_estab_h *estabh,
	       wcall_close_h *closeh,
	       wcall_metrics_h *metricsh,
	       void *arg);

void wcall_close(void);
	

void wcall_set_trace(int trace);


/* Returns 0 if successfull
 * Set is_video_call to 0 for false, non-0 for true
 */
int wcall_start(const char *convid,
		int is_video_call /*bool*/,
		int group /*bool*/);

/* Returns 0 if successfull */
int wcall_answer(const char *convid,
		 int group /*bool*/);

/* Async response from send handler.
 * The ctx parameter MUST be the same context provided in the
 * call to the send_handler callback
 */
void wcall_resp(int status, const char *reason, void *ctx);

/* An OTR call-type message has been received,
 * msg_time is the backend timestamp of when the message was received
 * curr_time is the timestamp (synced as close as possible)
 * to the backend time when this function is called.
 */
void wcall_recv_msg(const uint8_t *buf, size_t len,
		    uint32_t curr_time, /* timestamp in seconds */
		    uint32_t msg_time,  /* timestamp in seconds */
		    const char *convid,
		    const char *userid,
		    const char *clientid);

/* End the call in the conversation associated to
 * the conversation id in the convid parameter.
 */
void wcall_end(const char *convid,
	       int group /*bool*/);

/* Reject a call */
int wcall_reject(const char *convid, int group /* bool */);

int wcall_is_video_call(const char *convid /*bool*/);

void wcall_set_video_state_handler(wcall_video_state_change_h *vstateh);

/* Start/stop sending video to the remote side.
 */
void wcall_set_video_send_active(const char *convid, int active /*bool*/);

void wcall_network_changed(void);

void wcall_set_group_changed_handler(wcall_group_changed_h *chgh,
				     void *arg);
void wcall_set_audio_cbr_enabled_handler(wcall_audio_cbr_enabled_h *acbrh);

struct re_printf;
int  wcall_debug(struct re_printf *pf, void *ignored);


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

	
void wcall_set_state_handler(wcall_state_change_h *stateh);
int  wcall_get_state(const char *convid);
const char *wcall_state_name(int st);

/**
 * Syncronously call the callback handler for each WCALL state that is not IDLE
 */
void wcall_iterate_state(wcall_state_change_h *stateh, void *arg);	
	
struct ecall;
struct ecall *wcall_ecall(const char *convid);

void wcall_propsync_request(const char *convid);

void wcall_enable_audio_cbr(int enabled);

/**
 * Returns the members of a group conversation.
 *
 * Make sure to always call wcall_free_members after done with the
 * returned members.
 *
 * @param convid  conversation id for which to return members for
 *
 */
struct wcall_members *wcall_get_members(const char *convid);
void wcall_free_members(struct wcall_members *members);
	
#ifdef __cplusplus
}
#endif
