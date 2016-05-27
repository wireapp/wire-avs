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


TEST_F(RestTest, queued_requests)
{
	int pending;

	queued_requests(100, "GET", "/invalidresource", &pending);
	ASSERT_EQ(0, pending);
}


TEST_F(RestTest, http_fragmented_response)
{
#if 0
	log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(true);
#endif

	request("GET", "/fragment_test");

	ASSERT_EQ(200, scode);
	ASSERT_EQ(1, jobjc);

	json_object *jobj = jobjv[0];

	ASSERT_STREQ("tcp", jzon_str(jobj, "transport"));
	ASSERT_STREQ("yes", jzon_str(jobj, "fragmented"));
	ASSERT_STREQ("no",  jzon_str(jobj, "is_this_a_cool_test"));
}
