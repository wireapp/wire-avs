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
#include "avs_vie.h"

#include <sys/timeb.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/video_frame.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"

#include "capture_router.h"

#define PRINT_PERIODIC_FRAME_STATS 0

static struct vie_capture_router {
	webrtc::VideoCaptureInput *stream_input;
	lock *lock;
	bool buffer_rotate;
#if PRINT_PERIODIC_FRAME_STATS
	struct timeb fps_time;
	uint32_t fps_count;
#endif
} router = {
	.stream_input = NULL,
	.lock = NULL,
	.buffer_rotate = false,
};


int vie_capture_router_init(void)
{
	int err;

	router.stream_input = NULL;
	router.buffer_rotate = false;

	err = lock_alloc(&router.lock);
	if (err)
		return err;

#if PRINT_PERIODIC_FRAME_STATS
	ftime(&router.fps_time);
	router.fps_count = 0;
#endif

	return 0;
}


void vie_capture_router_deinit(void)
{
	router.stream_input = NULL;

	mem_deref(router.lock);
	router.lock = NULL;
}


void vie_capture_router_attach_stream(webrtc::VideoCaptureInput *stream_input,
	bool needs_buffer_rotation)
{
	lock_write_get(router.lock);
	
	debug("%s: attaching stream: %p rotate=%d\n",
	      __FUNCTION__, stream_input, needs_buffer_rotation);

	if (router.stream_input) {
		warning("%s: setting stream input when I already have one\n", __FUNCTION__);
	}

	router.stream_input = stream_input;
	router.buffer_rotate = needs_buffer_rotation;

	lock_rel(router.lock);
}

void vie_capture_router_detach_stream(webrtc::VideoCaptureInput *stream_input)
{
	lock_write_get(router.lock);
	if (router.stream_input == stream_input) {
		router.stream_input = NULL;
	}
	else {
		warning("%s: trying to detach stream input that isnt the current one\n", __FUNCTION__);
	}

	lock_rel(router.lock);
}

extern "C" {

void vie_capture_router_handle_frame(struct avs_vidframe *frame)
{
	webrtc::VideoFrame rtc_frame;
	webrtc::VideoType rtc_type;
	webrtc::VideoRotation rtc_rotation;
	bool needs_convert = false;
	int dw = frame->w;
	int dh = frame->h;

#if PRINT_PERIODIC_FRAME_STATS
	struct timeb now;
	ftime(&now);

	router.fps_count++;
	int msec = (now.time - router.fps_time.time) * 1000 +
		(now.millitm - router.fps_time.millitm) + 1;

	if (msec > 5000) {
		if (msec < 6000) {
			info("Capturer: res %dx%d fps: %0.2f\n", dw, dh,
				(float)router.fps_count * 1000.0f / msec); 
		}
		router.fps_count = 0;
		router.fps_time = now;
	}
#endif
	if (!router.stream_input)
		return;

	switch (frame->type) {
		case AVS_VIDFRAME_I420:
			rtc_type = webrtc::kI420;
			break;
			
		case AVS_VIDFRAME_NV12:
			rtc_type = webrtc::kNV12;
			needs_convert = true;
			break;
			
		case AVS_VIDFRAME_NV21:
			rtc_type = webrtc::kNV21;
			needs_convert = true;
			break;
	}

	switch (frame->rotation) {
		case 90:
			rtc_rotation = webrtc::kVideoRotation_90;
			if (router.buffer_rotate) {
				dw = frame->h;
				dh = frame->w;
				needs_convert = true;
			}
			break;

		case 180:
			rtc_rotation = webrtc::kVideoRotation_180;
			if (router.buffer_rotate) {
				needs_convert = true;
			}
			break;

		case 270:
			rtc_rotation = webrtc::kVideoRotation_270;
			if (router.buffer_rotate) {
				dw = frame->h;
				dh = frame->w;
				needs_convert = true;
			}
			break;

		default:
		case 0:
			rtc_rotation = webrtc::kVideoRotation_0;
			break;
	}

	//if (needs_convert) {
	if (true) { // For now we will always convert
		int err = 0;
		size_t dys = dw;
		size_t duvs = (dys + 1) / 2;
		webrtc::VideoRotation frot; /* Frame rotation */
		webrtc::VideoRotation crot; /* Convert rotation */
		
		frot = router.buffer_rotate ? webrtc::kVideoRotation_0
			: rtc_rotation;
		crot = router.buffer_rotate ? rtc_rotation
			: webrtc::kVideoRotation_0;
		
		rtc_frame.CreateEmptyFrame(dw, dh, dys, duvs, duvs);
		rtc_frame.set_rotation(frot);
		
		debug("%s: convert src %dx%d str %zu/%zu dst %dx%d "
		      "str %zu/%zu rot %d\n",
		      __FUNCTION__, frame->w, frame->h,
		      frame->ys, frame->us, dw, dh, dys,
		      duvs, crot);
		
		err = webrtc::ConvertToI420(rtc_type, frame->y, 0, 0,
					    frame->w, frame->h, 0, crot,
					    &rtc_frame);
		if (err < 0) {
			error("%s: failed to convert video frame (err=%d)\n",
			      __FUNCTION__, err);
			goto out;
		}
	}
	else {
		// Do fancy frame wrapping here
	}

	lock_read_get(router.lock);
	debug("handle_frame: stream_input=%p\n", router.stream_input);
	if (router.stream_input) {
		router.stream_input->IncomingCapturedFrame(rtc_frame);
	} 
	lock_rel(router.lock);

out:
	return;
}

};


