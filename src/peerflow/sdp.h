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

#ifdef __cplusplus
extern "C" {
#endif

int sdp_dup(struct sdp_session **sessp,
	    const char *sdp,
	    bool offer);

const char *sdp_sess2str(struct sdp_session *sess);	

const char *sdp_modify_offer(struct sdp_session *sess,
			     enum icall_conv_type conv_type,
			     bool audio_cbr);
const char *sdp_modify_answer(struct sdp_session *sess,
			      enum icall_conv_type conv_type,
			      bool audio_cbr);

void sdp_check_remote_acbr(const char *sdp,
			   bool offer,
			   peerflow_acbr_h *acbrh,
			   void *arg);

int sdp_strip_video(char **sdp, const char *osdp);
	
#ifdef __cplusplus
}
#endif
	
