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
#include <pthread.h>
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "webrtc/transport.h"
#include "webrtc/modules/video_capture/include/video_capture.h"
#include "webrtc/modules/video_capture/include/video_capture_factory.h"
#include "webrtc/video_encoder.h"
#include "vie_render_view.h"
#include "vie.h"

class ViETransport : public webrtc::newapi::Transport
{
public:
	ViETransport(struct vie *vie_) : vie(vie_), active(true)
	{
	}

	virtual ~ViETransport()
	{
		active = false;
	}

	bool SendRtp(const uint8_t* packet, size_t length)
	{
		struct videnc_state *ves = vie->ves;
		int err = 0;
		
		//debug("vie: rtp[%d bytes]\n", (int)length);

		if (!active || !ves) {
			warning("cannot send RTP\n");
			return -1;
		}

		stats_rtp_add_packet(&vie->stats_tx, packet, length);

		if (ves->rtph) {
			err = ves->rtph(packet, length, ves->arg);
			if (err) {
				warning("vie: rtp send failed (%m)\n", err);
				return -1;
			}
			return length;
		}

		return -1;
	}

	bool SendRtcp(const uint8_t* packet, size_t length)
	{
		struct videnc_state *ves = vie->ves;
		int err = 0;

		//debug("vie: rtcp[%d bytes]\n", (int)length);

		if (!active || !ves)
			return -1;

		stats_rtcp_add_packet(&vie->stats_tx, packet, length);

		if (ves->rtcph) {
			err = ves->rtcph(packet, length, ves->arg);
			if (err) {
				warning("vie: rtcp send failed (%m)\n", err);
				return -1;
			}			
			return length;
		}

		return -1;
	}

private:
	struct vie *vie;
	bool active;
};

static void print_summary(struct vie *vie, int ch)
{
#if 0
	unsigned int key_frames=0;
	unsigned int delta_frames=0;
	int r;

	re_printf("~ ~ ~ ~ VIE Summary (channel %d) ~ ~ ~ ~\n", ch);

	re_printf("Send:\n");
	if (vid_eng.codec) {

		r = vid_eng.codec->GetSendCodecStastistics(ch,
							   key_frames,
							   delta_frames);
		if (r < 0) {
			re_printf("could not get send statistics\n");
		}
		else {
			re_printf("key_frames=%u, delta_frames=%u\n",
				  key_frames, delta_frames);
		}

		//re_printf("%H\n", transp_stats_print, &vie->stats_tx);
	}

#if 0
	re_printf("\nRecv:\n");
	if (vid_eng.codec) {

		r = vid_eng.codec->GetReceiveCodecStastistics(ch,
							      key_frames,
							      delta_frames);
		if (r < 0) {
			re_printf("could not get receive statistics\n");
		}
		else {
			re_printf("key_frames=%u, delta_frames=%u\n",
				  key_frames, delta_frames);
		}

		//re_printf("%H\n", transp_stats_print, &vie->stats_rx);
	}
#endif

	re_printf("Tx: %H\n", stats_print, &vie->stats_tx);
	re_printf("Rx: %H\n", stats_print, &vie->stats_rx);
#endif
	re_printf("~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ \n");
}


static void vie_destructor(void *arg)
{
	struct vie *vie = (struct vie *)arg;

	list_unlink(&vie->le);

	print_summary(vie, vie->ch);

	if (vie->ves) {
		vie_capture_stop(vie->ves);
	}

	if (vie->vds) {
		vie_render_stop(vie->vds);
	}

	if (vie->transport)
		delete vie->transport;
    
	if(vie->call)
		delete vie->call;
    
#if FORCE_VIDEO_RECORDING
    if(vie->rtpDump->IsActive()){
        vie->rtpDump->Stop();
    }
    webrtc::RtpDump::DestroyRtpDump(vie->rtpDump);
#endif
}

int vie_alloc(struct vie **viep, const struct vidcodec *vc, int pt)
{
	struct vie *vie;
	int err = 0;

	if (!viep || !vc)
		return EINVAL;

	debug("vie_alloc: codec=%s\n", vc->name);

	vie = (struct vie *)mem_zalloc(sizeof(*vie), vie_destructor);
	if (vie == NULL)
		return ENOMEM;

	vie->ch = -1;

	vie->transport = new ViETransport(vie);
	webrtc::Call::Config config(vie->transport);

/* TODO: set bitrate limits */
/*
all_config.bitrate_config.min_bitrate_bps =
   static_cast<int>(config_.min_bitrate_kbps) * 1000;
all_config.bitrate_config.start_bitrate_bps =
   static_cast<int>(config_.start_bitrate_kbps) * 1000;
all_config.bitrate_config.max_bitrate_bps =
   static_cast<int>(config_.max_bitrate_kbps) * 1000;
*/

	vie->call = webrtc::Call::Create(config);
	if (vie->call == NULL) {
		/* return here to avoid protected scope error */
		mem_deref((void *)vie);
		return ENOSYS;
	}

#if FORCE_VIDEO_RECORDING
	vie->rtpDump = webrtc::RtpDump::CreateRtpDump();
#endif

out:
	if (err)
		mem_deref((void *)vie);
	else
		*viep = vie;

	return err;
}

void vie_update_ssrc_array( uint32_t array[], size_t *count, uint32_t val)
{
	int i;
	for(i = 0; i < *count; i++){
		if(val == array[i]){
			break;
		}
	}
	if( i == *count){
		array[*count] = val;
		(*count)++;
	}
}

void vie_background(enum media_bg_state state)
{
	vie_capture_background(NULL, state);
}
