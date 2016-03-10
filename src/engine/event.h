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
 * Event handling
 */


struct json_object;


/* Housekeeping
 */

int engine_event_update_token(struct engine *engine, const char *token);


/* Event listeners
 */

typedef void (engine_event_h)(struct engine *engine, const char *type,
			      struct json_object *job, bool catchup);
struct engine_event_lsnr {
	struct le le;
	const char *type;
	engine_event_h *eventh;
};

void engine_event_register(struct engine_event_lsnr *lsnr);


/* Module
 */

struct engine_module;
extern struct engine_module engine_event_module;
