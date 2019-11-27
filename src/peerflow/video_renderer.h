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

#ifndef VIDEO_RENDERER_H
#define VIDEO_RENDERER_H

#include <stdio.h>
#include <re.h>
#include <avs.h>
#include <avs_peerflow.h>

#include "rtc_base/scoped_ref_ptr.h"
#include "api/peerconnectioninterface.h"

#define STATS_DELAY 10000

extern "C" {

struct render_le {
	struct le le;
	rtc::VideoSinkInterface<webrtc::VideoFrame>* sink;
	webrtc::VideoTrackInterface *track;
};

static void render_destructor(void *arg)
{
	struct render_le *le = (struct render_le*)arg;

	list_unlink(&le->le);
	delete le->sink;
}

};


namespace wire {

class VideoRendererSink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
	VideoRendererSink(struct peerflow *pf,
			  const char *userid_remote,
			  const char *clientid_remote);

	~VideoRendererSink();
	
	void OnFrame(const webrtc::VideoFrame& frame);

private:
	char *userid_remote_;
	char *clientid_remote_;
	struct peerflow *pf_;
	int last_width_;
	int last_height_;
	uint64_t ts_fps_;
	uint32_t fps_count_;
	uint32_t frame_count_;
};

}

#endif

