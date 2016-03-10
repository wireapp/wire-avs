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

struct flowmgr;
struct rr_resp;


void flowmgr_set_cert(const char *cert, size_t cert_len);
int  flowmgr_init(const char *msysname, const char *log_url);
int  flowmgr_start(void);
void flowmgr_close(void);
int  flowmgr_is_ready(struct flowmgr *fm, bool *is_ready);
void flowmgr_enable_dualstack(bool enable);
void flowmgr_bind_interface(const char *ifname);

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

typedef void (flowmgr_log_append_h)(const char *msg, void *arg);
typedef void (flowmgr_log_upload_h)(void *arg);

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
 * Callbacks used to inform user that we need views for video rendering.
 *
 * This is not part of the public flow manager interface. Native bindings
 * need to provide this handler and forward a notification to the application.
 *
 * @param view     View to be released.
 * @param partid   Participant id for the view (in future multiparty video).
 * @param arg      The handler argument passed to flowmgr_alloc().
 */
typedef void (flowmgr_create_preview_h)(void *arg);
typedef void (flowmgr_release_preview_h)(void *view, void *arg);
typedef void (flowmgr_create_view_h)(const char *convid, const char *partid,
				     void *arg);
typedef void (flowmgr_release_view_h)(const char *convid, const char *partid,
				      void *view, void *arg);

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

void flowmgr_set_log_handlers(struct flowmgr *fm,
			      flowmgr_log_append_h *appendh,
			      flowmgr_log_upload_h *uploadh,
			      void *arg);

void flowmgr_set_media_estab_handler(struct flowmgr *fm,
				     flowmgr_media_estab_h *mestabh,
				     void *arg);

void flowmgr_set_conf_pos_handler(struct flowmgr *fm,
				  flowmgr_conf_pos_h *conf_posh,
				  void *arg);

void flowmgr_set_username_handler(struct flowmgr *fm,
				  flowmgr_username_h *usernameh, void *arg);

void flowmgr_set_video_handlers(struct flowmgr *fm, 
				flowmgr_create_preview_h *create_previewh,
				flowmgr_release_preview_h *release_previewh,
				flowmgr_create_view_h *create_viewh,
				flowmgr_release_view_h *release_viewh,
				void *arg);

void flowmgr_set_sessid(struct flowmgr *fm, const char *convid,
			const char *sessid);

int  flowmgr_interruption(struct flowmgr *fm, const char *convid,
			  bool interrupted);


void flowmgr_background(struct flowmgr *fm, enum media_bg_state bgst);

struct call *flowmgr_call(struct flowmgr *fm, const char *convid);

int  call_count_flows(struct call *call);
uint32_t flowmgr_call_count_active_flows(const struct call *call);
const char *flowmgr_call_sessid(const struct call *call);


/**
 * Free the flow manager.
 *
 * XXX Since the public flow manager API is always handed out through
 *     native implementation, this function can be dropped and replaced
 *     by a regular mem_deref().
 */
struct flowmgr *flowmgr_free(struct flowmgr *fm);


void flowmgr_enable_trace(struct flowmgr *fm, int trace);

const char *flowmgr_get_username(struct flowmgr *fm, const char *userid);


/* Call events we know of */
enum flowmgr_event {
	FLOWMGR_EVENT_FLOW_ADD,
	FLOWMGR_EVENT_FLOW_DEL,
	FLOWMGR_EVENT_FLOW_ACT,
	FLOWMGR_EVENT_CAND_ADD,
	FLOWMGR_EVENT_CAND_UPD,
	FLOWMGR_EVENT_SDP,
	FLOWMGR_EVENT_MAX    /* Keep last for count */
};

typedef void (flowmgr_event_h)(enum flowmgr_event ev,
			       const char *convid, const char *flowid,
			       void *jobj,
			       void *arg);

int flowmgr_set_event_handler(struct flowmgr *fm, flowmgr_event_h *evh,
			      void *arg);


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


/*
 * Call config
 */

struct call_config {
	struct zapi_ice_server iceserverv[4];
	size_t iceserverc;
};

typedef void (call_config_h)(struct call_config *cfg, void *arg);

int  flowmgr_config_start(struct flowmgr *fm);
int  flowmgr_config_starth(struct flowmgr *fm,
			   call_config_h *config, void *arg);
void flowmgr_config_stop(struct flowmgr *fm);
struct zapi_ice_server *flowmgr_config_iceservers(struct flowmgr *fm,
						  size_t *count);


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
size_t flowmgr_users_count(const struct flowmgr *fm, const char *convid);

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

int flowmgr_debug(struct re_printf *pf, const struct flowmgr *fm);

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

int  flowmgr_start_speak_file_record(const char fileNameUTF8[1024]);
void flowmgr_stop_speak_file_record(void);

