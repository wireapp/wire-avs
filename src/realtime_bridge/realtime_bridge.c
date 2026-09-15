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

#include <errno.h>
#include <string.h>

#include <re.h>

#include "realtime_bridge.h"

static bool valid_role(enum ccall_ecall_role role)
{
	return role == CCALL_ECALL_PUBLISHER ||
	       role == CCALL_ECALL_SUBSCRIBER;
}

static void session_reset(struct realtime_bridge_session *session)
{
	if (!session)
		return;

	session->active = false;
	session->session_id = mem_deref(session->session_id);
	session->session_url = mem_deref(session->session_url);
}

int realtime_bridge_alloc(struct realtime_bridge **bridgep,
			  struct ccall *ccall,
			  const struct realtime_bridge_handlers *handlers,
			  void *arg)
{
	struct realtime_bridge *bridge;

	if (!bridgep)
		return EINVAL;

	bridge = mem_zalloc(sizeof(*bridge), NULL);
	if (!bridge)
		return ENOMEM;

	bridge->ccall = ccall;
	bridge->arg = arg;
	bridge->session[CCALL_ECALL_PUBLISHER].role =
		CCALL_ECALL_PUBLISHER;
	bridge->session[CCALL_ECALL_SUBSCRIBER].role =
		CCALL_ECALL_SUBSCRIBER;
	if (handlers)
		bridge->handlers = *handlers;

	*bridgep = bridge;
	return 0;
}

int realtime_bridge_enable(struct realtime_bridge *bridge, bool enabled)
{
	if (!bridge)
		return EINVAL;

	if (!enabled && (bridge->session[CCALL_ECALL_PUBLISHER].active ||
				 bridge->session[CCALL_ECALL_SUBSCRIBER].active))
		return EBUSY;

	bridge->enabled = enabled;
	return 0;
}

int realtime_bridge_start_role(struct realtime_bridge *bridge,
			       enum ccall_ecall_role role)
{
	if (!bridge || !valid_role(role))
		return EINVAL;
	if (!bridge->enabled)
		return EACCES;

	bridge->session[role].active = true;
	return 0;
}

int realtime_bridge_set_session(struct realtime_bridge *bridge,
				 enum ccall_ecall_role role,
				 const char *session_id,
				 const char *session_url)
{
	struct realtime_bridge_session *session;
	int err;

	if (!bridge || !valid_role(role) || !session_id)
		return EINVAL;

	session = &bridge->session[role];
	err = str_dup(&session->session_id, session_id);
	if (err)
		return err;

	if (session_url) {
		err = str_dup(&session->session_url, session_url);
		if (err)
			return err;
	}

	session->active = true;
	return 0;
}

int realtime_bridge_send(struct realtime_bridge *bridge,
			 enum ccall_ecall_role role,
			 struct econn_message *msg)
{
	if (!bridge || !valid_role(role) || !msg)
		return EINVAL;
	if (!bridge->enabled || !bridge->session[role].active)
		return ENOTCONN;
	if (!bridge->handlers.sendh)
		return ENOSYS;

	return bridge->handlers.sendh(bridge, role, msg, bridge->arg);
}

int realtime_bridge_handle_message(struct realtime_bridge *bridge,
				   enum ccall_ecall_role role,
				   struct econn_message *msg)
{
	if (!bridge || !valid_role(role) || !msg)
		return EINVAL;
	if (!bridge->enabled || !bridge->session[role].active)
		return ENOTCONN;
	if (!bridge->handlers.messageh)
		return ENOSYS;

	return bridge->handlers.messageh(bridge, role, msg, bridge->arg);
}

int realtime_bridge_emit(struct realtime_bridge *bridge,
			 enum ccall_ecall_role role,
			 enum ccall_transport_event event,
			 const struct ccall_transport_state *state)
{
	struct ccall_transport_state current;

	if (!bridge || !valid_role(role))
		return EINVAL;

	if (state)
		bridge->state[role] = *state;
	else
		memset(&bridge->state[role], 0, sizeof(bridge->state[role]));

	current = bridge->state[role];
	if (event == CCALL_TRANSPORT_CLOSED)
		bridge->session[role].active = false;

	if (bridge->handlers.eventh)
		bridge->handlers.eventh(bridge, role, event, &current, bridge->arg);

	return 0;
}

int realtime_bridge_get_state(const struct realtime_bridge *bridge,
			      enum ccall_ecall_role role,
			      struct ccall_transport_state *state)
{
	if (!bridge || !valid_role(role) || !state)
		return EINVAL;

	*state = bridge->state[role];
	return 0;
}

void realtime_bridge_stop_role(struct realtime_bridge *bridge,
			       enum ccall_ecall_role role)
{
	if (!bridge || !valid_role(role))
		return;

	session_reset(&bridge->session[role]);
	memset(&bridge->state[role], 0, sizeof(bridge->state[role]));
}

void realtime_bridge_close(struct realtime_bridge *bridge)
{
	if (!bridge)
		return;

	session_reset(&bridge->session[CCALL_ECALL_PUBLISHER]);
	session_reset(&bridge->session[CCALL_ECALL_SUBSCRIBER]);
	mem_deref(bridge);
}
