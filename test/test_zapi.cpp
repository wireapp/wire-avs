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


TEST(zapi, prekeys)
{
	struct zapi_prekey out, in = {
		{1,2,3,4},
		4,
		42
	};
	struct json_object *jobj;
	int err;

	jobj = json_object_new_object();

	err = zapi_prekey_encode(jobj, &in);
	ASSERT_EQ(0, err);

	err = zapi_prekey_decode(&out, jobj);
	ASSERT_EQ(0, err);

	ASSERT_EQ(in.key_len, out.key_len);
	ASSERT_TRUE(0 == memcmp(in.key, out.key, in.key_len));
	ASSERT_EQ(in.id, out.id);

	mem_deref(jobj);
}
