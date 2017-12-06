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

struct mqueue *flowmgr_mqueue(void);
int  flowmgr_wakeup(void);


/**
 * Defines the HTTP Request handler for sending REST-requests. The
 * application must implement this callback handler, and send the real
 * HTTP request to the backend server and wait for the response.
 * When the response arrives the application must call the function
 * flowmgr_resp() with the same context (void *ctx).
 *
 * @param ctx     HTTP Request context, must be passed to flowmgr_resp() (can be NULL)
 * @param path    URL path (e.g. /conversations/9a088c8f-.../
 * @param method  HTTP Method
 * @param ctype   Content-type (e.g. application/json)
 * @param content The actual content (not zero-terminated)
 * @param clen    Number of bytes in the content
 * @param arg     Handler argument
 *
 * @return 0 if success, otherwise errorcode
 *
 * @note ctx might be NULL if flow-manager is not interested in a response.
 */
typedef int (flowmgr_req_h)(struct rr_resp *ctx,
			    const char *path, const char *method,
			    const char *ctype,
			    const char *content, size_t clen, void *arg);


typedef const char *(flowmgr_username_h)(const char *userid, void *arg);


/**
 * Defines an error handler.
 * This function is called when there was an error when handling a call
 * related flow.
 */
typedef void (flowmgr_err_h)(int err, const char *convid, void *arg);


typedef void (flowmgr_netq_h)(int err, const char *convid, float q, void *arg);

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
 * Defines the audio category handler. This function is called when the
 * flow managers needs the audio category to change for a conversation.
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to provide this handler and forward a call to their audio manager.
 *
 * @param convid   A string uniquely identifying the conversation.
 * @param cat      The audio category this conversation needs to be
 *                 switched to.
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (flowmgr_mcat_chg_h)(const char *convid,
				  enum flowmgr_mcat cat, void *arg);

typedef void (flowmgr_volume_h)(const char *convid,
				const char *userid,
				double invol, double outvol,
				void *arg);

typedef void (flowmgr_media_estab_h)(const char *convid, bool estab,
				     void *arg);

typedef void (flowmgr_conf_pos_h)(const char *convid,
				  struct list *partl, void *arg);


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
typedef void (flowmgr_video_size_h)(int w, int h, void *arg);
	

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
typedef int (flowmgr_render_frame_h)(struct avs_vidframe *frame, void *arg);

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
void flowmgr_set_media_handlers(struct flowmgr *fm, flowmgr_mcat_chg_h *cath,
				     flowmgr_volume_h *volh, void *arg);


void flowmgr_set_media_estab_handler(struct flowmgr *fm,
				     flowmgr_media_estab_h *mestabh,
				     void *arg);

void flowmgr_set_conf_pos_handler(struct flowmgr *fm,
				  flowmgr_conf_pos_h *conf_posh,
				  void *arg);

void flowmgr_set_username_handler(struct flowmgr *fm,
				  flowmgr_username_h *usernameh, void *arg);

void flowmgr_set_video_handlers(struct flowmgr *fm, 
				flowmgr_video_state_change_h *state_change_h,
				flowmgr_render_frame_h *render_frame_h,
				flowmgr_video_size_h *size_h,
				void *arg);

void flowmgr_set_audio_state_handler(struct flowmgr *fm,
				flowmgr_audio_state_change_h *state_change_h,
				void *arg);

void flowmgr_set_sessid(struct flowmgr *fm, const char *convid,
			const char *sessid);

int  flowmgr_interruption(struct flowmgr *fm, const char *convid,
			  bool interrupted);




/**
 * Free the flow manager.
 *
 * XXX Since the public flow manager API is always handed out through
 *     native implementation, this function can be dropped and replaced
 *     by a regular mem_deref().
 */
struct flowmgr *flowmgr_free(struct flowmgr *fm);


const char *flowmgr_get_username(struct flowmgr *fm, const char *userid);




