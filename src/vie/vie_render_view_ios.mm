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

#ifdef __cplusplus
extern "C" {
#endif
#include "re.h"
#include "avs.h"
#ifdef __cplusplus
};
#endif
#include "avs_vie.h"
#include "vie_render_view.h"
#include "vie.h"

#import "webrtc/modules/video_render/ios/video_render_ios_view.h"


void vie_set_getsize_handler(vie_getsize_h *getszh)
{
}


void vie_remove_renderer(void* renderer)
{
	VideoRenderIosView *rr = (VideoRenderIosView*)renderer;

	dispatch_async(dispatch_get_main_queue(),^{
		[rr removeFromSuperview];
	});
}

void *vie_render_view_attach(void *renderer, void *parent_view)
{
	UIView *pv = (UIView*)parent_view;
	VideoRenderIosView *rv = (VideoRenderIosView*)renderer;

	if (rv == nil) {
		rv = [[VideoRenderIosView alloc] initWithFrame:pv.bounds];
	}

	debug("%s: renderer %p parent %p\n", __FUNCTION__, rv, pv);

	dispatch_async(dispatch_get_main_queue(),^{
		[pv addSubview: rv];
		[rv setCoordinatesForZOrder:0.0f Left:0.0f Top:0.0f Right:1.0f Bottom:1.0f];
		rv.frame = pv.bounds;
		rv.hidden = NO;
	});

	return (void *)rv;
}

void vie_resize_renderer_for_video(void* renderer, void *parent_view, 
	webrtc::VideoRender *rtcRenderer, webrtc::VideoRenderCallback **cb, 
	int width, int height, bool mirror)
{
	UIView *pv = (UIView*)parent_view;
	VideoRenderIosView *rv = (VideoRenderIosView*)renderer;

	(void)rtcRenderer;
	(void)cb;

	if (rv == nil || pv == nil) {
		return;
	}
	debug("%s: renderer %p parent %p rtc %p sz %dx%d\n", __FUNCTION__,
		renderer, parent_view, rtcRenderer, width, height);

	dispatch_async(dispatch_get_main_queue(),^{
		CGRect frm = pv.bounds;
		struct vrect prect = {
			(int)(frm.origin.x),
			(int)(frm.origin.y),
			(int)(frm.size.width),
			(int)(frm.size.height)
		};
		struct vrect drect = {0, 0, 0, 0};
		struct vsz vs = {width, height};
		float left = mirror ? 1.0f : 0.0f;
		float right = 1.0f - left;

		[rv setCoordinatesForZOrder:0.0f Left:left Top:0.0f Right:right Bottom:1.0f];

		if (vie_calc_viewport_for_video(&prect, &vs, &drect) == 0) {
			frm.origin.x    = drect.x;
			frm.origin.y    = drect.y;
			frm.size.width  = drect.w;
			frm.size.height = drect.h;

			debug("%s: new frame %d %d %dx%d\n",
				__FUNCTION__, drect.x, drect.y, drect.w, drect.h);

			[rv setFrame: frm];
		}
	
	});
}

bool vie_should_mirror_preview(const char *dev_id)
{
	static const char* front_id = "com.apple.avfoundation.avcapturedevice.built-in_video:1";

	if (!dev_id) {
		return true;
	}

	return (strcmp(dev_id, front_id) == 0) ? true :false;
}

