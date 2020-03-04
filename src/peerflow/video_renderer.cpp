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

#include "video_renderer.h"

namespace wire {

VideoRendererSink::VideoRendererSink(struct peerflow *pf,
				     const char *userid_remote,
				     const char *clientid_remote) :
	pf_(pf),
	last_width_(0),
	last_height_(0),
	fps_count_(0),
	frame_count_(0)
{
	char uid_anon[ANON_ID_LEN];
	char cid_anon[ANON_CLIENT_LEN];

	str_dup(&userid_remote_, userid_remote);
	str_dup(&clientid_remote_, clientid_remote);
	ts_fps_ = tmr_jiffies();

	info("VideoRenderSink(%p): constructor user: %s.%s\n",
		this, anon_id(uid_anon, userid_remote_),
		anon_client(cid_anon, clientid_remote_));
}

VideoRendererSink::~VideoRendererSink()
{
	char uid_anon[ANON_ID_LEN];
	char cid_anon[ANON_CLIENT_LEN];

	info("VideoRenderSink(%p): destructor user: %s.%s frames: %u\n",
		this, anon_id(uid_anon, userid_remote_),
		anon_client(cid_anon, clientid_remote_), frame_count_);
	mem_deref(userid_remote_);
	mem_deref(clientid_remote_);
}
	
void VideoRendererSink::OnFrame(const webrtc::VideoFrame& frame)
{
	int fw = frame.width();
	int fh = frame.height();
	uint64_t now = tmr_jiffies();
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	frame_count_++;
	fps_count_++;
	uint64_t msec = now - ts_fps_;
	if (msec > STATS_DELAY) {
		if (msec < STATS_DELAY + 1000) {
			info("VideoRenderSync::OnFrame: user: %s.%s res: %dx%d fps: %0.2f\n",
				anon_id(userid_anon, userid_remote_),
				anon_client(clientid_anon, clientid_remote_),
				fw, fh,
				(float)fps_count_ * 1000.0f / msec);
		}
		fps_count_ = 0;
		ts_fps_ = now;
	}

	if (fw != last_width_ || fh != last_height_) {
		iflow_video_sizeh(fw, fh, userid_remote_);
		last_width_ = fw;
		last_height_ = fh;
	}

	struct avs_vidframe avsframe;
	const webrtc::I420BufferInterface *i420;

	avsframe.type = AVS_VIDFRAME_I420;
	avsframe.w = fw;
	avsframe.h = fh;
	avsframe.ts = frame.timestamp_us() / 1000;


	switch(frame.rotation()) {
	case webrtc::kVideoRotation_0:
		avsframe.rotation = 0;
		break;

	case webrtc::kVideoRotation_90:
		avsframe.rotation = 90;
		break;

	case webrtc::kVideoRotation_180:
		avsframe.rotation = 180;
		break;

	case webrtc::kVideoRotation_270:
		avsframe.rotation = 270;
		break;

	default:
		avsframe.rotation = 0;
		break;

	}
	i420 = frame.video_frame_buffer()->GetI420();
	avsframe.y  = (uint8_t *)i420->DataY();
	avsframe.u  = (uint8_t *)i420->DataU();
	avsframe.v  = (uint8_t *)i420->DataV();
	avsframe.ys = i420->StrideY();
	avsframe.us = i420->StrideU();
	avsframe.vs = i420->StrideV();
	iflow_render_frameh(&avsframe, userid_remote_, clientid_remote_);
		
}

}

