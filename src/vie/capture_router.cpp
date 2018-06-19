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

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/video_frame.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"

#include "capture_router.h"

#define STATS_DELAY 20000

struct vie_enc_stream {
	struct le le;
	webrtc::VideoCaptureInput *input;
};

static struct vie_capture_router {
	struct list streaml;
	struct lock *lock;
	bool buffer_rotate;
	uint64_t ts_fps;
	uint32_t fps_count;
} router = {
	.lock = NULL,
	.buffer_rotate = false,
};

static void stream_destructor(void *arg)
{
	struct vie_enc_stream *es = (struct vie_enc_stream*)arg;

	list_unlink(&es->le);
}

int vie_capture_router_init(void)
{
	int err;

	router.buffer_rotate = false;

	err = lock_alloc(&router.lock);
	if (err)
		return err;

	router.ts_fps = tmr_jiffies();
	router.fps_count = 0;

	list_init(&router.streaml);

	return 0;
}


void vie_capture_router_deinit(void)
{
	lock_write_get(router.lock);
	list_flush(&router.streaml);
	lock_rel(router.lock);

	mem_deref(router.lock);
	router.lock = NULL;
}


void vie_capture_router_attach_stream(webrtc::VideoCaptureInput *stream_input,
	bool needs_buffer_rotation)
{
	struct vie_enc_stream *stream = NULL;

	stream = (struct vie_enc_stream *)mem_zalloc(sizeof(*stream), stream_destructor);
	if (!stream)
		return; // TODO: report error

	stream->input = stream_input;
	debug("%s: attaching stream: %p rotate=%d clen=%d\n",
	      __FUNCTION__, stream_input, needs_buffer_rotation, list_count(&router.streaml));

	lock_write_get(router.lock);
	list_append(&router.streaml, &stream->le, stream);
	router.buffer_rotate |= needs_buffer_rotation;

	lock_rel(router.lock);
}

void vie_capture_router_detach_stream(webrtc::VideoCaptureInput *stream_input)
{
	struct vie_enc_stream *stream = NULL;
	struct le *found = NULL;
	struct le *le = NULL;

	lock_write_get(router.lock);

	LIST_FOREACH(&router.streaml, le) {
		stream = (struct vie_enc_stream *)le->data;
		
		if (stream->input == stream_input) {
			found = le;
		}
	}

	if (found)
		mem_deref(found);

	lock_rel(router.lock);
}

extern "C" {

void vie_capture_router_handle_frame(struct avs_vidframe *frame)
{
	webrtc::VideoFrame rtc_frame;
	webrtc::VideoType rtc_type;
	webrtc::VideoRotation rtc_rotation;
	bool needs_convert = false;
	bool log_convert = false;
	int dw = frame->w;
	int dh = frame->h;

	struct vie_enc_stream *stream = NULL;
	struct le *le = NULL;

	uint64_t now = tmr_jiffies();

	router.fps_count++;
	uint64_t msec = now - router.ts_fps;
	if (msec > STATS_DELAY) {
		if (msec < STATS_DELAY + 1000) {
			info("%s: res: %dx%d fps: %0.2f str: %u\n", __FUNCTION__, dw, dh,
				(float)router.fps_count * 1000.0f / msec,
				list_count(&router.streaml));
			log_convert = true;
		}
		router.fps_count = 0;
		router.ts_fps = now;
	}

	if (list_count(&router.streaml) == 0)
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

		if (log_convert) {
			info("%s: convert src: %dx%d str: %zu/%zu dst: %dx%d "
			      "str: %zu/%zu rot: %d\n",
			      __FUNCTION__, frame->w, frame->h,
			      frame->ys, frame->us, dw, dh, dys,
			      duvs, crot);
		}

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

	LIST_FOREACH(&router.streaml, le) {
		stream = (struct vie_enc_stream *)le->data;

		stream->input->IncomingCapturedFrame(rtc_frame);
	}
	lock_rel(router.lock);

out:
	return;
}

};


