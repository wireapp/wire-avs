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

#include "api/video/video_frame.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "third_party/libyuv/include/libyuv.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"

#include "capture_source.h"

#define STATS_DELAY 1000
#define MAX_PIXEL_W 1280
#define MAX_PIXEL_H  720
#define MIN_PIXEL_H 120

#define MAX_FPS 15
#define MIN_FPS 15


struct enc_stream {
	struct le le;
	rtc::VideoSinkInterface<webrtc::VideoFrame>* sink;
	rtc::VideoSinkWants wants;
};

static void stream_destructor(void *arg)
{
	struct enc_stream *es = (struct enc_stream*)arg;

	list_unlink(&es->le);
}

namespace wire {

CaptureSource * g_cap = NULL;

CaptureSource::CaptureSource()
{
	_buffer_rotate = false;

	lock_alloc(&_lock);

	_ts_fps = tmr_jiffies();
	_fps_count = 0;
	_skip_count = 0;
	_skipped = 1;
	_max_pixel_count = MAX_PIXEL_W * MAX_PIXEL_H;
	list_init(&_streaml);

	webrtc::VideoTrackSourceConstraints constraints = {
		.min_fps = MIN_FPS,
		.max_fps = MAX_FPS
	};
	this->ProcessConstraints(constraints);
}

CaptureSource::~CaptureSource()
{
	lock_write_get(_lock);
	list_flush(&_streaml);
	lock_rel(_lock);

	mem_deref(_lock);
	_lock = NULL;
}

CaptureSource* CaptureSource::GetInstance()
{
	if (!g_cap) {
		g_cap = new CaptureSource();
	}

	return g_cap;
}

void CaptureSource::ReleaseInstance()
{
	if (g_cap) {
		delete g_cap;
		g_cap = NULL;
	}
}

void CaptureSource::AddRef() const
{
}

rtc::RefCountReleaseStatus CaptureSource::Release() const
{
	return rtc::RefCountReleaseStatus::kOtherRefsRemained;
}

void CaptureSource::AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
				    const rtc::VideoSinkWants& wants)
{
	struct enc_stream *stream = NULL;
	struct le *found = NULL;
	struct le *le = NULL;

	info("CaptureSource::AddOrUpdateSink: rot: %s blk: %s max_pc: %d tgt_pc %d fps: %d\n",
		wants.rotation_applied ? "YES" : "NO",
		wants.black_frames ? "YES" : "NO",
		wants.max_pixel_count,
		wants.target_pixel_count,
		wants.max_framerate_fps);

	lock_write_get(_lock);

	_black_frames = wants.black_frames;

	LIST_FOREACH(&_streaml, le) {
		stream = (struct enc_stream *)le->data;
		
		if (stream->sink == sink) {
			found = le;
			stream->wants = wants;
		}
	}

	if (!found) {
		stream = (struct enc_stream *)mem_zalloc(sizeof(*stream), stream_destructor);
		if (stream) {
			stream->sink = sink;
			stream->wants = wants;
			info("CaptureSource::AddOrUpdateSink: attaching sink: %p clen=%d\n",
			      sink, list_count(&_streaml));

			list_append(&_streaml, &stream->le, stream);
		}
	}

	_max_pixel_count = MAX_PIXEL_W * MAX_PIXEL_H;

	LIST_FOREACH(&_streaml, le) {
		stream = (struct enc_stream *)le->data;

#if 0
		if (stream->wants.max_pixel_count < _max_pixel_count) {
			_max_pixel_count = stream->wants.max_pixel_count;
		}
#endif
	}

	lock_rel(_lock);

	//FireOnChanged();
}

void CaptureSource::RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink)
{
	struct enc_stream *stream = NULL;
	struct le *found = NULL;
	struct le *le = NULL;

	warning("CaptureSource::RemoveSink: %p clen=%d\n",
	      sink, list_count(&_streaml));
	lock_write_get(_lock);

	LIST_FOREACH(&_streaml, le) {
		stream = (struct enc_stream *)le->data;
		
		if (stream->sink == sink) {
			found = le;
		}
	}

	if (found)
		mem_deref(found);

	lock_rel(_lock);

	//FireOnChanged();
}

