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

typedef int (vie_getsize_h)(const void *view, int *w, int *h);

void vie_set_getsize_handler(vie_getsize_h *getsizeh);

void vie_capture_router_handle_frame(struct avs_vidframe *frame);

void vie_set_video_handlers(flowmgr_video_state_change_h *state_change_h,
	flowmgr_render_frame_h *render_frame_h, void *arg);

#ifdef __cplusplus
}
#endif

