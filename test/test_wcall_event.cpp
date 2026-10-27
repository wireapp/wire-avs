/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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

#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
struct Events {
	std::vector<std::string> incoming, missed, closed;
};
// Event handles retain their callback context; keep it alive across test cases.
Events events;

void incoming(const char *conv, uint32_t, const char *, const char *,
	      int, int, int, void *arg)
{
	static_cast<Events *>(arg)->incoming.push_back(conv);
}
void missed(const char *conv, uint32_t, const char *, const char *, int, void *arg)
{
	static_cast<Events *>(arg)->missed.push_back(conv);
}
void closed(int, const char *conv, uint32_t, const char *, const char *, void *arg)
{
	static_cast<Events *>(arg)->closed.push_back(conv);
}

class WcallEventQueue : public testing::Test {
protected:
	WUSER_HANDLE user;
	void SetUp() override
	{
		events = Events{};
		user = wcall_event_create("queue-test-user", "queue-test-client",
					 incoming, missed, closed, &events);
		ASSERT_NE(WUSER_INVALID_HANDLE, user);
		wcall_event_start(user);
	}
	void TearDown() override { wcall_event_destroy(user); }
	void process(const char *type, const char *conversation)
	{
		std::string message = std::string("{\"version\":\"3.0\",\"type\":\"")
			+ type + "\",\"sessid\":\"queue-test\",\"resp\":false";
		if (std::string(type) == "CONFSTART") {
			// Required conference payload; no SFT connection is made by this API.
			message += ",\"sft_url\":\"https://sft.example.invalid\","
				"\"secret\":\"c2VjcmV0\",\"timestamp\":\"100\",\"seqno\":\"1\"";
		}
		message += "}";
		ASSERT_EQ(0, wcall_event_process(user,
			reinterpret_cast<const uint8_t *>(message.data()), message.size(),
			100, 100, conversation, "remote-user", "remote-client",
			WCALL_CONV_TYPE_CONFERENCE));
	}
	void finish() { wcall_event_end(user); }
};

TEST_F(WcallEventQueue, emptyQueueClosesUnknownCall)
{
	process("CONFEND", "absent");
	finish();
	EXPECT_EQ((std::vector<std::string>{"absent"}), events.closed);
	EXPECT_TRUE(events.missed.empty());
}

TEST_F(WcallEventQueue, unmatchedTailPreservesIncomingCall)
{
	process("CONFSTART", "other");
	process("CONFEND", "absent");
	finish();
	EXPECT_EQ((std::vector<std::string>{"other"}), events.incoming);
	EXPECT_EQ((std::vector<std::string>{"absent"}), events.closed);
	EXPECT_TRUE(events.missed.empty());
}

TEST_F(WcallEventQueue, findsSecondQueuedCall)
{
	process("CONFSTART", "first");
	process("CONFSTART", "second");
	process("CONFEND", "second");
	finish();
	EXPECT_EQ((std::vector<std::string>{"first"}), events.incoming);
	EXPECT_EQ((std::vector<std::string>{"second"}), events.missed);
	EXPECT_TRUE(events.closed.empty());
}

TEST_F(WcallEventQueue, matchesFirstAndSkipsAlreadyMissedState)
{
	process("CONFSTART", "first");
	process("CONFEND", "first");
	process("CONFEND", "first");
	finish();
	EXPECT_TRUE(events.incoming.empty());
	EXPECT_EQ((std::vector<std::string>{"first"}), events.missed);
	EXPECT_EQ((std::vector<std::string>{"first"}), events.closed);
}
} // namespace
