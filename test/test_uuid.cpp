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


static void generate_uuid()
{
	char *uuid;
	size_t i;
	int err;

	err = uuid_v4(&uuid);
	ASSERT_EQ(0, err);

	ASSERT_EQ(36, strlen(uuid));
	//ASSERT_EQ('4', uuid[14]);
	//char y = tolower(uuid[19]);
	//ASSERT_TRUE(y=='8' || y=='9' || y=='a' || y=='b');
	for (i=0; i<strlen(uuid); i++) {

		char c = uuid[i];

		ASSERT_TRUE(isxdigit(c) || c == '-');
	}

	mem_deref(uuid);
}


TEST(uuid, generate_v4)
{
	for (int i=0; i<32; i++)
		generate_uuid();
}


TEST(uuid, validate)
{
	ASSERT_TRUE(uuid_isvalid("9cba9672-834b-45ac-924d-1af7a6425e0d"));

	ASSERT_FALSE(uuid_isvalid("dnsaoioijwejrjoijsdjlasd"));
	ASSERT_FALSE(uuid_isvalid("87498723918749872879381723"));
	ASSERT_FALSE(uuid_isvalid("9cba9672-834b-45ac-924d-1af7a6425e0"));
	ASSERT_FALSE(uuid_isvalid("09cba9672-834b-45ac-924d-1af7a6425e0"));
}
