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

#include "../src/peerflow/stats_util.h"
#include <gtest/gtest.h>

using namespace webrtc;


TEST(stats, null_report)
{
    rtc::scoped_refptr<const RTCStatsReport> report;
    const auto connection = wire::getConnection(report);
    ASSERT_EQ(connection, std::string());
}

TEST(stats, empty_report)
{
    auto report = RTCStatsReport::Create(Timestamp::Zero());
    const auto connection = wire::getConnection(report);
    ASSERT_EQ(connection, std::string());
}


class Stats : public ::testing::Test {

public:
	virtual void SetUp() override
	{
        auto local_candidate = new RTCLocalIceCandidateStats("someLocalCandidateId", Timestamp::Zero());
        local_candidate->protocol = "udp";
        local_candidate->candidate_type = "host";

        auto candidate_pair = new RTCIceCandidatePairStats(candidate_pair_id, Timestamp::Zero());
        candidate_pair->local_candidate_id = local_candidate->id();
        candidate_pair->state = "succeeded";
        candidate_pair->nominated = "true";

        report = RTCStatsReport::Create(Timestamp::Zero());
        report->AddStats(std::unique_ptr<RTCStats>(local_candidate));
        report->AddStats(std::unique_ptr<RTCStats>(candidate_pair));
	}

	virtual void TearDown() override
	{
	}

protected:
    rtc::scoped_refptr<RTCStatsReport> report;
    std::string expected_connection = std::string("udp");
    std::string candidate_pair_id = "someCandidatePairId";

};

TEST_F(Stats, report_without_transport)
{
    const auto connection = wire::getConnection(report);
    ASSERT_EQ(connection, expected_connection);
}

TEST_F(Stats, report_with_transport)
{
    auto transport = new RTCTransportStats("someTransportId", Timestamp::Zero());
    transport->selected_candidate_pair_id = candidate_pair_id;
    report->AddStats(std::unique_ptr<RTCStats>(transport));

    const auto connection = wire::getConnection(report);
    ASSERT_EQ(connection, expected_connection);
}






