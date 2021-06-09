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


TEST(ztime, absolute)
{
	struct ztime zt;

	int err = ztime_decode(&zt, "2016-02-25T08:39:34.752Z");
	ASSERT_EQ(0, err);

	ASSERT_EQ(1456389574, zt.sec);
	ASSERT_EQ(752,        zt.msec);
}


TEST(ztime, relative_diff)
{
	struct ztime t1, t2;
	int err;

	err = ztime_decode(&t1, "2016-02-25T08:39:34.152Z");
	ASSERT_EQ(0, err);

	err = ztime_decode(&t2, "2016-02-25T08:40:35.752Z");
	ASSERT_EQ(0, err);

	ASSERT_EQ( 61600LL, ztime_diff(&t2, &t1));
	ASSERT_EQ(-61600LL, ztime_diff(&t1, &t2));
}


TEST(ztime, invalid_input)
{
	ASSERT_EQ(EBADMSG, ztime_decode(NULL, "2016-02-25T08:39:34"));
}


TEST(ztime, get)
{
	struct ztime zt;

	int err = ztime_get(&zt);

	ASSERT_EQ(0, err);
	ASSERT_TRUE(zt.sec != 0);
}
