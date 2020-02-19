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

/*
 * Mediaflow interface
 */

struct iflow;
struct avs_vidframe;

struct iflow_stats {
	uint32_t apkts_recv;
	uint32_t apkts_sent;
	uint32_t vpkts_recv;
	uint32_t vpkts_sent;
	float dloss;
	float rtt;
};

/* Calls into iflow */
typedef int  (iflow_set_video_state)(struct iflow *flow, enum icall_vstate vstate);

typedef int  (iflow_generate_offer)(struct iflow *flow, char *sdp, size_t sz);
typedef int  (iflow_generate_answer)(struct iflow *flow, char *sdp, size_t sz);
typedef int  (iflow_handle_offer)(struct iflow *flow, const char *sdp);
typedef int  (iflow_handle_answer)(struct iflow *flow, const char *sdp);

typedef bool (iflow_has_video)(const struct iflow *flow);
typedef bool (iflow_is_gathered)(const struct iflow *flow);

typedef void (iflow_set_call_type)(struct iflow *flow, enum icall_call_type call_type);
typedef void (iflow_enable_privacy)(struct iflow *flow, bool enabled);

typedef bool (iflow_get_audio_cbr)(const struct iflow *flow, bool local);
typedef void (iflow_set_audio_cbr)(struct iflow *flow, bool enabled);
typedef int  (iflow_set_remote_userclientid)(struct iflow *flow,
					     const char *userid,
					     const char *clientid);

typedef int (iflow_add_turnserver)(struct iflow *flow,
				   const char *url,
				   const char *username,
				   const char *password);

typedef int  (iflow_gather_all_turn)(struct iflow *flow, bool offer);

typedef int  (iflow_add_decoders_for_user)(struct iflow *flow,
					   const char *userid,
					   const char *clientid,
					   uint32_t ssrca,
					   uint32_t ssrcv);
typedef int  (iflow_remove_decoders_for_user)(struct iflow *flow,
					      const char *userid,
					      const char *clientid,
					      uint32_t ssrca,
					      uint32_t ssrcv);

typedef int  (iflow_set_e2ee_key)(struct iflow *flow,
				  uint32_t idx,
				  uint8_t e2ee_key[E2EE_SESSIONKEY_SIZE]);

typedef void (iflow_stop_media)(struct iflow *flow);
typedef void (iflow_close)(struct iflow *flow);

typedef int (iflow_dce_send)(struct iflow *flow,
			     const uint8_t *data,
			     size_t len);

typedef int  (iflow_get_stats)(struct iflow *flow,
			       struct iflow_stats *stats);

typedef int  (iflow_debug)(struct re_printf *pf, const struct iflow *flow);

/* Static functions */
typedef int (iflow_allocf)(struct iflow		**flowp,
			   const char		*convid,
			   enum icall_conv_type	conv_type,
			   enum icall_call_type	call_type,
			   enum icall_vstate	vstate,
			   void			*extarg);

typedef void (iflow_destroyf)(void);

typedef void (iflow_set_mutef)(bool muted);
typedef bool (iflow_get_mutef)(void);

/* Callbacks from iflow */
typedef void (iflow_estab_h)(struct iflow *flow,
			     const char *crypto,
			     const char *codec,
			     void *arg);
typedef void (iflow_close_h)(struct iflow *flow,
			     int err,
			     void *arg);
typedef void (iflow_stopped_h)(struct iflow *flow,
			       void *arg);

typedef void (iflow_rtp_state_h)(struct iflow *flow,
				 bool started,
			         bool video_started,
			         void *arg);

typedef void (iflow_restart_h)(struct iflow *flow,
			       bool force_cbr,
			       void *arg);
typedef void (iflow_gather_h)(struct iflow *flow, void *arg);

typedef void (iflow_dce_estab_h)(struct iflow *flow, void *arg);
typedef void (iflow_dce_recv_h)(struct iflow *flow,
				const uint8_t *data,
				size_t len,
				void *arg);
typedef void (iflow_dce_close_h)(struct iflow *flow, void *arg);

/* Static callbacks */
typedef void (iflow_video_size_h)(int w,
				  int h,
				  const char *userid,
				  void *arg);
typedef int (iflow_render_frame_h)(struct avs_vidframe *frame,
				   const char *userid,
				   const char *clientid,
				   void *arg);

