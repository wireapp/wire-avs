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

#include "avs_stats.h"
#include <gtest/gtest.h>

using namespace webrtc;
using std::string;

namespace wire {
	bool operator==(const Jitter& lhs, const Jitter& rhs) {
		return lhs.audio == rhs.audio && lhs.video == rhs.video;
	}
	bool operator==(const Packets& lhs, const Packets& rhs) {
		return lhs.audio == rhs.audio && lhs.video == rhs.video;
	}
}

TEST(stats, null_report)
{
	rtc::scoped_refptr<const RTCStatsReport> report;
	struct avs_stats *stats;
	struct stats_report sr;

	stats_alloc(&stats, NULL);
	stats_update(stats, report.ToJson().c_str());
	stats_get_report(stats, &sr);
	mem_deref(stats);
		     	
	ASSERT_EQ(sr.proto, PROTOCOL_UNKNOWN);
	ASSERT_EQ(sr.cand, CANDIDATE_UNKNOWN);
	ASSERT_EQ(sr.jitter.audio_rx, 0.0f);
	ASSERT_EQ(sr.jitter.audio_tx, 0.0f);
	ASSERT_EQ(sr.jitter.video_rx, 0.0f);
	ASSERT_EQ(sr.jitter.video_tx, 0.0f);
}

TEST(stats, empty_report)
{
	auto report = RTCStatsReport::Create(Timestamp::Zero());
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.protocol, PROTOCOL_UNKNOWN);
	ASSERT_EQ(stats.candidate, CANDIDATE_UNKNOWN);
	ASSERT_EQ(stats.jitter_up, wire::Jitter());
	ASSERT_EQ(stats.jitter_down, wire::Jitter());
}


class Base {

public:
	virtual void SetUp(const string& protocol, const string& candidate)
	{
		auto local_candidate = new RTCLocalIceCandidateStats("someLocalCandidateId", Timestamp::Zero());
		local_candidate->protocol = protocol;
		local_candidate->candidate_type = candidate;

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
	stats_protocol expected_protocol = PROTOCOL_UDP;
	stats_candidate expected_candidate = CANDIDATE_HOST;
};

TEST_F(Stats, report_without_transport)
{
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.protocol, expected_protocol);
	ASSERT_EQ(stats.candidate, expected_candidate);
}

TEST_F(Stats, report_with_transport)
{
	auto transport = new RTCTransportStats("someTransportId", Timestamp::Zero());
	transport->selected_candidate_pair_id = candidate_pair_id;
	report->AddStats(std::unique_ptr<RTCStats>(transport));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.protocol, expected_protocol);
	ASSERT_EQ(stats.candidate, expected_candidate);
}

class StatsParam : public Base,
	public ::testing::TestWithParam<std::tuple<std::string, std::string, stats_protocol, stats_candidate>> {
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
							std::tuple{"udp", "host", PROTOCOL_UDP, CANDIDATE_HOST},
							std::tuple{"udp", "srflx", PROTOCOL_UDP, CANDIDATE_SRFLX},
							std::tuple{"udp", "prflx", PROTOCOL_UDP, CANDIDATE_PRFLX},
							std::tuple{"udp", "relay", PROTOCOL_UDP, CANDIDATE_RELAY},
							// tcp suite
							std::tuple{"tcp", "host", PROTOCOL_TCP, CANDIDATE_HOST},
							std::tuple{"tcp", "srflx", PROTOCOL_TCP, CANDIDATE_SRFLX},
							std::tuple{"tcp", "prflx", PROTOCOL_TCP, CANDIDATE_PRFLX},
							std::tuple{"tcp", "relay", PROTOCOL_TCP, CANDIDATE_RELAY}
						));

TEST_P(StatsParam, ptotocol_type_tests) {
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.protocol, std::get<2>(GetParam()));
	ASSERT_EQ(stats.candidate, std::get<3>(GetParam()));
}

class StatsJitter: public ::testing::Test {
public:
	virtual void SetUp() override
	{
		irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
		irrelevant_rtp->kind = "irrelevant";
		irrelevant_rtp->jitter = 1000.0;

		audio_rtp = new RTCInboundRtpStreamStats("someAudioRtpId", Timestamp::Zero());
		audio_rtp->kind = "audio";
		audio_rtp->jitter = 0.1;

		another_audio_rtp = new RTCInboundRtpStreamStats("anotherRtpId", Timestamp::Zero());
		another_audio_rtp->kind = "audio";
		another_audio_rtp->jitter = 0.2;

		video_rtp = new RTCInboundRtpStreamStats("someVideoRtpId", Timestamp::Zero());
		video_rtp->kind = "video";
		video_rtp->jitter = 0.2;

		another_video_rtp = new RTCInboundRtpStreamStats("anotherVideoRtpId", Timestamp::Zero());
		another_video_rtp->kind = "video";
		another_video_rtp->jitter = 0.15;

		remote_irrelevant_rtp = new RTCRemoteInboundRtpStreamStats("irrelevantRemoteRtpId", Timestamp::Zero());
		remote_irrelevant_rtp->kind = "irrelevant";
		remote_irrelevant_rtp->jitter = 1000.0;

		remote_audio_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteAudioRtpId", Timestamp::Zero());
		remote_audio_rtp->kind = "audio";
		remote_audio_rtp->jitter = 0.3;

		remote_video_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteVideoRtpId", Timestamp::Zero());
		remote_video_rtp->kind = "video";
		remote_video_rtp->jitter = 0.4;

		report = RTCStatsReport::Create(Timestamp::Zero());
		report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));
		report->AddStats(std::unique_ptr<RTCStats>(remote_irrelevant_rtp));
	}
