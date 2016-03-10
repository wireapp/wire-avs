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


#ifndef VIE_RENDER_VIEW_H_
#define VIE_RENDER_VIEW_H_

#include "webrtc/video_renderer.h"
#include "webrtc/modules/video_render/include/video_render.h"

struct vrect {
	int x;
	int y;
	int w;
	int h;
};

struct vsz {
	int w;
	int h;
};


void *vie_render_view_attach(void *renderer, void *parent_view);
void vie_remove_renderer(void* renderer);

void vie_resize_renderer_for_video(void* renderer, void *parent_view, 
	webrtc::VideoRender *rtcRenderer, webrtc::VideoRenderCallback **cb, 
	int width, int height, bool mirror);

int vie_calc_viewport_for_video(const struct vrect *vp, const struct vsz *vs,
	struct vrect *dp);

bool vie_should_mirror_preview(const char *dev_id);

#endif
