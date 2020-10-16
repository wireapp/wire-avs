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
#include "view.h"
#include "view_internal.h"

static struct view *_view = NULL;

int osx_view_init(struct view** v);
int test_view_init(struct view** v);

void view_init(bool test_view)
{
	if (!test_view) {
#if __APPLE__
		int err;
		err = osx_view_init(&_view);
		if (err == 0) {
			return;
		}
#endif
	}

	test_view_init(&_view);
}

void runloop_start(void)
{
	if (_view) {
		_view->runloop_start();
	}
}

void runloop_stop(void)
{
	if (_view) {
		_view->runloop_stop();
	}
}


void view_close(void)
{
	if (_view) {
		_view->view_close();
	}
}


void view_show(void)
{
	if (_view) {
		_view->view_show();
	}
}

void view_hide(void)
{
	if (_view) {
		_view->view_hide();
	}
}


void preview_start(void)
{
	if (_view) {
		_view->preview_start();
	}
}

void preview_stop(void)
{
	if (_view) {
		_view->preview_stop();
	}
}

void view_set_local_user(const char *userid, const char *clientid)
{
	if (_view) {
		_view->set_local_user(userid, clientid);
	}
}

void wcall_vidstate_handler(const char *convid,
			    const char *userid,
			    const char *clientid,
			    int state,
			    void *arg)
{

	if (_view) {
		_view->vidstate_changed(userid, clientid, state);
	}
}


int render_handler(struct avs_vidframe * frame,
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

void size_handler(int w,
		  int h,
		  const char *userid,
		  const char *clientid,
		  void *arg)
{
	info("size_handler: video size for %s changed to %dx%d\n", userid, w, h);
}

void view_show_mute(bool muted)
{
	if (_view) {
		_view->view_show_mute(muted);
	}
}

