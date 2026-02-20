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

#include "avsstats.h"
#include <gtest/gtest.h>

using namespace webrtc;
using std::string;


TEST(stats, null_report)
{
	rtc::scoped_refptr<const RTCStatsReport> report;
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.connection, std::string());
	ASSERT_EQ(stats.jitter, std::make_pair(0.0, 0.0));
}

TEST(stats, empty_report)
{
	auto report = RTCStatsReport::Create(Timestamp::Zero());
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.connection, std::string());
	ASSERT_EQ(stats.jitter, std::make_pair(0.0, 0.0));
}


class Base {

public:
	virtual void SetUp(const string& protocol, const string& candidate_type)
	{
		auto local_candidate = new RTCLocalIceCandidateStats("someLocalCandidateId", Timestamp::Zero());
		local_candidate->protocol = protocol;
		local_candidate->candidate_type = candidate_type;

		auto candidate_pair = new RTCIceCandidatePairStats(candidate_pair_id, Timestamp::Zero());
		candidate_pair->local_candidate_id = local_candidate->id();
		candidate_pair->state = "succeeded";
		candidate_pair->nominated = "true";

		report = RTCStatsReport::Create(Timestamp::Zero());
		report->AddStats(std::unique_ptr<RTCStats>(local_candidate));
		report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));
	}

protected:
	rtc::scoped_refptr<RTCStatsReport> report;
	std::string candidate_pair_id = "someCandidatePairId";
};

class Stats : public Base,
				public ::testing::Test {
public:
	virtual void SetUp() override
	{
		Base::SetUp("udp", "host");
	}
protected:
	std::string expected_connection = std::string("udp");
};

TEST_F(Stats, report_without_transport)
{
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.connection, expected_connection);
}

TEST_F(Stats, report_with_transport)
{
	auto transport = new RTCTransportStats("someTransportId", Timestamp::Zero());
	transport->selected_candidate_pair_id = candidate_pair_id;
	report->AddStats(std::unique_ptr<RTCStats>(transport));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.connection, expected_connection);
}

class StatsParam : public Base,
	public ::testing::TestWithParam<std::tuple<std::string, std::string, std::string>> {
public:
	virtual void SetUp() override
	{
		Base::SetUp(std::get<0>(GetParam()), std::get<1>(GetParam()));

		auto transport = new RTCTransportStats("someTransportId", Timestamp::Zero());
		transport->selected_candidate_pair_id = candidate_pair_id;
		report->AddStats(std::unique_ptr<RTCStats>(transport));
	}
};

INSTANTIATE_TEST_CASE_P(StatsParameter,
						 StatsParam,
						 ::testing::Values(
							// udp suite
							std::tuple{"udp", "host", "udp"},
							std::tuple{"udp", "srflx", "udp"},
							std::tuple{"udp", "prflx", "udp"},
							std::tuple{"udp", "relay", "Relay/udp"},
							// tcp suite
							std::tuple{"tcp", "host", "tcp"},
							std::tuple{"tcp", "srflx", "tcp"},
							std::tuple{"tcp", "prflx", "tcp"},
							std::tuple{"tcp", "relay", "Relay/tcp"}
						));

TEST_P(StatsParam, ptotocol_type_tests) {
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.connection, std::get<2>(GetParam()));
}


TEST(stats_jitter, audio_report)
{
	auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
	irrelevant_rtp->kind = "irrelevant";
	irrelevant_rtp->jitter = 0.5;

	auto audio_rtp = new RTCInboundRtpStreamStats("someRtpId", Timestamp::Zero());
	audio_rtp->kind = "audio";
	audio_rtp->jitter = 0.1;

	auto another_audio_rtp = new RTCInboundRtpStreamStats("anotherRtpId", Timestamp::Zero());
	another_audio_rtp->kind = "audio";
	another_audio_rtp->jitter = 0.2;

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(another_audio_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter, std::make_pair(0.2, 0.0));
}

TEST(stats_jitter, video_report)
{
	auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
	irrelevant_rtp->kind = "irrelevant";
	irrelevant_rtp->jitter = 0.5;

	auto video_rtp = new RTCInboundRtpStreamStats("someRtpId", Timestamp::Zero());
	video_rtp->kind = "video";
	video_rtp->jitter = 0.1;

	auto another_video_rtp = new RTCInboundRtpStreamStats("anotherRtpId", Timestamp::Zero());
	another_video_rtp->kind = "video";
	another_video_rtp->jitter = 0.2;

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(another_video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter, std::make_pair(0.0, 0.2));
}

TEST(stats_jitter, audio_and_video_report)
{
	auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
	irrelevant_rtp->kind = "irrelevant";
	irrelevant_rtp->jitter = 0.5;

	auto audio_rtp = new RTCInboundRtpStreamStats("someAudioRtpId", Timestamp::Zero());
	audio_rtp->kind = "audio";
	audio_rtp->jitter = 0.1;

	auto video_rtp = new RTCInboundRtpStreamStats("someVideoRtpId", Timestamp::Zero());
	video_rtp->kind = "video";
	video_rtp->jitter = 0.2;

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter, std::make_pair(0.1, 0.2));
}

