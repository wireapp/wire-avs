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

/*
 * ViE -- Video Engine
 */

int  vie_init(struct list *vidcodecl);
void vie_close(void);

int vie_get_video_capture_devices(struct list **device_list);

int vie_set_preview(struct videnc_state* ves, void *view);

int  vie_activate_video_preview(flowmgr_create_preview_h hndlr, void *arg);
void vie_deactivate_video_preview(flowmgr_release_preview_h hndlr, void *arg);

void vie_preview_background(enum media_bg_state state, flowmgr_create_preview_h cpvh,
	flowmgr_release_preview_h rpvh, void *arg);

typedef int (vie_getsize_h)(const void *view, int *w, int *h);

void vie_set_getsize_handler(vie_getsize_h *getsizeh);

#ifdef __cplusplus
}
#endif

