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

bool operator==(const stats_rx_tx& lhs, const stats_rx_tx& rhs) {
	return lhs.rx == rhs.rx && lhs.tx == rhs.tx;
}

bool operator==(const stats_packet_counts& lhs, const stats_packet_counts& rhs) {
	return lhs.audio == rhs.audio &&
			lhs.video == rhs.video &&
			lhs.lost == rhs.lost;
}

bool operator==(const stats_jitter& lhs, const stats_jitter& rhs) {
	return lhs.audio == rhs.audio && lhs.video == rhs.video;
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

//------------------------ SANITY TESTS  -----------------------------------

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

//------------------------ PACKET STATISTICS -----------------------------------

class Base {
public:
	virtual void SetUp() {
		stats_alloc(&stats, NULL);
		report = RTCStatsReport::Create(Timestamp::Zero());
	}

	virtual void TearDown() {
		mem_deref(stats);
	}

protected:
	rtc::scoped_refptr<RTCStatsReport> report;
	avs_stats *stats;
	stats_report sr;
};

class StatsBase : public Base,
						public ::testing::Test {
public:
	virtual void SetUp() override {
		Base::SetUp();
	}

	virtual void TearDown() override {
		Base::TearDown();
	}
};

TEST_F(StatsBase, irrelevant_packet_stats)
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

TEST_F(StatsBase, some_packet_stats)
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
	expected_packets.audio.rx = 10 + 20;
	expected_packets.audio.tx = 25;
	expected_packets.video.rx = 9 + 19;
	expected_packets.video.tx = 24;
	expected_packets.lost.rx = 1 + 2 + 3 + 0;
	expected_packets.lost.tx = 7 + 8;

	EXPECT_EQ(sr.packets, expected_packets);
}

// ---------------------------------------- Test Audio Level ------------------------------------

TEST_F(StatsBase, audio_level)
{
	const auto expected_audio_level = 25; // 0.1 * 255
	auto audio_source = new RTCAudioSourceStats("someAudioSource", Timestamp::Zero());
	audio_source->kind = "audio";
	audio_source->audio_level= 0.1;
	report->AddStats(std::unique_ptr<RTCStats>(audio_source));

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	ASSERT_EQ(sr.audio_level, expected_audio_level);
}


//------------------------ Connection (Protocol and Candidate) Tests  -----------------------------------

class Connection : public Base,
					public ::testing::TestWithParam<std::tuple<std::string, std::string, stats_proto, stats_cand>> {

public:
	virtual void SetUp() override {
		Base::SetUp();
	}

	virtual void TearDown() override {
		Base::TearDown();
	}
};

INSTANTIATE_TEST_CASE_P(Stats,
						 Connection,
						 ::testing::Values(
							// udp suite
							std::tuple{"udp", "host", STATS_PROTO_UDP, STATS_CAND_HOST},
							std::tuple{"udp", "srflx", STATS_PROTO_UDP, STATS_CAND_SRFLX},
							std::tuple{"udp", "prflx", STATS_PROTO_UDP, STATS_CAND_PRFLX},
							std::tuple{"udp", "relay", STATS_PROTO_UDP, STATS_CAND_RELAY},
							// tcp suite
							std::tuple{"tcp", "host", STATS_PROTO_TCP, STATS_CAND_HOST},
							std::tuple{"tcp", "srflx", STATS_PROTO_TCP, STATS_CAND_SRFLX},
							std::tuple{"tcp", "prflx", STATS_PROTO_TCP, STATS_CAND_PRFLX},
							std::tuple{"tcp", "relay", STATS_PROTO_TCP, STATS_CAND_RELAY}),
						 [](const testing::TestParamInfo<Connection::ParamType>& info) {
							return std::get<0>(info.param) + "_" + std::get<1>(info.param);});

TEST_P(Connection, ptotocol_type) {
	auto local_candidate = new RTCLocalIceCandidateStats("someLocalCandidateId", Timestamp::Zero());
	local_candidate->protocol = std::get<0>(GetParam());
	local_candidate->candidate_type = std::get<1>(GetParam());

	auto candidate_pair = new RTCIceCandidatePairStats("someCandidatePairId", Timestamp::Zero());
	candidate_pair->local_candidate_id = local_candidate->id();
	candidate_pair->state = "succeeded";
	candidate_pair->nominated = "true";

	report->AddStats(std::unique_ptr<RTCStats>(local_candidate));
	report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	EXPECT_EQ(sr.proto, std::get<2>(GetParam()));
	EXPECT_EQ(sr.cand, std::get<3>(GetParam()));
}


// ------------------------------------- Jitter Tests ------------------------------------

