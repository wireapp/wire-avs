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
 * Definition of struct engine.
 */


struct engine_module;
struct engine_event_data;
struct engine_user_data;
struct engine_conv_data;
struct engine_call_data;


enum engine_state {
	ENGINE_STATE_LOGIN,     /* currently logging in  */
	ENGINE_STATE_STARTUP,   /* starting up after successful login  */
	ENGINE_STATE_ACTIVE,    /* engine up and running  */
	ENGINE_STATE_SYNC,	/* engine up and running and syncing  */
	ENGINE_STATE_SHUTDOWN,  /* engine shutting down  */
	ENGINE_STATE_DEAD       /* shutdown finished  */
};


struct engine_module_state {
	struct le le;
	struct engine *engine;
	struct engine_module *module;
	enum engine_state state;
};


struct engine {
	/* Engine state and loaded modules
	 */
	enum engine_state state;
	bool need_sync;           /* ... once we reach active  */
	bool clear_cookies;       /* ... once we logged in */
	struct list modulel;

	/* Configuration
	 */
	char *request_uri;
	char *notification_uri;
	char *email;
	char *password;         /* will be NULL once login started.  */
	char *user_agent;

	/* Services we use.
	 */
	struct store *store;
	struct dnsc *dnsc;
	struct http_cli *http;
	struct http_cli *http_ws;
	struct rest_cli *rest;
	struct login *login;

	/* Handlers
	 */
	engine_ping_h *readyh;
	engine_status_h *errorh;
	engine_ping_h *shuth;
	void *arg;

	/* Listener list
	 */
	struct list lsnrl;

	/* Module data
	 */
	struct engine_event_data *event;
	struct engine_user_data *user;
	struct engine_conv_data *conv;
	struct engine_call_data *call;

	struct list syncl;  /* struct engine_sync_step */
	uint64_t ts_start;

	struct trace *trace;
	bool destroyed;
};

void engine_error(struct engine *engine, int err);

void engine_active_handler(struct engine *engine);
void engine_shutdown_handler(struct engine *engine);

int engine_event_restart(struct engine *engine);
