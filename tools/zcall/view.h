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

#ifndef VIEW_H
#define VIEW_H

void runloop_start(void);
void runloop_stop(void);

void view_init(bool test_view);
void view_close(void);

void view_show(void);
void view_hide(void);

void preview_start(void);
void preview_stop(void);

void view_set_local_user(const char *userid, const char *clientid);

void vidstate_handler(const char *userid, enum flowmgr_video_receive_state state,
	enum flowmgr_video_reason reason, void *arg);

void view_show_mute(bool muted);

void view_next_page(void);

int  render_handler(struct avs_vidframe * frame,
		    const char *userid,
		    const char *clientid,
		    void *arg);
void size_handler(int w,
		  int h,
		  const char *userid,
		  const char *clientid,
		  void *arg);

void wcall_vidstate_handler(const char *convid,
			    const char *userid,
			    const char *clientid,
			    int state,
			    void *arg);

#endif

