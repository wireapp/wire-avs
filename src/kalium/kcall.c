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

static struct avs_view *_view = NULL;
static WUSER_HANDLE _wuser = WUSER_INVALID_HANDLE;

int test_view_init(struct avs_view** v);
int osx_view_init(struct avs_view** v);

static int render_handler(struct avs_vidframe * frame,
			  const char *userid,
			  const char *clientid,
			  void *arg)
{
	(void)arg;

	if (_view) {
		return _view->render_frame(frame, userid, clientid);
	}
	return 0;
}

static void size_handler(int w,
			 int h,
			 const char *userid,
			 const char *clientid,
			 void *arg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	info("kcall: size_handler: video size for %s.%s changed to %dx%d\n", 
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid),
	     w, h);
}

void kcall_init(bool test_view)
{
	info("kcall: init test=%s\n", test_view ? "YES" : "NO");

	wcall_set_video_handlers(render_handler, size_handler, NULL);

	if (!test_view) {
#if __APPLE__
		int err = 0;
		err = osx_view_init(&_view);
		if (err == 0)
			return;
#endif
	}

	test_view_init(&_view);
}

void kcall_set_wuser(WUSER_HANDLE wuser)
{
	info("kcall: set wuser %08x\n", wuser);
	_wuser = wuser;
}

WUSER_HANDLE kcall_get_wuser(void)
{
	return _wuser;
}

void kcall_close(void)
{
	if (_view) {
		_view->view_close();
	}
}


void kcall_view_show(void)
{
	if (_view) {
		_view->view_show();
	}
}

void kcall_view_hide(void)
{
	if (_view) {
		_view->view_hide();
	}
}


void kcall_preview_start(void)
{
	if (_view) {
		_view->preview_start();
	}
}

void kcall_preview_stop(void)
{
	if (_view) {
		_view->preview_stop();
	}
}

void kcall_set_local_user(const char *userid, const char *clientid)
{
	if (_view) {
		_view->set_local_user(userid, clientid);
	}
}

void kcall_next_page(void)
{
	if (_view) {
		_view->next_page();
	}
}

void kcall_set_user_vidstate(const char *convid,
			     const char *userid,
			     const char *clientid,
			     int state,
			     void *arg)
{

	if (_view) {
		_view->vidstate_changed(convid, userid, clientid, state);
	}
}


void kcall_show_mute(bool muted)
{
	if (_view) {
		_view->show_mute(muted);
	}
}