TEST(stats_audio, null_audio_level)
{
	auto audio_source = new RTCAudioSourceStats("someAudioSource", Timestamp::Zero());

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(audio_source));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.audio_level, 0);
}

TEST(stats_audio, audio_level)
{
	const auto expected_audio_level = 25; // 0.1 * 255
	auto audio_source = new RTCAudioSourceStats("someAudioSource", Timestamp::Zero());
	audio_source->audio_level= 0.1;

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(audio_source));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.audio_level, expected_audio_level);
}

TEST(stats_rtt, without_candidates)
{
	const auto expected_rtt = 0;
	auto report = RTCStatsReport::Create(Timestamp::Zero());

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.rtt, expected_rtt);
}

TEST(stats_rtt, unsucceeded_candidates)
{
	const auto expected_rtt = 0;

	auto unsuccedded_candidate_pair = new RTCIceCandidatePairStats("unsucceededCandidatePair", Timestamp::Zero());
	unsuccedded_candidate_pair->state = "unsucceeded";
	unsuccedded_candidate_pair->current_round_trip_time = 0.01;

	auto empty_candidate_pair = new RTCIceCandidatePairStats("emptyCandidatePair", Timestamp::Zero());

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(unsuccedded_candidate_pair));
	report->AddStats(std::unique_ptr<RTCStats>(empty_candidate_pair));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.rtt, expected_rtt);
}

TEST(stats_rtt, some_rtt_values)
{
	const auto expected_rtt = 10; // 0.1 * 1000

	auto candidate_pair = new RTCIceCandidatePairStats("someCandidatePairId", Timestamp::Zero());
	candidate_pair->state = "succeeded";
	candidate_pair->current_round_trip_time = 0.01;

	auto report = RTCStatsReport::Create(Timestamp::Zero());
	report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.rtt, expected_rtt);
}

TEST(stats_packets, irrelevant_packet_stats)
{
	auto report = RTCStatsReport::Create(Timestamp::Zero());

	auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
	irrelevant_rtp->kind = "irrelevant";
	irrelevant_rtp->packets_received = 10000;
	report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));

	auto another_irrelevant_rtp = new RTCOutboundRtpStreamStats("anotherIrrelevantRtpId", Timestamp::Zero());
	another_irrelevant_rtp->kind = "irrelevant";
	another_irrelevant_rtp->packets_sent = 10000;
	report->AddStats(std::unique_ptr<RTCStats>(another_irrelevant_rtp));

	const auto expected_packets = std::pair<uint32_t, uint32_t>{0, 0};
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.packets_received, expected_packets);
	ASSERT_EQ(stats.packets_sent, expected_packets);
	ASSERT_EQ(stats.packets_lost, 0);
}

TEST(stats_packets, some_packet_stats)
{
	auto report = RTCStatsReport::Create(Timestamp::Zero());

	// Two incoming audio streams with 10 and 20 packets respectively
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

	// Two incoming video streams with 1 and 2 packets respectively
	auto video_rtp = new RTCInboundRtpStreamStats("someVideoRtpId", Timestamp::Zero());
	video_rtp->kind = "video";
	video_rtp->packets_received = 1;
	video_rtp->packets_lost = 3;
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));

	auto another_video_rtp = new RTCInboundRtpStreamStats("anotherVideoRtpId", Timestamp::Zero());
	another_video_rtp->kind = "video";
	another_video_rtp->packets_received = 2;
	report->AddStats(std::unique_ptr<RTCStats>(another_video_rtp));

	// an outgoing audio stream with 5 packets
	auto outbound_audio_rtp = new RTCOutboundRtpStreamStats("someOutboundAudioRtpId", Timestamp::Zero());
	outbound_audio_rtp->kind = "audio";
	outbound_audio_rtp->packets_sent = 5;
	report->AddStats(std::unique_ptr<RTCStats>(outbound_audio_rtp));

	// an outgoing video stream with 4 packets
	auto outbound_video_rtp = new RTCOutboundRtpStreamStats("someOutboundVideoRtpId", Timestamp::Zero());
	outbound_video_rtp->kind = "video";
	outbound_video_rtp->packets_sent = 4;
	report->AddStats(std::unique_ptr<RTCStats>(outbound_video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	// inbound audio
	ASSERT_EQ(stats.packets_received.first, 10 + 20);
	// inbound video
	ASSERT_EQ(stats.packets_received.second, 1 + 2);
	// outbound audio
	ASSERT_EQ(stats.packets_sent.first, 5);
	// outbound video
	ASSERT_EQ(stats.packets_sent.second, 4);
	// packets lost
	ASSERT_EQ(stats.packets_lost, 1 + 2 + 3);
}

