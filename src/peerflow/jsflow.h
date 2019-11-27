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

struct iflow;

int pc_platform_init(void);
void jsflow_destroy(void);

int jsflow_set_video_state(struct iflow *iflow,
			     enum icall_vstate vstate);

int jsflow_generate_offer(struct iflow *iflow,
			    char *sdp, size_t sz);
int jsflow_generate_answer(struct iflow *iflow,
			     char *sdp, size_t sz);

int jsflow_handle_offer(struct iflow *iflow,
			  const char *sdp_str);
int jsflow_handle_answer(struct iflow *iflow,
			   const char *sdp_str);

bool jsflow_has_video(const struct iflow *iflow);
bool jsflow_is_gathered(const struct iflow *iflow);

void jsflow_set_call_type(struct iflow *iflow,
			    enum icall_call_type call_type);

bool jsflow_get_audio_cbr(const struct iflow *iflow, bool local);
void jsflow_set_audio_cbr(struct iflow *iflow, bool enabled);

int jsflow_set_remote_userclientid(struct iflow *iflow,
				     const char *userid,
				     const char *clientid);

int jsflow_add_turnserver(struct iflow *iflow,
			    const char *url,
			    const char *username,
			    const char *password);

int jsflow_gather_all_turn(struct iflow *iflow, bool offer);

int jsflow_add_decoders_for_user(struct iflow *iflow,
				   const char *userid,
				   const char *clientid,
				   uint32_t ssrca,
				   uint32_t ssrcv);

void jsflow_stop_media(struct iflow *iflow);
void jsflow_close(struct iflow *iflow);

int jsflow_dce_send(struct iflow *flow,
		      const uint8_t *data,
		      size_t len);

void jsflow_set_stats(struct jsflow* flow, float downloss, float rtt);
int jsflow_get_stats(struct iflow *flow,
		     struct iflow_stats *stats);

int jsflow_debug(struct re_printf *pf, const struct iflow *flow);

#ifdef __cplusplus
}
#endif

