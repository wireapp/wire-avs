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

#include <assert.h>
#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_zapi.h"
#include "avs_econn.h"
#include "econn.h"


void econn_set_state(struct econn *conn, enum econn_state state)
{
	assert(ECONN_MAGIC == conn->magic);

	if (conn->state == state)
		return;

	info("econn: State changed: `%s' --> `%s'\n",
	     econn_state_name(conn->state),
	     econn_state_name(state));

	conn->state = state;

#if 0
	if (conn->stateh)
		conn->stateh(conn, state, conn->arg);
#endif
}


void econn_handle_event(struct econn *conn, int event)
{
	(void)conn;
	(void)event;
}