struct iflow {
	iflow_set_video_state		*set_video_state;
	iflow_generate_offer		*generate_offer;
	iflow_generate_answer		*generate_answer;
	iflow_handle_offer		*handle_offer;
	iflow_handle_answer		*handle_answer;
	iflow_has_video			*has_video;
	iflow_is_gathered		*is_gathered;
	iflow_enable_privacy		*enable_privacy;
	iflow_set_call_type		*set_call_type;
	iflow_get_audio_cbr		*get_audio_cbr;
	iflow_set_audio_cbr		*set_audio_cbr;
	iflow_set_remote_userclientid	*set_remote_userclientid;
	iflow_add_turnserver		*add_turnserver;
	iflow_gather_all_turn		*gather_all_turn;
	iflow_add_decoders_for_user	*add_decoders_for_user;
	iflow_remove_decoders_for_user	*remove_decoders_for_user;
	iflow_set_e2ee_key		*set_e2ee_key;
	iflow_dce_send			*dce_send;
	iflow_stop_media		*stop_media;
	iflow_close			*close;
	iflow_get_stats			*get_stats;
	iflow_debug			*debug;

	iflow_estab_h			*estabh;
	iflow_close_h			*closeh;
	iflow_stopped_h			*stoppedh;
	iflow_rtp_state_h		*rtp_stateh;
	iflow_restart_h			*restarth;
	iflow_gather_h			*gatherh;
	iflow_dce_estab_h		*dce_estabh;
	iflow_dce_recv_h		*dce_recvh;
	iflow_dce_close_h		*dce_closeh;

	void				*arg;
}__attribute__ ((aligned (8)));

#define IFLOW_CALL(iflow, fn, ...) if(iflow && (iflow)->fn){iflow->fn(iflow, ##__VA_ARGS__);}
#define IFLOW_CALLE(iflow, fn, ...) (iflow && (iflow)->fn) ? iflow->fn(iflow, ##__VA_ARGS__) : 0

#define IFLOW_CALL_CB(iflow, fn, ...) if(iflow.fn){iflow.fn(&iflow, ##__VA_ARGS__);}
#define IFLOW_CALL_CBE(iflow, fn, ...) iflow.fn ? iflow.fn(&iflow, ##__VA_ARGS__) : 0


void iflow_set_functions(struct iflow *iflow,
			 iflow_set_video_state		*set_video_state,
			 iflow_generate_offer		*generate_offer,
			 iflow_generate_answer		*generate_answer,
			 iflow_handle_offer		*handle_offer,
			 iflow_handle_answer		*handle_answer,
			 iflow_has_video		*has_video,
			 iflow_is_gathered		*is_gathered,
			 iflow_enable_privacy		*enable_privacy,
			 iflow_set_call_type		*set_call_type,
			 iflow_get_audio_cbr		*get_audio_cbr,
			 iflow_set_audio_cbr		*set_audio_cbr,
			 iflow_set_remote_userclientid	*set_remote_userclientid,
			 iflow_add_turnserver		*add_turnserver,
			 iflow_gather_all_turn		*gather_all_turn,
			 iflow_add_decoders_for_user	*add_decoders_for_user,
			 iflow_remove_decoders_for_user	*remove_decoders_for_user,
			 iflow_set_e2ee_key		*set_e2ee_key,
			 iflow_dce_send			*dce_send,
			 iflow_stop_media		*stop_media,
			 iflow_close			*close,
			 iflow_get_stats		*get_stats,
			 iflow_debug			*debug);

void iflow_set_callbacks(struct iflow *iflow,
			 iflow_estab_h			*estabh,
			 iflow_close_h			*closeh,
			 iflow_stopped_h		*stoppedh,
			 iflow_rtp_state_h		*rtp_stateh,
			 iflow_restart_h		*restarth,
			 iflow_gather_h			*gatherh,
			 iflow_dce_estab_h		*dce_estabh,
			 iflow_dce_recv_h		*dce_recvh,
			 iflow_dce_close_h		*dce_closeh,
			 void				*arg);

void iflow_set_alloc(iflow_allocf *allocf);

void iflow_register_statics(iflow_destroyf *destroy,
			    iflow_set_mutef *set_mute,
			    iflow_get_mutef *get_mute);

int iflow_alloc(struct iflow		**flowp,
		const char		*convid,
		enum icall_conv_type	conv_type,
		enum icall_call_type	call_type,
		enum icall_vstate	vstate,
		void			*extarg);

void iflow_destroy(void);

void iflow_set_mute(bool mute);
bool iflow_get_mute(void);

void iflow_set_video_handlers(iflow_render_frame_h *render_frame_h,
			      iflow_video_size_h *size_h,
			      void *arg);

void iflow_video_sizeh(int w,
		       int h,
		       const char *userid);
int iflow_render_frameh(struct avs_vidframe *frame,
			const char *userid,
			const char *clientid);

