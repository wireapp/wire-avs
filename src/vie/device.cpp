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
#include <pthread.h>
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "webrtc/modules/video_capture/include/video_capture_factory.h"
#include "vie_render_view.h"
#include "vie.h"


static void list_destructor(void *arg)
{
	struct list *l = (struct list *)arg;

	list_flush(l);
}


int vie_get_video_capture_devices(struct list **device_list)
{
	int i;
	struct list *dl;
	struct videnc_capture_device *list_id;

	if (!vid_eng.devinfo) {
		return EINVAL;
	}

	dl = (struct list*)mem_zalloc(sizeof(struct list), list_destructor);
	if (! dl) {
		return ENOMEM;
	}

	list_init(dl);

	for (i = 0; i < vid_eng.devinfo->NumberOfDevices(); ++i) {
		list_id = (struct videnc_capture_device *)mem_zalloc(sizeof(videnc_capture_device), NULL);
		
		vid_eng.devinfo->GetDeviceName(i,
			list_id->dev_name, sizeof(list_id->dev_name),
			list_id->dev_id, sizeof(list_id->dev_id));

		debug("get_video_capture_device: adding dev: name=%s id=%s\n",
		      list_id->dev_name, list_id->dev_id);
		list_prepend(dl, &list_id->list_elem, list_id);
	}

	*device_list = dl;
	return 0;
}

