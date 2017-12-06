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


TEST(audummy, basic_init_close)
{
	struct list aucodecl = LIST_INIT;
	int err;

	log_set_min_level(LOG_LEVEL_WARN);
	log_enable_stderr(true);

	err = audummy_init(&aucodecl);
	ASSERT_EQ(0, err);
	
	ASSERT_GT(list_count(&aucodecl), 0);

	audummy_close();

	ASSERT_EQ(0, list_count(&aucodecl));
}


TEST(audummy, extra_close)
{
	audummy_close();
}


TEST(audummy, close_twice)
{
	struct list aucodecl = LIST_INIT;
	int err;

	err = audummy_init(&aucodecl);
	ASSERT_EQ(0, err);

	audummy_close();
	audummy_close();
}