class StatsJitter: public Base,
					public ::testing::Test {
public:
	virtual void SetUp() override
	{
		Base::SetUp();

		const auto irrelevant_rtp = new RTCInboundRtpStreamStats("irrelevantRtpId", Timestamp::Zero());
		irrelevant_rtp->kind = "irrelevant";
		irrelevant_rtp->jitter = 1.0;
		report->AddStats(std::unique_ptr<RTCStats>(irrelevant_rtp));

		const auto audio_rtp = new RTCInboundRtpStreamStats("someAudioRtpId", Timestamp::Zero());
		audio_rtp->kind = "audio";
		audio_rtp->jitter = 0.01;
		report->AddStats(std::unique_ptr<RTCStats>(audio_rtp));

		const auto another_audio_rtp = new RTCInboundRtpStreamStats("anotherRtpId", Timestamp::Zero());
		another_audio_rtp->kind = "audio";
		another_audio_rtp->jitter = 0.02;
		report->AddStats(std::unique_ptr<RTCStats>(another_audio_rtp));

		const auto video_rtp = new RTCInboundRtpStreamStats("someVideoRtpId", Timestamp::Zero());
		video_rtp->kind = "video";
		video_rtp->jitter = 0.025;
		report->AddStats(std::unique_ptr<RTCStats>(video_rtp));

		const auto another_video_rtp = new RTCInboundRtpStreamStats("anotherVideoRtpId", Timestamp::Zero());
		another_video_rtp->kind = "video";
		another_video_rtp->jitter = 0.015;
		report->AddStats(std::unique_ptr<RTCStats>(another_video_rtp));

		const auto remote_irrelevant_rtp = new RTCRemoteInboundRtpStreamStats("irrelevantRemoteRtpId", Timestamp::Zero());
		remote_irrelevant_rtp->kind = "irrelevant";
		remote_irrelevant_rtp->jitter = 1.0;
		report->AddStats(std::unique_ptr<RTCStats>(remote_irrelevant_rtp));

		const auto remote_audio_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteAudioRtpId", Timestamp::Zero());
		remote_audio_rtp->kind = "audio";
		remote_audio_rtp->jitter = 0.03;
		report->AddStats(std::unique_ptr<RTCStats>(remote_audio_rtp));

		const auto remote_video_rtp = new RTCRemoteInboundRtpStreamStats("someRemoteVideoRtpId", Timestamp::Zero());
		remote_video_rtp->kind = "video";
		remote_video_rtp->jitter = 0.04;
		report->AddStats(std::unique_ptr<RTCStats>(remote_video_rtp));
	}

	virtual void TearDown() override {
		Base::TearDown();
	}
};

TEST_F(StatsJitter, audio_and_video)
{
	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	stats_jitter expected_jitter;
	expected_jitter.audio.rx = 20; // max of [0.01, 0.02] * 1000
	expected_jitter.audio.tx = 30;
	expected_jitter.video.rx = 25; // max of [0.025, 0.015] * 1000
	expected_jitter.video.tx = 40;

	EXPECT_EQ(sr.jitter, expected_jitter);
}

// ------------------------------------- RTT Tests --------------------------------------
class StatsRtt: public Base,
				public ::testing::Test {
public:
	virtual void SetUp() override
	{
		Base::SetUp();

		candidate_pair = new RTCIceCandidatePairStats("candidatePair", Timestamp::Zero());
		candidate_pair->current_round_trip_time = 0.01;
		auto empty_candidate_pair = new RTCIceCandidatePairStats("emptyCandidatePair", Timestamp::Zero());
		report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));
		report->AddStats(std::unique_ptr<RTCStats>(empty_candidate_pair));
	}

	virtual void TearDown() override {
		Base::TearDown();
	}

protected:
	RTCIceCandidatePairStats* candidate_pair;
	const float zero_rtt = 0.0;
};

TEST_F(StatsRtt, without_candidates)
{
	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	EXPECT_FLOAT_EQ(sr.rtt, zero_rtt);
}

TEST_F(StatsRtt, unsucceeded_candidates)
{
	candidate_pair->state = "unsucceeded";

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	EXPECT_FLOAT_EQ(sr.rtt, zero_rtt);
}

TEST_F(StatsRtt, unnominated_candidates)
{
	candidate_pair->state = "succeeded";
	candidate_pair->nominated = false;

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	EXPECT_FLOAT_EQ(sr.rtt, zero_rtt);
}

TEST_F(StatsRtt, some_rtt_values)
{
	const auto expected_rtt = 0.01;

	candidate_pair->state = "succeeded";
	candidate_pair->nominated = true;
	candidate_pair->current_round_trip_time = expected_rtt;

	stats_update(stats, report->ToJson().c_str());
	stats_get_report(stats, &sr);

	EXPECT_FLOAT_EQ(sr.rtt, expected_rtt);
}

// ----------------------------------------- Sample Json from Web ----------------------------------

TEST(StatsSamples, sample_web)
{
	std::string sample_json = "[";
	sample_json.append("{\"id\":\"OTaudio1A3928733473\",\"timestamp\":1772636175216.103,"
			"\"type\":\"outbound-rtp\",\"codecId\":\"COTaudio1_111_cbr=1;sprop-stereo=0;stereo=0;useinbandfec=1\","
			"\"kind\":\"audio\",\"mediaType\":\"audio\",\"ssrc\":3928733473,\"transportId\":\"Taudio1\","
			"\"bytesSent\":2992,\"packetsSent\":16,\"active\":true,\"headerBytesSent\":448,"
			"\"mediaSourceId\":\"SA1\",\"mid\":\"audio\",\"nackCount\":0,\"packetsSentWithEct1\":0,"
			"\"retransmittedBytesSent\":0,\"retransmittedPacketsSent\":0,\"targetBitrate\":32000,"
			"\"totalPacketSendDelay\":0}");
	sample_json.append("]");

	avs_stats *stats;
	stats_report sr;

	stats_alloc(&stats, NULL);

	stats_update(stats, sample_json.c_str());
	stats_get_report(stats, &sr);

	mem_deref(stats);

	stats_report expected_report = stats_report();
	expected_report.packets.audio.tx = 16;
	EXPECT_EQ(sr,expected_report);
}

// ToDo: Get a full sample from web and use as a test case