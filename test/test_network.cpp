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


TEST(network, sa_private)
{
	struct sa priv, pub;

	sa_set_str(&priv, "10.0.0.42", 0);
	sa_set_str(&pub, "62.96.148.44", 0);

	ASSERT_TRUE(sa_ipv4_is_private(&priv));
	ASSERT_FALSE(sa_ipv4_is_private(&pub));
}
