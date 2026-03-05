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
	virtual void SetUp() override {
		stats_alloc(&stats, NULL);
	}

	virtual void TearDown() override {
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


class StatsPackets : public ::testing::Test {
public:
	virtual void SetUp() override {
		stats_alloc(&stats, NULL);
		report = RTCStatsReport::Create(Timestamp::Zero());
	}

	virtual void TearDown() override {
		mem_deref(stats);
	}

protected:
	rtc::scoped_refptr<RTCStatsReport> report;
	avs_stats *stats;
	stats_report sr;
};

TEST_F(StatsPackets, irrelevant_packet_stats)
{
	// add some irrelevant inbound and outbound rtp reports
	auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
	irrelevant_rtp->kind = "irrelevant";
	irrelevant_rtp->packets_received = 10000;
	report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));

	auto another_irrelevant_rtp = new RTCOutboundRtpStreamStats("anotherIrrelevantRtpId", Timestamp::Zero());
	another_irrelevant_rtp->kind = "irrelevant";
	another_irrelevant_rtp->packets_sent = 10000;
	report->AddStats(std::unique_ptr<RTCStats>(another_irrelevant_rtp));

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	const auto zero_packets = stats_packet_counts{};

	EXPECT_EQ(sr.packets, zero_packets);
}

TEST_F(StatsPackets, some_packet_stats)
{
	auto report = RTCStatsReport::Create(Timestamp::Zero());

	// Two incoming audio streams with 10 and 20 packets respectively with 1 and 2 packet loss
	auto audio_rtp = new RTCInboundRtpStreamStats("someRtpId", Timestamp::Zero());
	audio_rtp->kind = "audio";
	audio_rtp->packets_received = 10;
	audio_rtp->packets_lost = 1;
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));

	auto another_audio_rtp = new RTCInboundRtpStreamStats("anotherRtpId", Timestamp::Zero());
	another_audio_rtp->kind = "audio";
	another_audio_rtp->packets_received = 20;
	another_audio_rtp->packets_lost = 2;
	report->AddStats(std::unique_ptr<RTCStats>(another_audio_rtp));

	// Two incoming video streams with 9 and 19 packets respectively with 3 and 0 packet loss
	auto video_rtp = new RTCInboundRtpStreamStats("someVideoRtpId", Timestamp::Zero());
	video_rtp->kind = "video";
	video_rtp->packets_received = 9;
	video_rtp->packets_lost = 3;
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));

	auto another_video_rtp = new RTCInboundRtpStreamStats("anotherVideoRtpId", Timestamp::Zero());
	another_video_rtp->kind = "video";
	another_video_rtp->packets_received = 19;
	report->AddStats(std::unique_ptr<RTCStats>(another_video_rtp));

	// an outgoing audio stream with 25 packets
	auto outbound_audio_rtp = new RTCOutboundRtpStreamStats("someOutboundAudioRtpId", Timestamp::Zero());
	outbound_audio_rtp->kind = "audio";
	outbound_audio_rtp->packets_sent = 25;
	report->AddStats(std::unique_ptr<RTCStats>(outbound_audio_rtp));

	// an outgoing video stream with 24 packets
	auto outbound_video_rtp = new RTCOutboundRtpStreamStats("someOutboundVideoRtpId", Timestamp::Zero());
	outbound_video_rtp->kind = "video";
	outbound_video_rtp->packets_sent = 24;
	report->AddStats(std::unique_ptr<RTCStats>(outbound_video_rtp));

	// remote audio report of packet loss of 7
	auto remote_audio_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteAudioRtpId", Timestamp::Zero());
	remote_audio_rtp->kind = "audio";
	remote_audio_rtp->packets_lost = 7;
	report->AddStats(std::unique_ptr<RTCStats>(remote_audio_rtp));

	// remote video report of packet loss of 8
	auto remote_video_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteVideoRtpId", Timestamp::Zero());
	remote_video_rtp->kind = "video";
	remote_video_rtp->packets_lost = 8;
	report->AddStats(std::unique_ptr<RTCStats>(remote_video_rtp));

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	stats_packet_counts expected_packets;
	expected_packets.audio_rx = 10 + 20;
	expected_packets.audio_tx = 25;
	expected_packets.video_rx = 9 + 19;
	expected_packets.video_tx = 24;
	expected_packets.lost_rx = 1 + 2 + 3 + 0;
	expected_packets.lost_tx = 7 + 8;

	EXPECT_EQ(sr.packets, expected_packets);
}
