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

#include "webrtc/video_renderer.h"
#include "webrtc/modules/video_render/include/video_render.h"
#include <re.h>

class ViERenderer : public webrtc::VideoRenderer
{
public:
	ViERenderer();
	virtual ~ViERenderer();
    
	void RenderFrame(const webrtc::VideoFrame& video_frame, int time_to_render_ms);

	bool IsTextureSupported() const;

	int AttachToView(void *view);
	void DetachFromView();

	void setMirrored(bool mirror);

	void setMaxdim(int max_dim);
    
	void setUseTimeoutImage(bool use_timeout_image);
    
	int Start();
	int Stop();
	
	void *View() const;
private:
	void *_platRenderer;
	void *_view;
	webrtc::VideoRender *_rtcRenderer;
	webrtc::VideoRenderCallback *_cb;

	int _vWidth;
	int _vHeight;
	bool _mirror;
    
	int _max_dim;

	bool _use_timeout_image;
    
	struct lock *_lock;

	bool _running;
};

#endif

