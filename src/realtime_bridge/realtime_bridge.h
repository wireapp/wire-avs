/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#ifndef REALTIME_BRIDGE_H
#define REALTIME_BRIDGE_H

#include <stdbool.h>

struct config;
struct ecall_conf;
struct econn_message;
struct icall;
struct list;
struct stats_report;
struct wcall_members;
struct zapi_ice_server;
enum icall_call_type;
enum icall_stream_mode;
enum icall_vstate;

#include <avs_ccall.h>

struct realtime_bridge;

struct realtime_bridge_session {
	enum ccall_ecall_role role;
	bool active;
	char *session_id;
	char *session_url;
};

struct realtime_bridge_handlers {
	/* Send an AVS/econn message to the provider adapter. */
	int (*sendh)(struct realtime_bridge *bridge, enum ccall_ecall_role role, struct econn_message *msg, void *arg);

	/* Deliver a provider message to the selected ECall. */
	int (*messageh)(struct realtime_bridge *bridge, enum ccall_ecall_role role, struct econn_message *msg, void *arg);

	/* Observe role-specific lifecycle events. */
	void (*eventh)(struct realtime_bridge *bridge, enum ccall_ecall_role role,
					enum ccall_transport_event event, const struct ccall_transport_state *state, void *arg);
};

struct realtime_bridge {
	struct ccall *ccall;
	struct realtime_bridge_session session[2];
	struct realtime_bridge_handlers handlers;
	struct ccall_transport_state state[2];
	void *arg;
	bool enabled;
};

int realtime_bridge_alloc(struct realtime_bridge **bridgep,
			  struct ccall *ccall,
			  const struct realtime_bridge_handlers *handlers,
			  void *arg);

int realtime_bridge_enable(struct realtime_bridge *bridge, bool enabled);

int realtime_bridge_start_role(struct realtime_bridge *bridge,
			       enum ccall_ecall_role role);

int realtime_bridge_set_session(struct realtime_bridge *bridge,
				 enum ccall_ecall_role role,
				 const char *session_id,
				 const char *session_url);

int realtime_bridge_send(struct realtime_bridge *bridge,
			 enum ccall_ecall_role role,
			 struct econn_message *msg);

int realtime_bridge_handle_message(struct realtime_bridge *bridge,
				   enum ccall_ecall_role role,
				   struct econn_message *msg);

int realtime_bridge_emit(struct realtime_bridge *bridge,
			 enum ccall_ecall_role role,
			 enum ccall_transport_event event,
			 const struct ccall_transport_state *state);

int realtime_bridge_get_state(const struct realtime_bridge *bridge,
			      enum ccall_ecall_role role,
			      struct ccall_transport_state *state);

void realtime_bridge_stop_role(struct realtime_bridge *bridge,
			       enum ccall_ecall_role role);

void realtime_bridge_close(struct realtime_bridge *bridge);

#endif
