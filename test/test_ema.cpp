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


class Base {
public:
	virtual void SetUp() {
		ema_alloc(&ema, nullptr);
	}

	virtual void TearDown() {
		mem_deref(ema);
	}

protected:
	avs_ema *ema;
};

class EMASanity : public Base,
						public ::testing::Test {
public:
	void SetUp() override {
		Base::SetUp();
	}

	void TearDown() override {
		Base::TearDown();
	}
};

TEST_F(EMASanity, shouldHandleNullPtr)
{
	EXPECT_EQ(ema_alloc(nullptr, nullptr), EINVAL);
}

TEST_F(EMASanity, shouldBeCreated)
{
	EXPECT_TRUE(ema);
}

TEST_F(EMASanity, shouldDefaultToMaxQuality)
{
	const int max_quality_idx = 1;
	int val;
	EXPECT_FALSE(ema_get_val(ema, &val));
	EXPECT_EQ(max_quality_idx, val);
}

TEST_F(EMASanity, shouldNotUpdateInvaldData)
{
	const auto invalid_data = std::vector<float>{
									std::numeric_limits<float>::min(),
									-1.0,
									0,
									0.99,
									3.01,
									std::numeric_limits<float>::max()
	};

	for (const auto& data : invalid_data) {
		EXPECT_EQ(ema_update(ema, data), EINVAL);
	}
}


TEST_F(EMASanity, shouldRespondToStepChangeToMidIn4thVal)
{
	const auto test_parameters = std::vector{
							std::tuple{2.0, 1, "update should resulting ema of 1.20 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.36 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.48 => 1"},
							std::tuple{2.0, 2, "update should resulting ema of 1.56 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.67 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.73 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.79 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.83 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.86 => 2"},
							std::tuple{2.0, 2, "update should resulting ema of 1.89 => 2"},
	};

	for (const auto& [input_data, expected_ema, err_msg] : test_parameters) {
		ema_update(ema, input_data);
		int val;
		ema_get_val(ema, &val);
		EXPECT_EQ(expected_ema, val) << err_msg;
	}
}

TEST_F(EMASanity, shouldRespondToStepChangeToWorstIn7thVal)
{
	const auto test_parameters = std::vector{
							std::tuple{3.0, 1, "update should resulting ema of 1.40 => 1"},
							std::tuple{3.0, 2, "update should resulting ema of 1.72 => 2"},
							std::tuple{3.0, 2, "update should resulting ema of 1.97 => 2"},
							std::tuple{3.0, 2, "update should resulting ema of 2.18 => 2"},
							std::tuple{3.0, 2, "update should resulting ema of 2.34 => 2"},
							std::tuple{3.0, 2, "update should resulting ema of 2.47 => 2"},
							std::tuple{3.0, 3, "update should resulting ema of 2.58 => 3"},
							std::tuple{3.0, 3, "update should resulting ema of 2.66 => 3"},
							std::tuple{3.0, 3, "update should resulting ema of 2.73 => 3"},
							std::tuple{3.0, 3, "update should resulting ema of 2.78 => 3"},
	};

	for (const auto& [input_data, expected_ema, err_msg] : test_parameters) {
		ema_update(ema, input_data);
		int val;
		ema_get_val(ema, &val);
		EXPECT_EQ(expected_ema, val) << err_msg;
	}
}

TEST_F(EMASanity, checkResponseForFlipping)
{
	const auto test_parameters = std::vector{
							std::tuple{2.0, 1, "update should resulting ema of 1.20 => 1"},
							std::tuple{1.0, 1, "update should resulting ema of 1.16 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.32 => 1"},
							std::tuple{1.0, 1, "update should resulting ema of 1.26 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.40 => 1"},
							std::tuple{1.0, 1, "update should resulting ema of 1.32 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.46 => 1"},
							std::tuple{1.0, 1, "update should resulting ema of 1.36 => 1"},
							std::tuple{2.0, 1, "update should resulting ema of 1.49 => 1"},
							std::tuple{1.0, 1, "update should resulting ema of 1.39 => 1"},
	};

	for (const auto& [input_data, expected_ema, err_msg] : test_parameters) {
		ema_update(ema, input_data);
		int val;
		ema_get_val(ema, &val);
		EXPECT_EQ(expected_ema, val) << err_msg;
	}
}