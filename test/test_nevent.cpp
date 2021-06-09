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
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include "fakes.hpp"
#include "fixture.h"


TEST_F(RestTest, nevent)
{
	// Create an Access-Token on the Server
	backend->addToken(1, "abc-123");

	subscribe();

	ASSERT_EQ(1, nevent_estab_called);
	ASSERT_EQ(0, nevent_recv_called);

	/* Send an event from the Backend server */
	err = backend->simulate_message("guten morgen");
	ASSERT_EQ(0, err);

	// wait for traffic SERVER -> CLIENT
	wait();

	ASSERT_EQ(1, nevent_estab_called);
	ASSERT_EQ(1, nevent_recv_called);

	// todo: move this block to TearDown ?
#if 1
	mem_deref(nevent);
	websock_shutdown(websock);

	// wait for Websocket shutdown
	wait();
#endif
}
