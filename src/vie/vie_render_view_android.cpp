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

#include "vie_render_view.h"

#import "webrtc/modules/video_render/android/video_render_android_native_opengl2.h"


static vie_getsize_h *getsizeh;

void vie_set_getsize_handler(vie_getsize_h *getszh)
{
	getsizeh = getszh;
}


void vie_remove_renderer(void* renderer)
{
}

void *vie_render_view_attach(void *renderer, void *parent_view)
{
	(void)renderer;
	
	return parent_view;
}

void vie_resize_renderer_for_video(void* renderer, void *parent_view, 
	webrtc::VideoRender *rtcRenderer, webrtc::VideoRenderCallback **cb, 
	int width, int height, bool mirror)
{
	int vieww;
	int viewh;
	int err;

	if (getsizeh) {
		err = getsizeh(parent_view, &vieww, &viewh);
		if (err) {
			error("vie: %s: could not get view size: %m\n",
			      __func__, err);
			return;
		}
	}
	else {
		error("vie: %s: could not get view size\n",
		      __func__);
		return;
	}

	info("vie: %s: view(%dx%d) reqeusted(%dx%d)\n",
	     __func__, vieww, viewh, width, height);

	struct vrect viewrect = {0, 0, vieww, viewh};
	struct vrect drect = {0, 0, 0, 0};
	struct vsz vs = {width, height};

	if (vie_calc_viewport_for_video(&viewrect, &vs, &drect) == 0) {

		float left, right, top, bottom;
		if (mirror) {
			right  = (float)(drect.x)           / vieww;
			left   = (float)(drect.x + drect.w) / vieww;
		}
		else {
			left   = (float)(drect.x)           / vieww;
			right  = (float)(drect.x + drect.w) / vieww;
		}
		top    = (float)(drect.y)           / viewh;
		bottom = (float)(drect.y + drect.h) / viewh;

		info("vie: %s: left: %.2f right: %.2f top: %.2f bottom: %.2f\n",
		     __func__, left, right, top, bottom);

		rtcRenderer->DeleteIncomingRenderStream(0);
		*cb = rtcRenderer->AddIncomingRenderStream(0, 0, left, top, right, bottom);
		rtcRenderer->StartRender(0);
	}
}

bool vie_should_mirror_preview(const char *dev_id)
{
	struct list *devlist;
	struct videnc_capture_device *dev;
	int err = 0;
	bool mirrored = false;

	if (!dev_id) {
		return true;
	}
	err = vie_get_video_capture_devices(&devlist);
	if (err != 0) {
		goto out;
	}

	if (list_count(devlist) < 1) {
		err = ENODEV;
		goto out;
	}

	if (list_count(devlist) > 0) {
		dev = (struct videnc_capture_device*)list_head(devlist)->data;
		mirrored = (strcmp(dev_id, dev->dev_id) == 0) ? true : false;
	}

out:
	if (devlist) {
		mem_deref(devlist);
	}

	return mirrored;
}