void CaptureSource::HandleFrame(struct avs_vidframe *frame)
{
	rtc::scoped_refptr<webrtc::I420Buffer> frmbuf;
	webrtc::VideoRotation rtc_rotation;

	int64_t ts_us = tmr_jiffies() * 1000;

	uint32_t dw, dh, yoff, uvoff;

	_fps_count++;
	if (_skip_count) {
		if (_skipped < _skip_count) {
			++_skipped;
			return;
		}
		else
			_skipped = 1;
	}

	dw = frame->w;
	dh = frame->h;

	info("HandleFrame: %dx%d rot=%d\n", dw, dh, frame->rotation);
	
#if 0
	dw = (frame->h * 4 / 3) & ~15;
	if (dw > frame->w) {
		dw = frame->w;
	}
#endif

	yoff = ((frame->w - dw) / 2) & ~1;

	frmbuf = webrtc::I420Buffer::Create(dw, dh);
	frmbuf->InitializeData();

	if (_black_frames) {
		webrtc::I420Buffer::SetBlack(frmbuf.get());
	}
	else {
		switch(frame->type) {
		case AVS_VIDFRAME_NV12:
			libyuv:: NV12ToI420(frame->y + yoff, frame->ys,
				frame->u + yoff, frame->us,
				(uint8_t *)frmbuf->DataY(), frmbuf->StrideY(),
				(uint8_t *)frmbuf->DataU(), frmbuf->StrideU(),
				(uint8_t *)frmbuf->DataV(), frmbuf->StrideV(),
				dw, dh);
			break;

		case AVS_VIDFRAME_NV21:
			libyuv:: NV21ToI420(frame->y + yoff, frame->ys,
				frame->u + yoff, frame->us,
				(uint8_t *)frmbuf->DataY(), frmbuf->StrideY(),
				(uint8_t *)frmbuf->DataU(), frmbuf->StrideU(),
				(uint8_t *)frmbuf->DataV(), frmbuf->StrideV(),
				dw, dh);
			break;

		case AVS_VIDFRAME_I420:
			uvoff = yoff / 2;
			libyuv::I420Copy(frame->y + yoff, frame->ys,
				frame->u + uvoff, frame->us,
				frame->v + uvoff, frame->vs,
				(uint8_t *)frmbuf->DataY(), frmbuf->StrideY(),
				(uint8_t *)frmbuf->DataU(), frmbuf->StrideU(),
				(uint8_t *)frmbuf->DataV(), frmbuf->StrideV(),
				dw, dh);
			break;
		}
	}

	switch (frame->rotation) {
	case 90:
		rtc_rotation = webrtc::kVideoRotation_90;
		break;

	case 180:
		rtc_rotation = webrtc::kVideoRotation_180;
		break;

	case 270:
		rtc_rotation = webrtc::kVideoRotation_270;
		break;

	case 0:
	default:
		rtc_rotation = webrtc::kVideoRotation_0;
		break;
	}
	struct le *le = NULL;
	bool buffer_rotate = false;

	lock_read_get(_lock);
	LIST_FOREACH(&_streaml, le) {
		struct enc_stream *stream = (struct enc_stream*)le->data;
		if (stream) {
			buffer_rotate |= stream->wants.rotation_applied;
		}
	}
	lock_rel(_lock);

	if (buffer_rotate && rtc_rotation != webrtc::kVideoRotation_0) {
		frmbuf = webrtc::I420Buffer::Rotate(*frmbuf, rtc_rotation);
		rtc_rotation = webrtc::kVideoRotation_0;
	}

	uint32_t sw, sh;

	sw = MAX_PIXEL_W;
	sh = MAX_PIXEL_H;

	while((sw > dw || sh > dh || (sw * sh) > _max_pixel_count) &&
		sh > MIN_PIXEL_H) {
		sw /= 2;
		sh /= 2;
	}

	if (dw != sw || dh != sh) {
		rtc::scoped_refptr<webrtc::I420Buffer> sbuf;

		sbuf = webrtc::I420Buffer::Create(sw, sh);
		sbuf->InitializeData();

		sbuf->CropAndScaleFrom(*frmbuf);

		frmbuf = sbuf;
	}
	
	uint64_t now = tmr_jiffies();

	_fps_send++;
	uint64_t msec = now - _ts_fps;
	if (msec > STATS_DELAY) {
		float fps = (float)_fps_count * 1000.0f / msec;
		float fps_send = (float)_fps_send * 1000.0f / msec;
		if (msec < STATS_DELAY + 1000) {
			info("CaptureSource::HandleFrame: res: %dx%d fps: %0.2f/%02f str: %u\n",
			     frame->w, frame->h,
			     fps, fps_send,
			     list_count(&_streaml));
		}
		if (fps > (float)MAX_FPS) {
			_skip_count = ((float)fps + 0.5f) / (float)MAX_FPS;
			info("CaptureSource::HandleFrame: skip: %u\n", _skip_count);
		}
		_fps_count = 0;
		_fps_send = 0;
		_ts_fps = now;
	}

	webrtc::VideoFrame::Builder builder;

	webrtc::VideoFrame rtc_frame(builder.set_video_frame_buffer(frmbuf).
		set_timestamp_us(ts_us).
		set_rotation(rtc_rotation).build());

	lock_read_get(_lock);
	LIST_FOREACH(&_streaml, le) {
		struct enc_stream *stream = (struct enc_stream*)le->data;
		rtc::VideoSinkInterface<webrtc::VideoFrame>* sink = stream->sink;
		sink->OnFrame(rtc_frame);
	}
	lock_rel(_lock);

out:
	return;
}

webrtc::MediaSourceInterface::SourceState CaptureSource::state() const
{
	return kLive;
}

};

extern "C" {

void capture_source_handle_frame(struct avs_vidframe *frame)
{
	wire::CaptureSource::GetInstance()->HandleFrame(frame);
}

};


