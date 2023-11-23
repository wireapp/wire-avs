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

#ifndef AVS_KCALL_H
#define AVS_KCALL_H

void kcall_init(int test_view);
void kcall_close(void);

void kcall_set_wuser(WUSER_HANDLE wuser);
WUSER_HANDLE kcall_get_wuser(void);

void kcall_view_show(void);
void kcall_view_hide(void);

void kcall_preview_start(void);
void kcall_preview_stop(void);

void kcall_set_local_user(const char *userid, const char *clientid);

void kcall_set_user_vidstate(const char *convid,
			     const char *userid,
			     const char *clientid,
			     int state);

void kcall_show_mute(bool muted);

void kcall_next_page(void);

#endif

