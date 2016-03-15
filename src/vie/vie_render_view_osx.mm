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

#include "re.h"
#include "avs.h"
#include "avs_vie.h"

#include "vie_render_view.h"

#include "webrtc/modules/video_render/mac/cocoa_render_view.h"

static void *vie_create_renderer_for_view(void* parent_view)
{
	NSView *pv = (NSView*)parent_view;

	NSOpenGLPixelFormatAttribute attr[] = {
		NSOpenGLPFADoubleBuffer,
		NSOpenGLPFADepthSize,
		16,
		0,
	};

	NSOpenGLPixelFormat *pixFmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attr];

	CocoaRenderView *renderer = [[CocoaRenderView alloc] initWithFrame:pv.bounds];
	[renderer initCocoaRenderView:pixFmt];

	return renderer;
}

void vie_set_getsize_handler(vie_getsize_h *getszh)
{
}


void vie_remove_renderer(void* renderer)
{
	CocoaRenderView *rr = (CocoaRenderView*)renderer;

	[rr removeFromSuperview];
}

void *vie_render_view_attach(void *renderer, void *parent_view)
{
	NSView *pv = (NSView*)parent_view;
	CocoaRenderView *rv = (CocoaRenderView*)renderer;

	if (rv == nil) {
		rv = (CocoaRenderView*)vie_create_renderer_for_view(parent_view);
	}

	[pv addSubview: rv];
	rv.hidden = NO;

	rv.frame = pv.bounds;
	return rv;
}

bool vie_resize_renderer_for_video(void* renderer, void *parent_view, 
	webrtc::VideoRender *rtcRenderer, webrtc::VideoRenderCallback **cb, 
	int width, int height, bool mirror)
{
	NSView *pv = (NSView*)parent_view;
	CocoaRenderView *rv = (CocoaRenderView*)renderer;

	(void)rtcRenderer;
	(void)cb;

	debug("%s: renderer %p parent %p rtc %p sz %dx%d mirror %s\n", __FUNCTION__,
		renderer, parent_view, rtcRenderer, width, height, mirror ? "true" : "false");
	if (rv == nil || pv == nil) {
		return false;
	}

	CGRect bnds = pv.bounds;
	struct vrect prect = {
		(int)(bnds.origin.x),
		(int)(bnds.origin.y),
		(int)(bnds.size.width),
		(int)(bnds.size.height)
	};
	struct vrect drect = {0, 0, 0, 0};
	struct vsz vs = {width, height};

	debug("%s: parent bounds %d %d %dx%d\n",
		__FUNCTION__, prect.x, prect.y, prect.w, prect.h);

	if (vie_calc_viewport_for_video(&prect, &vs, &drect) == 0) {
		bnds.origin.x    = drect.x;
		bnds.origin.y    = drect.y;
		bnds.size.width  = drect.w;
		bnds.size.height = drect.h;

		debug("%s: new frame %d %d %dx%d\n",
			__FUNCTION__, drect.x, drect.y, drect.w, drect.h);
		[rv setFrame: bnds];

		return true;
	}

	return false;
}

bool vie_should_mirror_preview(const char *dev_id)
{
	return true;
}

