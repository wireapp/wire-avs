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

#include "stats_util.h"
#include <gtest/gtest.h>

using namespace webrtc;
using std::string;


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

class Stats :   public Base,
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
    const auto connection = wire::getConnection(report);
    ASSERT_EQ(connection, std::get<2>(GetParam()));
}