int flowmgr_start_preproc_recording(const char fileNameUTF8[1024]);
void flowmgr_stop_preproc_recording(void);

int flowmgr_start_packet_recording(const char fileNameUTF8[1024]);
void flowmgr_stop_packet_recording(void);

void flowmgr_enable_fec(bool enable);
void flowmgr_enable_aec(bool enable);
void flowmgr_enable_rcv_ns(bool enable);

void flowmgr_set_bitrate(int rate_bps);
void flowmgr_set_packet_size(int packet_size_ms);

typedef void (flowmgr_vm_play_status_h)(bool is_playing, unsigned int cur_time_ms, unsigned int file_length_ms, void *arg);

int flowmgr_vm_start_record(struct flowmgr *fm, const char fileNameUTF8[1024]);
int flowmgr_vm_stop_record(struct flowmgr *fm);
int flowmgr_vm_get_length(struct flowmgr *fm, const char fileNameUTF8[1024], int* length_ms);
int flowmgr_vm_start_play(struct flowmgr *fm, const char fileNameUTF8[1024], int  start_time_ms, flowmgr_vm_play_status_h *handler, void *arg);
int flowmgr_vm_stop_play(struct flowmgr *fm);

void flowmgr_silencing(bool start);

struct flowmgr *flowmgr_rr_flowmgr(const struct rr_resp *rr);

const char **flowmgr_events(int *nevs);

int flowmgr_has_active(struct flowmgr *fm, bool *has_active);

int flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
			   const char *msg);

void flowmgr_enable_metrics(struct flowmgr *fm, bool metrics);
void flowmgr_enable_logging(struct flowmgr *fm, bool logging);

int flowmgr_send_metrics(struct flowmgr *fm, const char *convid,
			 const char *path);

#if HAVE_VIDEO

enum flowmgr_video_send_state {
	FLOWMGR_VIDEO_SEND_NONE = 0,
	FLOWMGR_VIDEO_PREVIEW,
	FLOWMGR_VIDEO_SEND
};

bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid);
bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid);
void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state);

void flowmgr_set_video_preview(struct flowmgr *fm, const char *convid, void *view);
void flowmgr_set_video_view(struct flowmgr *fm, const char *convid, const char *partid, void *view);
void flowmgr_get_video_capture_devices(struct flowmgr *fm, struct list **device_list);
void flowmgr_set_video_capture_device(struct flowmgr *fm, const char *convid, const char *devid);
#endif

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
void marshal_flowmgr_set_log_handlers(struct flowmgr *fm,
				      flowmgr_log_append_h *appendh,
				      flowmgr_log_upload_h *uploadh,
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
void marshal_flowmgr_enable_logging(struct flowmgr *fm, bool logging);
void marshal_flowmgr_set_sessid(struct flowmgr *fm, const char *convid,
				const char *sessid);
int  marshal_flowmgr_interruption(struct flowmgr *fm, const char *convid,
				  bool interrupted);
void marshal_flowmgr_background(struct flowmgr *fm, enum media_bg_state bgst);
void marshal_flowmgr_user_add(struct flowmgr *fm, const char *convid,
			      const char *userid, const char *name);
void marshal_flowmgr_set_self_userid(struct flowmgr *fm,
				     const char *userid);
void marshal_flowmgr_refresh_access_token(struct flowmgr *fm,
					  const char *token,
					  const char *type);


int marshal_flowmgr_vm_start_record(struct flowmgr *fm, const char fileNameUTF8[1024]);
int marshal_flowmgr_vm_stop_record(struct flowmgr *fm);
int marshal_flowmgr_vm_start_play(struct flowmgr *fm, const char fileNameUTF8[1024], int  start_time_ms, flowmgr_vm_play_status_h *handler, void *arg);
int marshal_flowmgr_vm_stop_play(struct flowmgr *fm);



#if HAVE_VIDEO

void marshal_flowmgr_set_video_handlers(struct flowmgr *fm, 
			flowmgr_create_preview_h *create_preview_handler,
			flowmgr_release_preview_h *release_preview_handler,
			flowmgr_create_view_h *create_view_handler,
			flowmgr_release_view_h *release_view_handler,
			void *arg);

int marshal_flowmgr_can_send_video(struct flowmgr *fm, const char *convid);
int marshal_flowmgr_is_sending_video(struct flowmgr *fm,
				     const char *convid, const char *partid);
void marshal_flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state);

void marshal_flowmgr_set_video_preview(struct flowmgr *fm, const char *convid, void *view);
void marshal_flowmgr_set_video_view(struct flowmgr *fm, const char *convid, const char *partid, void *view);
void marshal_flowmgr_get_video_capture_devices(struct flowmgr *fm, struct list **device_list);
void marshal_flowmgr_set_video_capture_device(struct flowmgr *fm, const char *convid, const char *devid);
#endif

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
