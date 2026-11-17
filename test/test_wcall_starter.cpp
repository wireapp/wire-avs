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

#include <string>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>

/* The conference call starter is reported to the app on reject. */

namespace {

const char *convid = "starter-conv";

struct state {
	unsigned incomings;
	std::string incoming_userid;
	unsigned closes;
	int close_reason;
	std::string close_userid;
	std::string close_clientid;
};

struct state st;


void incoming_handler(const char *convid, uint32_t msg_time,
		      const char *userid, const char *clientid,
		      int video_call, int should_ring, int conv_type,
		      void *arg)
{
	++st.incomings;
	st.incoming_userid = userid ? userid : "";
}


void close_handler(int reason, const char *convid, uint32_t msg_time,
		   const char *userid, const char *clientid, void *arg)
{
	++st.closes;
	st.close_reason = reason;
	st.close_userid = userid ? userid : "";
	st.close_clientid = clientid ? clientid : "";
}


class WcallStarter : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		st = state();

		wcall_set_mode(WCALL_MODE_DIRECT);
		ASSERT_EQ(0, wcall_init(0));

		wuser = wcall_create_ex("self-user", "self-client",
					0, /* use_mediamgr */
					"voe",
					NULL, /* ready */
					NULL, /* send */
					NULL, /* sft */
					incoming_handler,
					NULL, /* missed */
					NULL, /* answer */
					NULL, /* estab */
					close_handler,
					NULL, /* metrics */
					NULL, /* config_req */
					NULL, /* audio_cbr */
					NULL, /* video_state */
					NULL);
		ASSERT_NE(WUSER_INVALID_HANDLE, wuser);

		/* Calls are only accepted once there is a config */
		wcall_config_update(wuser, 0,
				    "{\"ice_servers\":[{"
				    "\"urls\":[\"turn:127.0.0.1:3478\"],"
				    "\"username\":\"user\","
				    "\"credential\":\"secret\"}],"
				    "\"sft_servers\":[{"
				    "\"urls\":[\"https://sft.example.invalid\"]}],"
				    "\"ttl\":3600}");
	}

	virtual void TearDown() override
	{
		wcall_destroy(wuser);
		wcall_close();
		wcall_set_mode(WCALL_MODE_MARSHAL);
	}

protected:
	WUSER_HANDLE wuser = WUSER_INVALID_HANDLE;
};

}


TEST_F(WcallStarter, reject_reports_starter)
{
	std::string msg;

	/* CONFSTART resent by the keygenerator, call started by another */
	msg = "{\"version\":\"3.0\",\"type\":\"CONFSTART\","
		"\"sessid\":\"starter\",\"resp\":false,"
		"\"sft_url\":\"https://sft.example.invalid\","
		"\"secret\":\"c2VjcmV0\",\"timestamp\":\"100\","
		"\"seqno\":\"1\","
		"\"starter_userid\":\"starter-user\","
		"\"starter_clientid\":\"starter-client\"}";
	ASSERT_EQ(0, wcall_recv_msg(wuser, (const uint8_t *)msg.data(),
				    msg.size(), 100, 100, convid,
				    "keygen-user", "keygen-client",
				    WCALL_CONV_TYPE_CONFERENCE, 0));
	ASSERT_EQ(WCALL_STATE_INCOMING, wcall_get_state(wuser, convid));
	EXPECT_EQ(1u, st.incomings);
	EXPECT_EQ("starter-user", st.incoming_userid);

	wcall_reject(wuser, convid);

	EXPECT_EQ(1u, st.closes);
	EXPECT_EQ(WCALL_REASON_STILL_ONGOING, st.close_reason);
	EXPECT_EQ("starter-user", st.close_userid);
	EXPECT_EQ("starter-client", st.close_clientid);
}
