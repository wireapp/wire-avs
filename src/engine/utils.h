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
/* libavs -- simple sync engine
 *
 * Utilities
 */

struct engine_status_data {
	engine_status_h *h;
	void *arg;
};

int engine_alloc_status(struct engine_status_data **datap,
			engine_status_h *h, void *arg);
int engine_rest_err(int err, const struct http_msg *msg);
void engine_status_handler(int err, const struct http_msg *msg,
			   struct mbuf *mb, struct json_object *jobj,
			   void *arg);

int engine_str_repl(char **dstp, const char *src);
