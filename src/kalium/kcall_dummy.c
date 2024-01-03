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
#include <avs_wcall.h>
#include <avs_kcall.h>
#include <avs_view.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

AVS_EXPORT
void kcall_init(int test_view)
{
}

AVS_EXPORT
void kcall_set_wuser(WUSER_HANDLE wuser)
{
}

AVS_EXPORT
WUSER_HANDLE kcall_get_wuser(void)
{
	return WUSER_INVALID_HANDLE;
}

AVS_EXPORT
void kcall_close(void)
{
}

AVS_EXPORT
void kcall_view_show(void)
{
}

AVS_EXPORT
void kcall_view_hide(void)
{
}

AVS_EXPORT
void kcall_preview_start(void)
{
}

AVS_EXPORT
void kcall_preview_stop(void)
{
}

AVS_EXPORT
void kcall_set_local_user(const char *userid, const char *clientid)
{
}

AVS_EXPORT
void kcall_next_page(void)
{
}

AVS_EXPORT
void kcall_set_user_vidstate(const char *convid,
			     const char *userid,
			     const char *clientid,
			     int state)
{
}

AVS_EXPORT
void kcall_show_mute(bool muted)
{
}

