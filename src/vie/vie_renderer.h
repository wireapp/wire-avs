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

#ifndef VIE_RENDERER_H
#define VIE_RENDERER_H

#define VIE_RENDERER_TIMEOUT_LIMIT 10000

#include "webrtc/video_renderer.h"
#include "webrtc/modules/video_render/include/video_render.h"
#include <re.h>

enum ViERendererState {
	VIE_RENDERER_STATE_STOPPED = 0,
	VIE_RENDERER_STATE_RUNNING,
	VIE_RENDERER_STATE_TIMEDOUT
};
	
class ViERenderer : public webrtc::VideoRenderer
{
public:
	ViERenderer();
	virtual ~ViERenderer();
    
	void RenderFrame(const webrtc::VideoFrame& video_frame, int time_to_render_ms);

	bool IsTextureSupported() const;
	
	void ReportTimeout();

private:
	enum ViERendererState _state;
	lock *_lock;
	struct tmr _timer;
};

#endif

