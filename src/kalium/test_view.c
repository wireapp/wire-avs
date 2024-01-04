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
#include "test_capturer.h"
#include "pgm.h"

extern char *zcall_vfile;

int test_view_init(struct avs_view** v);
void test_view_capture_next_frame(void);

static bool _capture_frame = false;
static struct list _capturedl;
static struct list _renderl;
char _convid[ECONN_ID_LEN];

struct capture_elem {
	struct le le;
	char *userid;
};

struct render_user {
	struct le le;
	char userid[ECONN_ID_LEN];
	char clientid[ECONN_ID_LEN];
	int state;
};

static void capture_destructor(void *arg)
{
	struct capture_elem *cap = arg;
	mem_deref(cap->userid);
}

static void test_view_close(void)
{
	list_flush(&_capturedl);
	list_flush(&_renderl);
}

static void test_view_show(void)
{
}

static void test_view_hide(void)
{
}

static void test_preview_start(void)
{
// TODO: fix file input
#if 0
	if (zcall_vfile)
		test_capturer_start_static(zcall_vfile, 15);
	else
#endif
		test_capturer_start_dynamic(640, 480, 15);

}

static void test_preview_stop(void)
{
	test_capturer_stop();
}

static void test_request_streams(void)
{
	char *json_str = NULL;
	struct json_object *jobj;
	struct json_object *jcli;
	struct json_object *jclients;
	struct render_user *u;
	struct le *le;

	jobj = jzon_alloc_object();

	jclients = json_object_new_array();

	LIST_FOREACH(&_renderl, le) {
		u = le->data;
		if (u->state == WCALL_VIDEO_STATE_STARTED) {
			jcli = jzon_alloc_object();
			
			jzon_add_str(jcli, "userid", "%s", u->userid);
			jzon_add_str(jcli, "clientid", "%s", u->clientid);
			json_object_array_add(jclients, jcli);
		}
	}

	jzon_add_str(jobj, "convid", "%s", _convid);
	json_object_object_add(jobj, "clients", jclients);

	jzon_encode(&json_str, jobj);

	if (json_str) {

		WUSER_HANDLE wuser = kcall_get_wuser();
		wcall_request_video_streams(wuser,
					    _convid,
					    0,
					    json_str);
	}
	mem_deref(jobj);
	mem_deref(json_str);
}

static void test_vidstate_changed(const char *convid,
				  const char *userid,
				  const char *clientid,
				  int state)
{
	struct le *le;

	strncpy(_convid, convid, ECONN_ID_LEN);
	LIST_FOREACH(&_renderl, le) {
		struct render_user *u = le->data;

		if (strcmp(userid, u->userid) == 0 &&
		    strcmp(clientid, u->clientid) == 0) {
			u->state = state;
			test_request_streams();
			return;
		}
	}

	struct render_user *u = mem_zalloc(sizeof(struct render_user), NULL);
	strncpy(u->userid, userid, ECONN_ID_LEN);
	strncpy(u->clientid, clientid, ECONN_ID_LEN);
	u->state = state;

	list_append(&_renderl, &u->le, u);
	test_request_streams();
}

static int test_render_frame(struct avs_vidframe * frame,
			     const char *userid,
			     const char *clientid)
{
	if (_capture_frame) {
		struct capture_elem *ce;
		struct le *le;
		char   fname[1024];
		struct pgm p;

		LIST_FOREACH(&_capturedl, le) {
			ce = le->data;
			if (strcmp(ce->userid, userid) == 0) {
				return 0;
			}
		}
		snprintf(fname, 1000, "%s.pgm", userid);
		p.w = frame->w;
		p.h = frame->h;
		p.s = frame->ys;
		p.buf = frame->y;

		pgm_save(&p, fname);

		ce = mem_zalloc(sizeof(*ce), capture_destructor);
		str_dup(&ce->userid, userid);
		list_append(&_capturedl, &ce->le, ce);
	}
	//test_capturer_framenum(frame->y, frame->w);
	return 0;
}

void test_view_capture_next_frame(void)
{
	list_flush(&_capturedl);
	_capture_frame = true;
}

static void test_view_set_local_user(const char *userid, const char *clientid)
{
}

static void test_view_show_mute(bool muted)
{
}

static void test_view_next_page(void)
{
}

static struct avs_view _view = {
	.view_close = test_view_close,
	.view_show = test_view_show,
	.view_hide = test_view_hide,
	.set_local_user = test_view_set_local_user,
	.vidstate_changed = test_vidstate_changed,
	.render_frame = test_render_frame,
	.preview_start = test_preview_start,
	.preview_stop = test_preview_stop,
	.show_mute = test_view_show_mute,
	.next_page = test_view_next_page
};


int test_view_init(struct avs_view** v)
{

	test_capturer_init();
	*v = &_view;
	return 0;
}

