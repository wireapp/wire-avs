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
#include "fakes.hpp"
#include "fixture.h"


TEST_F(RestTest, get_self)
{
	request("GET", "/self");
	ASSERT_EQ(200, scode);
	ASSERT_EQ(1, jobjc);

	json_object *jobj = jobjv[0];

	// TODO: the response is hardcoded on the fake-backend

	ASSERT_STREQ("blender@wearezeta.com", jzon_str(jobj, "email"));
	ASSERT_STREQ("Test-Blender",          jzon_str(jobj, "name"));
	ASSERT_STREQ("9cba9672-834b-45ac-924d-1af7a6425e0d",
		     jzon_str(jobj, "id"));
}
