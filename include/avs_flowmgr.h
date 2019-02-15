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
#ifndef AVS_FLOWMGR_H
#define AVS_FLOWMGR_H    1

#include "avs_vidframe.h"
#include "avs_audio_effect.h"

struct flowmgr;
struct rr_resp;


int  flowmgr_init(const char *msysname);
int  flowmgr_start(void);
void flowmgr_close(void);
int  flowmgr_is_ready(struct flowmgr *fm, bool *is_ready);
struct msystem *flowmgr_msystem(void);

int  flowmgr_wakeup(void);


typedef int (flowmgr_req_h)(struct rr_resp *ctx,
			    const char *path, const char *method,
			    const char *ctype,
			    const char *content, size_t clen, void *arg);


/**
 * Defines an error handler.
 * This function is called when there was an error when handling a call
 * related flow.
 */
typedef void (flowmgr_err_h)(int err, const char *convid, void *arg);


/**
 * Audio categories define requirements a conversation has towards the
 * audio subsystem. They allow the audio system to correctly configure
 * audio routing and whatnot.
 *
 * In the regular category, the conversation may occasionally play
 * notification tones. In the call category, the conversation has
 * calling enabled and thus requires the entire audio system.
 */
enum flowmgr_mcat {
	FLOWMGR_MCAT_NORMAL = 0,
	FLOWMGR_MCAT_HOLD = 1,
	FLOWMGR_MCAT_ACTIVE = 2,
	FLOWMGR_MCAT_CALL = 3,
	FLOWMGR_MCAT_CALL_VIDEO = 4,	
};


/**
 * Video states for sending and receiving.
 * Send state is set by the user, receive state is a notification to the user.
 */
enum flowmgr_video_send_state {
	FLOWMGR_VIDEO_SEND_NONE = 0,
	FLOWMGR_VIDEO_SEND
};

enum flowmgr_video_receive_state {
	FLOWMGR_VIDEO_RECEIVE_STOPPED = 0,
	FLOWMGR_VIDEO_RECEIVE_STARTED
};

/**
 * Reasons for video stopping.
 */
enum flowmgr_video_reason {
	FLOWMGR_VIDEO_NORMAL = 0,
	FLOWMGR_VIDEO_BAD_CONNECTION
};

enum flowmgr_audio_receive_state {
	FLOWMGR_AUDIO_INTERRUPTION_STOPPED = 0,
	FLOWMGR_AUDIO_INTERRUPTION_STARTED
};


/**
 * Callback used to inform user that received audio is interrupted
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to provide this handler and forward a notification to the application.
 *
 * @param state    New audio state interruption start/stopped
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (flowmgr_audio_state_change_h)(
			enum flowmgr_audio_receive_state state,
			void *arg);


/**
 * Callback used to inform user that received video has started or stopped
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to provide this handler and forward a notification to the application.
 *
 * @param state    New video state start/stopped
 * @param reason   Reason (when stopping), normal/low bandwidth etc.
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (flowmgr_video_state_change_h)(
			enum flowmgr_video_receive_state state,
			enum flowmgr_video_reason reason,
			void *arg);


/**
 * Callback used to inform user that received video frame has
 * changed size.
 */
typedef void (flowmgr_video_size_h)(int w, int h, const char *userid, void *arg);
	

/**
 * Callback used to render frames
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to render the frame. Note the vidframe stuct and its contents are valid
 * only until the function returns. You need to copy to texture or normal RAM before
 * returning.
 *
 * @param frame    Pointer to the frame object to render
 * @param partid   Participant id for the participant whose video has started/stopped
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef int (flowmgr_render_frame_h)(struct avs_vidframe *frame, const char *userid, void *arg);

/**
 * Create a flow manager.
 *
 * Upon success, a pointer to the new flow manager will be returned through
 * *fmp. In this case, the function returns 0. Otherwise an errno-based
 * error value is returned.
 *
 * @param fmp    A pointer to a pointer that will be set to the created
 *               flow manager.
 * @param reqh   A pointer to a request handler function.
 * @param cath   A pointer to an audio category handler function.
 * @param arg    A pointer that is passed in as arg argument to all
 *               handlers.
 */
int flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
		  flowmgr_err_h *errh, void *arg);

/**
 * Set media handlers
 *
 */


void flowmgr_set_audio_state_handler(struct flowmgr *fm,
				flowmgr_audio_state_change_h *state_change_h,
				void *arg);


/**
 * Free the flow manager.
 *
 * XXX Since the public flow manager API is always handed out through
 *     native implementation, this function can be dropped and replaced
 *     by a regular mem_deref().
 */
struct flowmgr *flowmgr_free(struct flowmgr *fm);


enum flowmgr_auplay {
	FLOWMGR_AUPLAY_EARPIECE,
	FLOWMGR_AUPLAY_SPEAKER,
	FLOWMGR_AUPLAY_HEADSET,
	FLOWMGR_AUPLAY_BT,
	FLOWMGR_AUPLAY_LINEOUT,
	FLOWMGR_AUPLAY_SPDIF
};

int flowmgr_auplay_changed(struct flowmgr *fm, enum flowmgr_auplay aplay);

int flowmgr_set_mute(struct flowmgr *fm, bool mute);
int flowmgr_get_mute(struct flowmgr *fm, bool *muted);


bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid);
bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid);
void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state);

void flowmgr_set_video_view(struct flowmgr *fm, const char *convid, const char *partid, void *view);


/* Marshalled functions */
int  marshal_flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
			   flowmgr_err_h *errh, void *arg);
int  marshal_flowmgr_start(void);

void marshal_flowmgr_free(struct flowmgr *fm);
int  marshal_flowmgr_auplay_changed(struct flowmgr *fm,
				    enum flowmgr_auplay aplay);
int  marshal_flowmgr_set_mute(struct flowmgr *fm, bool mute);
int  marshal_flowmgr_get_mute(struct flowmgr *fm, bool *muted);

void marshal_flowmgr_set_video_handlers(struct flowmgr *fm, 
			flowmgr_video_state_change_h *state_change_h,
			flowmgr_render_frame_h *render_frame_h,
			flowmgr_video_size_h *sizeh,
			void *arg);

void marshal_flowmgr_set_audio_state_handler(struct flowmgr *fm,
			flowmgr_audio_state_change_h *audio_state_change_handler,
			void *arg);

int marshal_flowmgr_can_send_video(struct flowmgr *fm, const char *convid);
int marshal_flowmgr_is_sending_video(struct flowmgr *fm,
				     const char *convid, const char *partid);
void marshal_flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state);


/* Wrap flow manager calls into these macros if you want to call them
 * from outside the re thread.
 */

#define FLOWMGR_MARSHAL_RET(t, r, f, ...)       \
	if ((t) == pthread_self())              \
		(r) = (f)(__VA_ARGS__);	        \
	else {                              \
		(r) = marshal_##f(__VA_ARGS__); \
	}

#define FLOWMGR_MARSHAL_VOID(t, f, ...)   \
	if ((t) == pthread_self())        \
		(f)(__VA_ARGS__);	  \
	else {                            \
	        marshal_##f(__VA_ARGS__); \
	}


#endif /* #ifndef AVS_FLOWMGR_H */
