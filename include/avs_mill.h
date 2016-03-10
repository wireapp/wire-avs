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
/* libavs -- simple backend interface
 */

struct mill;
struct rest_cli;

typedef void (mill_ready_h)(struct mill *mill, void *arg);
typedef void (mill_error_h)(int err, struct mill *mill, void *arg);
typedef void (mill_shut_h)(struct mill *mill, void *arg);

int mill_alloc(struct mill **mlp, const char *request_uri,
	       const char *notification_uri,
	       bool clear_cookies, const char *email,
	       const char *password, const char *user_agent,
	       mill_ready_h *readyh, mill_error_h *errorh,
	       mill_shut_h *shuth, void *arg);
void mill_shutdown(struct mill *ml);

struct rest_cli *mill_get_rest(struct mill *ml);
struct nevent *mill_get_nevent(struct mill *ml);

int mill_clear_cookies(struct mill *mill);

