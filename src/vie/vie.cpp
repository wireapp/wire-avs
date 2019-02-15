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
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/modules/video_coding/include/video_coding.h"
#include "vie.h"


#if USE_RTX
#define NUM_CODECS 2
#else
#define NUM_CODECS 1
#endif



/* global data */
struct vid_eng vid_eng;


struct media_ctx {
	struct vie *vie;
};


class ViETransport;


static struct vidcodec vie_vidcodecv[NUM_CODECS] = {
	{
		.le = LE_INIT,
		.pt = "100",
		.name = "VP8",
		.variant = NULL,
		.fmtp = NULL,

		.enc_alloch   = vie_enc_alloc,
		.enc_starth   = vie_capture_start,
		.enc_stoph    = vie_capture_stop,
		.enc_holdh    = vie_capture_hold,
		.enc_bwalloch = vie_capture_getbw,

		.dec_alloch   = vie_dec_alloc,
		.dec_starth   = vie_render_start,
		.dec_stoph    = vie_render_stop,
		.dec_holdh    = vie_render_hold,
		.dec_rtph     = vie_dec_rtp_handler,
		.dec_rtcph    = vie_dec_rtcp_handler,
		.dec_debugh   = NULL,
		.dec_bwalloch = vie_dec_getbw,

		.fmtp_ench    = vie_fmtp_enc,
	},
#if USE_RTX
	{
		.le = LE_INIT,
		.pt = "96",
		.name = "rtx",
		.variant = NULL,
		.fmtp = NULL,

		.enc_alloch   = NULL,
		.enc_starth   = NULL,
		.enc_stoph    = NULL,
		.enc_holdh    = NULL,
		.enc_bwalloch = NULL,

		.dec_alloch   = NULL,
		.dec_starth   = NULL,
		.dec_stoph    = NULL,
		.dec_holdh    = NULL,
		.dec_rtph     = vie_dec_rtp_handler,
		.dec_rtcph    = vie_dec_rtcp_handler,
		.dec_debugh   = NULL,
		.dec_bwalloch = NULL,

		.codec_ref    = &vie_vidcodecv[0],

		.fmtp_ench    = vie_rtx_fmtp_enc,
	}
#endif
	
};


void vie_close(void)
{
	info("%s:\n", __FUNCTION__);

	list_flush(&vid_eng.chl);
	
	for (int i = 0; i < NUM_CODECS; ++i) {
		struct vidcodec *vc = &vie_vidcodecv[i];

		vidcodec_unregister(vc);
	}
	vie_capture_router_deinit();

	if (vid_eng.codecs) {
		delete [] vid_eng.codecs;
		vid_eng.codecs = NULL;
	}
}


const webrtc::VideoCodec *vie_find_codec(const struct vidcodec *vc)
{
	bool found = false;
	const webrtc::VideoCodec *c;
	size_t i;

	for (i = 0; i < vid_eng.ncodecs && !found; ++i) {
		c = &vid_eng.codecs[i];
		found = streq(c->plName, vc->name);
	}

	return found ? c : NULL;
}


int vie_init(struct list *vidcodecl)
{
	size_t i;
	int err = 0;

	info("%s: invoked\n", __FUNCTION__);

#if 0
	webrtc::Trace::CreateTrace();
	webrtc::Trace::SetTraceCallback(&vid_eng.log_handler);
	webrtc::Trace::set_level_filter(
					webrtc::kTraceWarning |
					webrtc::kTraceError |
					webrtc::kTraceCritical
					| webrtc::kTraceAll
					);

#endif

	vid_eng.renderer_reset = false;
	vid_eng.capture_reset = false;
	
	/* list all supported codecs */

	vid_eng.ncodecs = 1;
	vid_eng.codecs = new webrtc::VideoCodec[vid_eng.ncodecs];
	for (i = 0; i < vid_eng.ncodecs; i++) {
		webrtc::VideoCodec c;


    		webrtc::VideoCodingModule::Codec(webrtc::kVideoCodecVP8, &c);
		debug("codec name=%s pt=%d size=%dx%d startB=%d"
		      " maxB=%d minB=%d"
		      " targetB=%d\n",
		      c.plName, c.plType, c.width, c.height,
		      c.startBitrate, c.maxBitrate, c.minBitrate,
		      c.targetBitrate);

		vid_eng.codecs[i] = c;
	}
	for (int i = 0; i < NUM_CODECS; ++i) {
		const webrtc::VideoCodec *c;
		struct vidcodec *vc = &vie_vidcodecv[i];

		if (!vc->name)
			continue;

		c = vie_find_codec(vc);

		vc->data = (void *)c;

		vidcodec_register(vidcodecl, vc);

		info("%s: registering %s -- %p\n", __FUNCTION__, vc->name, c);
	}

	list_init(&vid_eng.chl);

	err = vie_capture_router_init();
	if (err)
		goto out;

 out:
	if (err)
		vie_close();

	return err;
}

void vie_set_video_handlers(vie_video_state_change_h *state_change_h,
			    vie_render_frame_h *render_frame_h,
			    vie_video_size_h *size_h,
			    void *arg)
{
	vid_eng.state_change_h = state_change_h;
	vid_eng.render_frame_h = render_frame_h;
	vid_eng.size_h = size_h;
	vid_eng.cb_arg = arg;
}

