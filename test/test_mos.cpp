/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#include "re.h"
#include "avs_icall.h"
#include "avs_stats.h"

#include <limits>

#include <gtest/gtest.h>
#include <fstream>

// Helper expressions to improve readabilty
constexpr double DOUBLE_MIN = std::numeric_limits<double>::min();
constexpr double DOUBLE_MAX = std::numeric_limits<double>::max();

class MosSanity : public ::testing::TestWithParam<std::tuple<double, double, double>> {
public:
	void SetUp() override {}
	void TearDown() override {}
};

INSTANTIATE_TEST_CASE_P(Mos,
						 MosSanity,
						 ::testing::Values(
							std::tuple{0, 0, 0},
							std::tuple{0, DOUBLE_MIN, 0},
							std::tuple{0, 0, DOUBLE_MIN},
							std::tuple{DOUBLE_MIN, 0, 0},
							std::tuple{DOUBLE_MIN, DOUBLE_MIN, 0},
							std::tuple{DOUBLE_MIN, 0, DOUBLE_MIN},
							std::tuple{DOUBLE_MIN, DOUBLE_MIN, 0},
							std::tuple{DOUBLE_MIN, 0, DOUBLE_MIN},
							std::tuple{DOUBLE_MIN, DOUBLE_MIN, DOUBLE_MIN},
							std::tuple{0, DOUBLE_MAX, 0},
							std::tuple{0, 0, DOUBLE_MAX},
							std::tuple{DOUBLE_MAX, 0, 0},
							std::tuple{DOUBLE_MAX, DOUBLE_MAX, 0},
							std::tuple{DOUBLE_MAX, 0, DOUBLE_MAX},
							std::tuple{DOUBLE_MAX, DOUBLE_MAX, 0},
							std::tuple{DOUBLE_MAX, 0, DOUBLE_MAX},
							std::tuple{DOUBLE_MAX, DOUBLE_MAX, DOUBLE_MAX}));

TEST_P(MosSanity, should_be_in_range_1_to_5) {
	const auto [rtt, packet_loss, jitter_buffer_delay] = GetParam();
	const auto mos_estimate = g107_2_estimate(rtt, packet_loss, jitter_buffer_delay);
	
	EXPECT_TRUE(mos_estimate >= 1.0);
	EXPECT_TRUE(mos_estimate <= 5.0);
}
