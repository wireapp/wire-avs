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
 * Syncing
 */

struct engine;
struct engine_sync_step;

typedef void (engine_sync_h)(struct engine_sync_step *sync);

struct engine_sync_step {
	struct le le;
	struct engine *engine;
	char name[64];
	float prio;
	engine_sync_h *synch;
};

int  engine_sync_register(struct engine *engine, const char *name,
			  float prio, engine_sync_h *synch);
void engine_sync_unregister(struct engine_sync_step *step);
void engine_sync_next(struct engine_sync_step *step); 