protected:
    rtc::scoped_refptr<RTCStatsReport> report;
	RTCInboundRtpStreamStats* irrelevant_rtp;
	RTCInboundRtpStreamStats* audio_rtp;
	RTCInboundRtpStreamStats* another_audio_rtp;
	RTCInboundRtpStreamStats* video_rtp;
	RTCInboundRtpStreamStats* another_video_rtp;
	RTCRemoteInboundRtpStreamStats* remote_irrelevant_rtp;
	RTCRemoteInboundRtpStreamStats* remote_audio_rtp;
	RTCRemoteInboundRtpStreamStats* remote_video_rtp;
};

TEST_F(StatsJitter, audio_report)
{
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(another_audio_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter_up, wire::Jitter(0.2, 0.0));
	ASSERT_EQ(stats.jitter_down, wire::Jitter());
}

TEST_F(StatsJitter, video_report)
{
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(another_video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter_up, wire::Jitter(0.0, 0.2));
	ASSERT_EQ(stats.jitter_down, wire::Jitter());
}

TEST_F(StatsJitter, audio_and_video_report)
{
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter_up, wire::Jitter(0.1, 0.2));
	ASSERT_EQ(stats.jitter_down, wire::Jitter());
}

TEST_F(StatsJitter, both_directions_audio_and_video_report)
{
	report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(video_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(remote_audio_rtp));
	report->AddStats(std::unique_ptr<RTCStats>(remote_video_rtp));

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.jitter_up, wire::Jitter(0.1, 0.2));
	ASSERT_EQ(stats.jitter_down, wire::Jitter(0.3, 0.4));
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

class StatsRtt: public ::testing::Test {
public:
	virtual void SetUp() override
	{
		candidate_pair = new RTCIceCandidatePairStats("candidatePair", Timestamp::Zero());
		candidate_pair->current_round_trip_time = 0.01;

		auto empty_candidate_pair = new RTCIceCandidatePairStats("emptyCandidatePair", Timestamp::Zero());

		report = RTCStatsReport::Create(Timestamp::Zero());
		report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));
		report->AddStats(std::unique_ptr<RTCStats>(empty_candidate_pair));
	}
protected:
    rtc::scoped_refptr<RTCStatsReport> report;
	RTCIceCandidatePairStats* candidate_pair;
	const float zero_rtt = 0.0;
};

TEST_F(StatsRtt, without_candidates)
{
	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_FLOAT_EQ(stats.rtt, zero_rtt);
}

TEST_F(StatsRtt, unsucceeded_candidates)
{
	candidate_pair->state = "unsucceeded";

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_FLOAT_EQ(stats.rtt, zero_rtt);
}

TEST_F(StatsRtt, unnominated_candidates)
{
	candidate_pair->state = "succeeded";
	candidate_pair->nominated = false;

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_FLOAT_EQ(stats.rtt, zero_rtt);
}

TEST_F(StatsRtt, some_rtt_values)
{
	const auto expected_rtt = 0.01;

	candidate_pair->state = "succeeded";
	candidate_pair->nominated = true;
	candidate_pair->current_round_trip_time = expected_rtt;

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_FLOAT_EQ(stats.rtt, expected_rtt);
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

	wire::AvsStats stats;
	stats.ReadFromRTCReport(report);
	ASSERT_EQ(stats.packets_received, wire::Packets());
	ASSERT_EQ(stats.packets_sent, wire::Packets());
	ASSERT_EQ(stats.packets_lost_up, 0);
	ASSERT_EQ(stats.packets_lost_down, 0);
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
	// inbound
	ASSERT_EQ(stats.packets_received, wire::Packets(10 + 20, 1 + 2));
	// outbound
	ASSERT_EQ(stats.packets_sent, wire::Packets(5, 4));
	// downstream packets lost
	ASSERT_EQ(stats.packets_lost_down, 1 + 2 + 3);
	// upstream packets lost
	ASSERT_EQ(stats.packets_lost_up, 0);
}

