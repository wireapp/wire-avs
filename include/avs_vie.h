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

enum vie_renderer_state {
	VIE_RENDERER_STATE_STOPPED = 0,
	VIE_RENDERER_STATE_RUNNING,
	VIE_RENDERER_STATE_TIMEDOUT
};

typedef void (vie_video_state_change_h)(const char *userid,
					enum vie_renderer_state state,
					void *arg);
typedef void (vie_video_size_h)(int w, int h, const char *userid, void *arg);
typedef int (vie_render_frame_h)(struct avs_vidframe *frame, const char *userid, void *arg);

int  vie_init(struct list *vidcodecl);
void vie_close(void);

void vie_capture_router_handle_frame(struct avs_vidframe *frame);

void vie_set_video_handlers(vie_video_state_change_h *state_change_h,
			    vie_render_frame_h *render_frame_h,
			    vie_video_size_h *size_h,
			    void *arg);

#ifdef __cplusplus
}
#endif

