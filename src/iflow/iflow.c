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

#include <re.h>
#include <avs.h>

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
			 iflow_sync_decoders	        *sync_decoders,
			 iflow_set_keystore		*set_keystore,
			 iflow_dce_send			*dce_send,
			 iflow_stop_media		*stop_media,
			 iflow_close			*close,
			 iflow_get_stats		*get_stats,
			 iflow_debug			*debug)
{
	if (!iflow) {
		warning("iflow_set_functions called on NULL iflow\n");
		return;
	}
	iflow->set_video_state		= set_video_state;
	iflow->generate_offer		= generate_offer;
	iflow->generate_answer		= generate_answer;
	iflow->handle_offer		= handle_offer;
	iflow->handle_answer		= handle_answer;
	iflow->has_video		= has_video;
	iflow->is_gathered		= is_gathered;
	iflow->enable_privacy		= enable_privacy;
	iflow->set_call_type		= set_call_type;
	iflow->get_audio_cbr		= get_audio_cbr;
	iflow->set_audio_cbr		= set_audio_cbr;
	iflow->set_remote_userclientid	= set_remote_userclientid;
	iflow->add_turnserver		= add_turnserver;
	iflow->gather_all_turn		= gather_all_turn;
	iflow->add_decoders_for_user	= add_decoders_for_user;
	iflow->remove_decoders_for_user	= remove_decoders_for_user;
	iflow->sync_decoders            = sync_decoders;
	iflow->set_keystore		= set_keystore;
	iflow->dce_send			= dce_send;
	iflow->stop_media		= stop_media;
	iflow->close			= close;
	iflow->get_stats		= get_stats;
	iflow->debug			= debug;
}

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
			 iflow_acbr_detect_h		*acbr_detecth,
			 iflow_norelay_h		*norelayh,
			 void				*arg)
{
	if (!iflow) {
		warning("iflow_set_functions called on NULL iflow\n");
		return;
	}
	iflow->estabh			= estabh;
	iflow->closeh			= closeh;
	iflow->stoppedh			= stoppedh;
	iflow->rtp_stateh		= rtp_stateh;
	iflow->restarth			= restarth;
	iflow->gatherh			= gatherh;
	iflow->dce_estabh		= dce_estabh;
	iflow->dce_recvh		= dce_recvh;
	iflow->acbr_detecth		= acbr_detecth;
	iflow->norelayh                 = norelayh;
	iflow->arg			= arg;
}

static struct {
	iflow_render_frame_h *render_frameh;
	iflow_video_size_h *sizeh;
	void *arg;
} video = {
	NULL,
	NULL,
	NULL
};


void iflow_set_video_handlers(iflow_render_frame_h *render_frameh,
			      iflow_video_size_h *sizeh,
			      void *arg)
{
	video.render_frameh = render_frameh;
	video.sizeh = sizeh;
	video.arg = arg;
}


void iflow_video_sizeh(int w,
		       int h,
		       const char *userid,
		       const char *clientid)
{
	if (video.sizeh) {
		video.sizeh(w, h, userid, clientid, video.arg);
	}
}


int iflow_render_frameh(struct avs_vidframe *frame,
			const char *userid,
			const char *clientid)
{
	if (video.render_frameh) {
		return video.render_frameh(frame, userid, clientid, video.arg);
	}
	return EINVAL;
}

struct {
	iflow_allocf	*alloc;
	iflow_destroyf	*destroy;
	iflow_set_mutef	*set_mute;
	iflow_get_mutef	*get_mute;
} statics = {
#ifdef __EMSCRIPTEN__
	jsflow_alloc,
#else
	NULL,
#endif
	NULL,
	NULL,
	NULL
};


void iflow_destroy(void)
{
	if (statics.destroy) {
		statics.destroy();
	}
	statics.destroy = NULL;
	statics.set_mute = NULL;
	statics.get_mute = NULL;
}


void iflow_set_mute(bool mute)
{
	if (statics.set_mute) {
		statics.set_mute(mute);
	}
}


bool iflow_get_mute(void)
{
	if (statics.get_mute) {
		return statics.get_mute();
	}
	return false;
}


void iflow_register_statics(iflow_destroyf *destroy,
			    iflow_set_mutef *set_mute,
			    iflow_get_mutef *get_mute)
{
	iflow_destroy();
	statics.destroy = destroy;
	statics.set_mute = set_mute;
	statics.get_mute = get_mute;
}


void iflow_set_alloc(iflow_allocf *allocf)
{
	statics.alloc = allocf;
}


int iflow_alloc(struct iflow		**flowp,
		const char		*convid,
		const char		*userid_self,
		enum icall_conv_type	conv_type,
		enum icall_call_type	call_type,
		enum icall_vstate	vstate,
		void			*extarg)
{
	if (!statics.alloc) {
		error("iflow: statics.alloc is NULL\n");
		return ENOMEM;
	}
	return statics.alloc(flowp,
			     convid,
			     userid_self,
			     conv_type,
			     call_type,
			     vstate,
			     extarg);
}

