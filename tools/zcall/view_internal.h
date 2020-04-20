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

#ifndef VIEW_INTERNAL_H
#define VIEW_INTERNAL_H

typedef void (runloop_startf)(void);
typedef void (runloop_stopf)(void);

typedef void (view_closef)(void);

typedef void (view_showf)(void);
typedef void (view_hidef)(void);

typedef void (set_local_userf)(const char *userid, const char *clientid);

typedef void (vidstate_changedf)(const char *userid, const char *clientid, int state);

typedef int  (render_framef)(struct avs_vidframe * frame,
			     const char *userid,
			     const char *clientid);

typedef void (preview_startf)(void);
typedef void (preview_stopf)(void);

struct view {
	runloop_startf    *runloop_start;
	runloop_stopf     *runloop_stop;
	view_closef       *view_close;
	view_showf        *view_show;
	view_hidef        *view_hide;
	set_local_userf   *set_local_user;
	vidstate_changedf *vidstate_changed;
	render_framef     *render_frame;
	preview_startf    *preview_start;
	preview_stopf     *preview_stop;
};

#endif

