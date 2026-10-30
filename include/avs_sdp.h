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

/* Opus ptime (ms) and maxaveragebitrate (bps) for the SDPs negotiated from
 * now on (default 40 and 24000): put in the local offers and answers (what
 * the remote side should send) and in the remote ones before they are
 * applied (what our encoder sends); 0 leaves one out.
 */
void sdp_set_audio_params(int ptime, int max_bitrate);

/* Protection against packet loss for the calls set up from now on; 0 turns
 * one off (all 0 is the plain WebRTC behaviour):
 *  red          opus RED (RFC 2198) with this many earlier frames in each
 *               packet (1..9, default 1); RED is negotiated (kept in the
 *               SDPs) only when set
 *  fec_loss_pct lowest packet loss (%) the opus encoder assumes, so that its
 *               in-band FEC is on from the start (1..100, default 15)
 *  max_bitrate  bandwidth estimate cap (bps) while there is no video; limits
 *               the probing padding (default 100000)
 *  jb_min_ms    jitter buffer minimum delay (ms) of the received audio
 *               (default 60)
 */
struct sdp_loss_params {
	int red;
	int fec_loss_pct;
	int max_bitrate;
	int jb_min_ms;
};

void sdp_set_loss_params(const struct sdp_loss_params *lp);
void sdp_get_loss_params(struct sdp_loss_params *lp);

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

int sdp_munge(char **sdp_out, const char *sdp,
	      enum icall_conv_type conv_type, bool offer);
int sdp_munge_audio(char **sdp_out, const char *sdp,
		    enum icall_conv_type conv_type, bool offer,
		    bool strip_video, bool munge);

/* bundle */

typedef int (bundle_flow_update_h)(struct iflow *flow, const char *sdp);

int bundle_update(struct iflow *flow,
		  enum icall_conv_type conv_type,
		  bool include_audio,
		  const char *remote_sdp,
		  struct list *conf_membl,
		  bundle_flow_update_h *flow_updateh);

