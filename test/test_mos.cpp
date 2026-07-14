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

// Helper expressions to improve readabilty
constexpr double DOUBLE_MIN = std::numeric_limits<double>::min();
constexpr double DOUBLE_MAX = std::numeric_limits<double>::max();

const double MAX_MOS_SCORE = 5.0;
const double MIN_MOS_SCORE = 1.0;

class MosRangeSanity : public ::testing::TestWithParam<std::tuple<double, double, double>> {

};

INSTANTIATE_TEST_CASE_P(Mos,
						 MosRangeSanity,
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

TEST_P(MosRangeSanity, should_be_in_range_1_to_5) {
	const auto [rtt, packet_loss, jitter_buffer_delay] = GetParam();
	const auto mos_estimate = g107_2_estimate(rtt, packet_loss, jitter_buffer_delay);
	
	EXPECT_TRUE(mos_estimate >= MIN_MOS_SCORE);
	EXPECT_TRUE(mos_estimate <= MAX_MOS_SCORE);
}

class MosSanity: public ::testing::Test {
public:
	double rtt_ms = 50;
	double packet_loss_percentage = 0.01;
	double jitter_buffer_delay_ms =  50;

	double previous_mos_estimate = MAX_MOS_SCORE;
};

TEST_F(MosSanity, should_decrease_with_increasing_rtt) {
	double min_rtt_ms = 0;
	double max_rtt_ms = 1000;
	double delta = 10;

	// initial mos estimate, with low rtt, should be closer to MAX_MOS_SCORE
	const auto initial_mos_estimate = g107_2_estimate(min_rtt_ms, packet_loss_percentage, jitter_buffer_delay_ms);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - initial_mos_estimate) < abs(initial_mos_estimate - MIN_MOS_SCORE));

	for (double rtt = min_rtt_ms; rtt <= max_rtt_ms; rtt += delta) {
		const auto mos_estimate = g107_2_estimate(rtt, packet_loss_percentage, jitter_buffer_delay_ms);
		EXPECT_TRUE(mos_estimate <= previous_mos_estimate);
		previous_mos_estimate = mos_estimate;
	}

	// final mos estimate, with high rtt, should be closer to MIN_MOS_SCORE
	const auto final_mos_estimate = g107_2_estimate(max_rtt_ms, packet_loss_percentage, jitter_buffer_delay_ms);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - final_mos_estimate) > abs(final_mos_estimate - MIN_MOS_SCORE));

}

TEST_F(MosSanity, should_decrease_with_increasing_packet_loss) {
	double zero_packet_loss = 0;
	double full_packet_loss = 1.0;
	double delta = 0.001;

	// initial mos estimate, with low packet loss, should be closer to MAX_MOS_SCORE
	const auto initial_mos_estimate = g107_2_estimate(rtt_ms, zero_packet_loss, jitter_buffer_delay_ms);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - initial_mos_estimate) < abs(initial_mos_estimate - MIN_MOS_SCORE));

	for (double packet_loss = zero_packet_loss; packet_loss <= full_packet_loss; packet_loss += delta) {
		const auto mos_estimate = g107_2_estimate(rtt_ms, packet_loss, jitter_buffer_delay_ms);
		EXPECT_TRUE(mos_estimate <= previous_mos_estimate);
		previous_mos_estimate = mos_estimate;
	}

	// final mos estimate, with high packet loss, should be closer to MIN_MOS_SCORE
	const auto final_mos_estimate = g107_2_estimate(rtt_ms, full_packet_loss, jitter_buffer_delay_ms);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - final_mos_estimate) > abs(final_mos_estimate - MIN_MOS_SCORE));

}

TEST_F(MosSanity, should_decrease_with_increasing_jitter_buffer_delay) {
	double min_jitter_buffer_delay = 0;
	double max_jitter_buffer_delay = 10000;
	double delta = 10;

	// initial mos estimate, with low jitter buffer delay, should be closer to MAX_MOS_SCORE
	const auto initial_mos_estimate = g107_2_estimate(rtt_ms, packet_loss_percentage, min_jitter_buffer_delay);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - initial_mos_estimate) < abs(initial_mos_estimate - MIN_MOS_SCORE));

	for (double jitter_buffer_delay = min_jitter_buffer_delay; jitter_buffer_delay <= max_jitter_buffer_delay; jitter_buffer_delay += delta) {
		const auto mos_estimate = g107_2_estimate(rtt_ms, packet_loss_percentage, jitter_buffer_delay_ms);
		EXPECT_TRUE(mos_estimate <= previous_mos_estimate);
		previous_mos_estimate = mos_estimate;
	}

	// final mos estimate, with high packet loss,  should be closer to MIN_MOS_SCORE
	const auto final_mos_estimate = g107_2_estimate(rtt_ms, packet_loss_percentage, max_jitter_buffer_delay);
	EXPECT_TRUE(abs(MAX_MOS_SCORE - final_mos_estimate) > abs(final_mos_estimate - MIN_MOS_SCORE));

}
