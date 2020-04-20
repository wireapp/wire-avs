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
void peerflow_destroy(void);

int peerflow_set_video_state(struct iflow *iflow,
			     enum icall_vstate vstate);

int peerflow_generate_offer(struct iflow *iflow,
			    char *sdp, size_t sz);
int peerflow_generate_answer(struct iflow *iflow,
			     char *sdp, size_t sz);

int peerflow_handle_offer(struct iflow *iflow,
			  const char *sdp_str);
int peerflow_handle_answer(struct iflow *iflow,
			   const char *sdp_str);

bool peerflow_has_video(const struct iflow *iflow);
bool peerflow_is_gathered(const struct iflow *iflow);

void peerflow_set_call_type(struct iflow *iflow,
			    enum icall_call_type call_type);

bool peerflow_get_audio_cbr(const struct iflow *iflow, bool local);
void peerflow_set_audio_cbr(struct iflow *iflow, bool enabled);

int peerflow_set_remote_userclientid(struct iflow *iflow,
				     const char *userid,
				     const char *clientid);

int peerflow_add_turnserver(struct iflow *iflow,
			    const char *url,
			    const char *username,
			    const char *password);

int peerflow_gather_all_turn(struct iflow *iflow, bool offer);

int peerflow_add_decoders_for_user(struct iflow *iflow,
				   const char *userid,
				   const char *clientid,
				   uint32_t ssrca,
				   uint32_t ssrcv);

int peerflow_set_keystore(struct iflow *iflow,
			  struct keystore *keystore);

void peerflow_stop_media(struct iflow *iflow);
void peerflow_close(struct iflow *iflow);

int peerflow_dce_send(struct iflow *flow,
		      const uint8_t *data,
		      size_t len);

bool peerflow_is_dcopen(struct peerflow *pf);

int peerflow_debug(struct re_printf *pf, const struct iflow *flow);

int peerflow_get_stats(struct iflow *flow,
		       struct iflow_stats *stats);
void peerflow_set_stats(struct peerflow* pf,
			uint32_t apkts_recv,
			uint32_t vpkts_recv,
			uint32_t apkts_sent,
			uint32_t vpkts_sent,
			float downloss,
			float rtt);

#ifdef __cplusplus
}
#endif

