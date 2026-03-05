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
#include "avs_stats.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

#include <gtest/gtest.h>

using namespace webrtc;

bool operator==(const stats_packet_counts& lhs, const stats_packet_counts& rhs) {
	return lhs.audio_rx == rhs.audio_rx &&
			lhs.audio_tx == rhs.audio_tx &&
			lhs.video_rx == rhs.video_rx &&
			lhs.video_tx == rhs.video_tx &&
			lhs.lost_rx == rhs.lost_rx &&
			lhs.lost_tx == rhs.lost_tx;
}

bool operator==(const stats_jitter& lhs, const stats_jitter& rhs) {
	return lhs.audio_rx == rhs.audio_rx &&
			lhs.audio_tx == rhs.audio_tx &&
			lhs.video_rx == rhs.video_rx &&
			lhs.video_tx == rhs.video_tx;
}

bool operator==(const stats_report& lhs, const stats_report& rhs) {
	return lhs.proto == rhs.proto &&
			lhs.cand == rhs.cand &&
			lhs.audio_level == rhs.audio_level &&
			lhs.audio_level_smooth == rhs.audio_level_smooth &&
			lhs.rtt == rhs.rtt &&
			lhs.jitter == rhs.jitter &&
			lhs.packets == rhs.packets;
}

const auto zero_report = stats_report {};

class Sanity : public ::testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
	virtual void SetUp() override
	{
		stats_alloc(&stats, NULL);
	}

	virtual void TearDown() override
	{
		mem_deref(stats);
	}

protected:
	avs_stats *stats;
	stats_report sr;
};

INSTANTIATE_TEST_CASE_P(Stats,
						 Sanity,
						 ::testing::Values(
							// udp suite
							std::tuple{"", "empty_string"},
							std::tuple{"{}", "empty_json"},
							std::tuple{"[]","empty_array"},
							std::tuple{"{[hjdhjd", "some_nonsense"},
							std::tuple{RTCStatsReport::Create(Timestamp::Zero())->ToJson(), "empty_report"}),
						 [](const testing::TestParamInfo<Sanity::ParamType>& info) {
							return std::get<1>(info.param);});

TEST_P(Sanity, input) {

	stats_update(stats, std::get<0>(GetParam()).c_str());
	stats_get_report(stats, &sr);

	EXPECT_EQ(sr, zero_report);
}

