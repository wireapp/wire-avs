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
/* libavs
 *
 * Notification Events
 */

struct json_object;


typedef void (nevent_estab_h)(void *arg);
typedef void (nevent_recv_h)(struct json_object *jobj, void *arg);
typedef void (nevent_close_h)(int err, void *arg);


struct nevent;

int nevent_alloc(struct nevent **nep, struct websock *websock,
		 struct http_cli *http_cli,
		 const char *server_uri, const char *access_token,
		 nevent_estab_h *estabh, nevent_recv_h *recvh,
		 nevent_close_h *closeh, void *arg);
int nevent_restart(struct nevent *ne);

typedef void (nevent_h)(const char *type, struct json_object *jobj, void *arg);

struct nevent_lsnr {
	struct le le;
	const char *type;  /* NULL for all.  */
	nevent_h *eventh;
	void *arg;
};

int nevent_set_access_token(struct nevent *ne, const char *access_token);

void nevent_register(struct nevent *ne, struct nevent_lsnr *lsnr);
void nevent_unregister(struct nevent_lsnr *lsnr);
