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
#include <avs_vie.h>
#include "vie.h"
#include "vie_renderer.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"

#define STATS_DELAY 20000

extern "C" {
void frame_timeout_timer(void *arg)
{
	ViERenderer *renderer = (ViERenderer*)arg;

	if (renderer) {
		renderer->ReportTimeout();
	}
}
};

ViERenderer::ViERenderer(const char *userid_remote)
	: _state(VIE_RENDERER_STATE_STOPPED)
	, _ts_last(0)
	, _fps_count(0)
{
	lock_alloc(&_lock);
	tmr_init(&_timer);

	str_dup(&_userid_remote, userid_remote);
	_ts_fps = tmr_jiffies();
	tmr_start(&_timer, VIE_RENDERER_TIMEOUT_LIMIT,
		  frame_timeout_timer, this);
}

ViERenderer::~ViERenderer()
{
	tmr_cancel(&_timer);
	SetState(VIE_RENDERER_STATE_STOPPED);
	mem_deref(_userid_remote);
	mem_deref(_lock);
}

void ViERenderer::SetState(enum vie_renderer_state newState)
{
	if (_state != newState) {
		_state = newState;
		if (vid_eng.state_change_h) {
			vid_eng.state_change_h(_userid_remote, newState, vid_eng.cb_arg);
		}
	}
}

/*
 * this function is called from a non-MAIN thread
 */
void ViERenderer::OnFrame(const webrtc::VideoFrame& video_frame)
{
	struct avs_vidframe avs_frame;
	char userid_anon[ANON_ID_LEN];
	int err;

	lock_write_get(_lock);
	SetState(VIE_RENDERER_STATE_RUNNING);
	lock_rel(_lock);

	/* Save the time when the last frame was received */
	uint64_t now = tmr_jiffies();
	_ts_last = now;

	_fps_count++;
	uint64_t msec = now - _ts_fps;
	if (msec > STATS_DELAY) {
		if (msec < STATS_DELAY + 1000) {
			info("vie_renderer_handle_frame user: %s hndlr: %p "
				"res: %dx%d fps: %0.2f\n",
				anon_id(userid_anon, _userid_remote),
				vid_eng.render_frame_h, video_frame.width(),
				video_frame.height(), (float)_fps_count * 1000.0f / msec); 
		}
		_fps_count = 0;
		_ts_fps = now;
	}

	if (!vid_eng.render_frame_h)
		return;
	
	memset(&avs_frame, 0, sizeof(avs_frame));

	avs_frame.type = AVS_VIDFRAME_I420;
	avs_frame.y  = (uint8_t*)video_frame.video_frame_buffer()->DataY();
	avs_frame.u  = (uint8_t*)video_frame.video_frame_buffer()->DataU();
	avs_frame.v  = (uint8_t*)video_frame.video_frame_buffer()->DataV();
	avs_frame.ys = video_frame.video_frame_buffer()->StrideY();
	avs_frame.us = video_frame.video_frame_buffer()->StrideU();
	avs_frame.vs = video_frame.video_frame_buffer()->StrideV();
	avs_frame.w = video_frame.width();
	avs_frame.h = video_frame.height();
	avs_frame.ts = video_frame.timestamp();

	switch(video_frame.rotation()) {
	case webrtc::kVideoRotation_0:
		avs_frame.rotation = 0;
		break;

	case webrtc::kVideoRotation_90:
		avs_frame.rotation = 90;
		break;

	case webrtc::kVideoRotation_180:
		avs_frame.rotation = 180;
		break;

	case webrtc::kVideoRotation_270:
		avs_frame.rotation = 270;
		break;
	}

	err = vid_eng.render_frame_h(&avs_frame, _userid_remote, vid_eng.cb_arg);
	if (err == ERANGE && vid_eng.size_h)
		vid_eng.size_h(avs_frame.w, avs_frame.h, _userid_remote, vid_eng.cb_arg);
		
}

void ViERenderer::ReportTimeout()
{
	bool timeout = false;

	tmr_start(&_timer, VIE_RENDERER_TIMEOUT_LIMIT,
		  frame_timeout_timer, this);

	lock_read_get(_lock);

	if (_ts_last) {
		const uint64_t now = tmr_jiffies();
		int delta = now - _ts_last;

		if (delta > VIE_RENDERER_TIMEOUT_LIMIT)
			timeout = true;

		info("vie: time since last rendered frame: %d ms"
		      " (timeout=%d)\n",
		      delta, timeout);
	}

	if (timeout && _state == VIE_RENDERER_STATE_RUNNING)
	{
		SetState( VIE_RENDERER_STATE_TIMEDOUT);
	}

	lock_rel(_lock);
}

