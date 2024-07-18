/*
* Wire
* Copyright (C) 2019 Wire Swiss GmbH
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

int sdp_dup(struct sdp_session **sessp,
	    enum icall_conv_type conv_type,
	    const char *sdp,
	    bool offer);

const char *sdp_sess2str(struct sdp_session *sess);	

const char *sdp_modify_offer(struct sdp_session *sess,
			     enum icall_conv_type conv_type,
			     bool screenshare,
			     bool audio_cbr);
const char *sdp_modify_answer(struct sdp_session *sess,
			      enum icall_conv_type conv_type,
			      bool screenshare,
			      bool audio_cbr);

int sdp_strip_video(char **sdp, enum icall_conv_type conv_type,
		    const char *osdp);
	
void sdp_check(const char *sdp,
	       bool local,
	       bool offer,
	       peerflow_acbr_h *acbrh,
	       peerflow_norelay_h *norelayh,
	       peerflow_tool_h *toolh,
	       void *arg);

void sdp_safe_session_set_lattr(struct sdp_session *sess, bool replace,
				const char *name, const char *value);

void sdp_safe_media_set_lattr(struct sdp_media *sdpm, bool replace,
			      const char *name, const char *value);

/* bundle */

typedef int (bundle_flow_update_h)(struct iflow *flow, const char *sdp);

int bundle_update(struct iflow *flow,
		  enum icall_conv_type conv_type,
		  bool include_audio,
		  const char *remote_sdp,
		  struct list *conf_membl,
		  bundle_flow_update_h *flow_updateh);