/* Pass a (websocket) event to flow manager *fm*.
 *
 * The media type of the event should be in *ctype* and the raw content
 * of the event in *content* and its size in octets in *clen*.
 *
 * Returns 0 if sucessfull or an errno based error code on failure.
 * If hp is non-NULL it will be populatd with true if the event was handled
 */
int flowmgr_process_event(bool *hp, struct flowmgr *fm,
			  const char *ctype, const char *content, size_t clen);



/* Pass a response to a request made through flowmgr_req_h.
 *
 * The *status* should be an HTTP status code or -1 if the request failed
 * without ever having been sent out. Pass the media type in *ctype* and
 * the raw body in *content* with its length in octets in *clen*. Pass
 * the value of the *ctx* argument of the respective flowmgr_req_h in#
 * *ctx*.
 */
int flowmgr_resp(struct flowmgr *fm, int status, const char *reason,
		 const char *ctype, const char *content, size_t clen,
		 struct rr_resp *rr);



/* Advice the flow manager *fm* to acquire flows for *convid.*
 *
 * This will trigger creation of flows and media negotation. It does,
 * however, not start an actual call. You have to do this by setting
 * your device call state.
 *
 * Recording and playback will start automatically once the backend has
 * activated a flow.
 *
 * If qh is set network probing will be performed and the specified handler
 * will be called with the calculated network quality.
 */
int flowmgr_acquire_flows(struct flowmgr *fm, const char *convid,
			  const char *sessionid,
			  flowmgr_netq_h *qh, void *arg);

int flowmgr_user_add(struct flowmgr *fm, const char *convid,
		     const char *userid, const char *username);

void flowmgr_set_self_userid(struct flowmgr *fm, const char *userid);
const char *flowmgr_get_self_userid(struct flowmgr *fm);

void flowmgr_refresh_access_token(struct flowmgr *fm,
				  const char *token, const char *type);



/* Advice the flow manager *fm* to release all flows in *convid.*
 *
 * The flow manager will delete all flows it may have on the conversation.
 */
void flowmgr_release_flows(struct flowmgr *fm, const char *convid);

void flowmgr_set_active(struct flowmgr *fm, const char *convid, bool active);

void flowmgr_mcat_changed(struct flowmgr *fm,
			  const char *convid,
			  enum flowmgr_mcat cat);

int flowmgr_has_media(struct flowmgr *fm, const char *convid,
		      bool *has_media);

void flowmgr_network_changed(struct flowmgr *fm);

enum flowmgr_ausrc {
	FLOWMGR_AUSRC_INTMIC,
	FLOWMGR_AUSRC_EXTMIC,
	FLOWMGR_AUSRC_HEADSET,
	FLOWMGR_AUSRC_BT,	
	FLOWMGR_AUSRC_LINEIN,
	FLOWMGR_AUSRC_SPDIF	
};

enum flowmgr_auplay {
	FLOWMGR_AUPLAY_EARPIECE,
	FLOWMGR_AUPLAY_SPEAKER,
	FLOWMGR_AUPLAY_HEADSET,
	FLOWMGR_AUPLAY_BT,
	FLOWMGR_AUPLAY_LINEOUT,
	FLOWMGR_AUPLAY_SPDIF
};

int flowmgr_ausrc_changed(struct flowmgr *fm, enum flowmgr_ausrc asrc);
int flowmgr_auplay_changed(struct flowmgr *fm, enum flowmgr_auplay aplay);

int flowmgr_set_mute(struct flowmgr *fm, bool mute);
int flowmgr_get_mute(struct flowmgr *fm, bool *muted);

int flowmgr_sort_participants(struct list *partl);

int  flowmgr_start_mic_file_playout(const char fileNameUTF8[1024], int fs);
void flowmgr_stop_mic_file_playout(void);

void flowmgr_set_bitrate(int rate_bps);
void flowmgr_set_packet_size(int packet_size_ms);

void flowmgr_silencing(bool start);

struct flowmgr *flowmgr_rr_flowmgr(const struct rr_resp *rr);

const char **flowmgr_events(int *nevs);

int flowmgr_has_active(struct flowmgr *fm, bool *has_active);

int flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
			   const char *msg);

void flowmgr_enable_metrics(struct flowmgr *fm, bool metrics);

bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid);
bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid);
void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state);

void flowmgr_set_video_view(struct flowmgr *fm, const char *convid, const char *partid, void *view);


struct avs_vidframe;
void flowmgr_handle_frame(struct avs_vidframe *frame);

/* Marshalled functions */
int  marshal_flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
			   flowmgr_err_h *errh, void *arg);
int  marshal_flowmgr_start(void);

void marshal_flowmgr_free(struct flowmgr *fm);
void marshal_flowmgr_set_media_handlers(struct flowmgr *fm,
					     flowmgr_mcat_chg_h *cath,
					     flowmgr_volume_h *volh, void *arg);
void marshal_flowmgr_set_media_estab_handler(struct flowmgr *fm,
					     flowmgr_media_estab_h *mestabh,
					     void *arg);
void marshal_flowmgr_set_conf_pos_handler(struct flowmgr *fm,
					  flowmgr_conf_pos_h *conf_posh,
					  void *arg);
int  marshal_flowmgr_resp(struct flowmgr *fm, int status, const char *reason,
			  const char *ctype, const char *content, size_t clen,
			  struct rr_resp *rr);
int  marshal_flowmgr_process_event(bool *hp, struct flowmgr *fm,
				   const char *ctype, const char *content,
				   size_t clen);
int  marshal_flowmgr_acquire_flows(struct flowmgr *fm, const char *convid,
				   const char *sessid,
				   flowmgr_netq_h *qh, void *arg);
void marshal_flowmgr_release_flows(struct flowmgr *fm, const char *convid);
void marshal_flowmgr_set_active(struct flowmgr *fm, const char *convid,
				bool active);
void marshal_flowmgr_network_changed(struct flowmgr *fm);
void marshal_flowmgr_mcat_changed(struct flowmgr *fm, const char *convid,
				  enum flowmgr_mcat cat);
bool marshal_flowmgr_has_media(struct flowmgr *fm, const char *convid,
			       bool *has_media);
int  marshal_flowmgr_ausrc_changed(struct flowmgr *fm, enum flowmgr_ausrc asrc);
int  marshal_flowmgr_auplay_changed(struct flowmgr *fm,
				    enum flowmgr_auplay aplay);
int  marshal_flowmgr_set_mute(struct flowmgr *fm, bool mute);
int  marshal_flowmgr_get_mute(struct flowmgr *fm, bool *muted);
int  marshal_flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
				    const char *msg);
void marshal_flowmgr_enable_metrics(struct flowmgr *fm, bool metrics);
void marshal_flowmgr_set_sessid(struct flowmgr *fm, const char *convid,
				const char *sessid);
int  marshal_flowmgr_interruption(struct flowmgr *fm, const char *convid,
				  bool interrupted);
void marshal_flowmgr_user_add(struct flowmgr *fm, const char *convid,
			      const char *userid, const char *name);
void marshal_flowmgr_set_self_userid(struct flowmgr *fm,
				     const char *userid);
void marshal_flowmgr_refresh_access_token(struct flowmgr *fm,
					  const char *token,
					  const char *type);

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
#if 1
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

#else

#define FLOWMGR_MARSHAL_RET(t, r, f, ...) \
	if ((t) == pthread_self())	  \
		(r) = (f)(__VA_ARGS__);   \
	else {	                        \
		flowmgr_wakeup();       \
		re_thread_enter();	\
		(r) = (f)(__VA_ARGS__);	\
		re_thread_leave();      \
	}

#define FLOWMGR_MARSHAL_VOID(t, f, ...) \
	if ((t) == pthread_self())      \
		(f)(__VA_ARGS__);	\
	else {                          \
		flowmgr_wakeup();       \
		re_thread_enter();      \
		(f)(__VA_ARGS__);       \
		re_thread_leave();	\
	}
#endif


#endif /* #ifndef AVS_FLOWMGR_H */
