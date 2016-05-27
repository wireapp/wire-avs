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


TEST(string, wordexp_empty)
{
	struct str_wordexp we;
	int err;

	err = str_wordexp(&we, "");
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, we.wordc);

	str_wordfree(&we);
}


TEST(string, wordexp_five_words)
{
	struct str_wordexp we;
	int err;

	err = str_wordexp(&we, "hei  paa\t\tdig\rmr\nspud");
	ASSERT_EQ(0, err);

	ASSERT_EQ(5, we.wordc);
	ASSERT_STREQ("hei",  we.wordv[0]);
	ASSERT_STREQ("paa",  we.wordv[1]);
	ASSERT_STREQ("dig",  we.wordv[2]);
	ASSERT_STREQ("mr",   we.wordv[3]);
	ASSERT_STREQ("spud", we.wordv[4]);

	str_wordfree(&we);
}
