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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* Call starter tracking from CONFSTART/CONFCHECK. No SFT or media is used. */
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "../src/ccall/ccall.h"
}

#define CONVID   "starter-conv"
#define SELF_U   "user-c"
#define SELF_C   "client-c"
#define STARTER_U "user-a"
#define STARTER_C "client-a"
#define SENDER_U "user-b"
#define SENDER_C "client-b"

class CcallStarter : public testing::Test {
protected:
	struct ccall *ccall = nullptr;
	struct ecall_conf conf = {};
	unsigned starts = 0;
	std::string start_userid;
	std::string start_clientid;

	static void start_handler(struct icall *, uint32_t,
				  const char *userid, const char *clientid,
				  bool, bool, enum icall_conv_type, void *arg)
	{
		auto *test = static_cast<CcallStarter *>(arg);

		++test->starts;
		test->start_userid = userid ? userid : "";
		test->start_clientid = clientid ? clientid : "";
	}

	void SetUp() override
	{
		ASSERT_EQ(0, ccall_alloc(&ccall, &conf, CONVID,
					 SELF_U, SELF_C, false, false));
		struct icall *icall = ccall_get_icall(ccall);
		icall->starth = start_handler;
		icall->arg = this;
	}

	void TearDown() override
	{
		mem_deref(ccall);
	}

	static struct econn_message *conf_msg(enum econn_msg type,
					      uint64_t ts,
					      const char *starter_userid,
					      const char *starter_clientid,
					      bool resp = false)
	{
		struct econn_message *msg = econn_message_alloc();
		uint8_t *secret;
		char **pstarter_u, **pstarter_c;

		econn_message_init(msg, type, CONVID);
		msg->resp = resp || (type == ECONN_CONF_CHECK);
		msg->age = 1;

		secret = (uint8_t *)mem_zalloc(16, NULL);
		memset(secret, 0x42, 16);
		if (type == ECONN_CONF_START) {
			str_dup(&msg->u.confstart.sft_url, "https://sft.invalid");
			msg->u.confstart.secret = secret;
			msg->u.confstart.secretlen = 16;
			msg->u.confstart.timestamp = ts;
			msg->u.confstart.seqno = 1;
			pstarter_u = &msg->u.confstart.starter_userid;
			pstarter_c = &msg->u.confstart.starter_clientid;
		}
		else {
			str_dup(&msg->u.confcheck.sft_url, "https://sft.invalid");
			msg->u.confcheck.secret = secret;
			msg->u.confcheck.secretlen = 16;
			msg->u.confcheck.timestamp = ts;
			msg->u.confcheck.seqno = 1;
			pstarter_u = &msg->u.confcheck.starter_userid;
			pstarter_c = &msg->u.confcheck.starter_clientid;
		}
		if (starter_userid)
			str_dup(pstarter_u, starter_userid);
		if (starter_clientid)
			str_dup(pstarter_c, starter_clientid);

		return msg;
	}

	int recv(struct econn_message *msg,
		 const char *userid, const char *clientid)
	{
		int err = ccall_msg_recv(ccall_get_icall(ccall), 0, 0,
					 userid, clientid, msg);
		mem_deref(msg);
		return err;
	}

	void expect_starter(const char *userid, const char *clientid,
			    bool confirmed = true)
	{
		EXPECT_STREQ(userid, ccall->starter_userid);
		EXPECT_STREQ(clientid, ccall->starter_clientid);
		EXPECT_EQ(confirmed, ccall->starter_confirmed);
	}
};


TEST_F(CcallStarter, unknown_when_idle)
{
	EXPECT_TRUE(ccall->starter_userid == nullptr);
	EXPECT_TRUE(ccall->starter_clientid == nullptr);
	EXPECT_FALSE(ccall->starter_confirmed);
}

TEST_F(CcallStarter, confcheck_from_keygenerator_reports_starter)
{
	/* B is keygenerator, but A started the call */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000,
				   STARTER_U, STARTER_C),
			  SENDER_U, SENDER_C));

	EXPECT_EQ(1u, starts);
	EXPECT_EQ(STARTER_U, start_userid);
	EXPECT_EQ(STARTER_C, start_clientid);
	expect_starter(STARTER_U, STARTER_C);
}

TEST_F(CcallStarter, confstart_resend_reports_starter)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_START, 1000,
				   STARTER_U, STARTER_C),
			  SENDER_U, SENDER_C));

	EXPECT_EQ(1u, starts);
	EXPECT_EQ(STARTER_U, start_userid);
	EXPECT_EQ(STARTER_C, start_clientid);
	expect_starter(STARTER_U, STARTER_C);
}

TEST_F(CcallStarter, old_client_falls_back_to_sender)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000, NULL, NULL),
			  SENDER_U, SENDER_C));

	EXPECT_EQ(1u, starts);
	EXPECT_EQ(SENDER_U, start_userid);
	EXPECT_EQ(SENDER_C, start_clientid);
	expect_starter(SENDER_U, SENDER_C, false);
}

TEST_F(CcallStarter, old_client_confstart_request_is_confirmed)
{
	/* Only the initial CONFSTART is a request */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_START, 1000, NULL, NULL),
			  STARTER_U, STARTER_C));

	EXPECT_EQ(1u, starts);
	EXPECT_EQ(STARTER_U, start_userid);
	expect_starter(STARTER_U, STARTER_C, true);
}

TEST_F(CcallStarter, old_client_confstart_response_is_guess)
{
	/* Resent CONFSTART (e.g. by the keygenerator) is a response */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_START, 1000, NULL, NULL, true),
			  SENDER_U, SENDER_C));

	expect_starter(SENDER_U, SENDER_C, false);
}

TEST_F(CcallStarter, guess_upgraded_by_same_call)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000, NULL, NULL),
			  SENDER_U, SENDER_C));
	expect_starter(SENDER_U, SENDER_C, false);

	/* Another old client's CONFCHECK is still a guess, keep the first */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000, NULL, NULL),
			  "user-d", "client-d"));
	expect_starter(SENDER_U, SENDER_C, false);

	/* Newer client knows the starter */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000,
				   STARTER_U, STARTER_C),
			  "user-d", "client-d"));
	expect_starter(STARTER_U, STARTER_C, true);
	EXPECT_EQ(1u, starts);
}

TEST_F(CcallStarter, guess_upgraded_by_late_confstart)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000, NULL, NULL),
			  SENDER_U, SENDER_C));
	expect_starter(SENDER_U, SENDER_C, false);

	/* Initial CONFSTART from an old client arrives late */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_START, 1000, NULL, NULL),
			  STARTER_U, STARTER_C));
	expect_starter(STARTER_U, STARTER_C, true);
}

TEST_F(CcallStarter, confirmed_not_replaced_by_guess)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000,
				   STARTER_U, STARTER_C),
			  SENDER_U, SENDER_C));
	expect_starter(STARTER_U, STARTER_C, true);

	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000, NULL, NULL),
			  "user-d", "client-d"));
	expect_starter(STARTER_U, STARTER_C, true);
}

TEST_F(CcallStarter, earlier_call_replaces_starter)
{
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 2000,
				   SENDER_U, SENDER_C),
			  SENDER_U, SENDER_C));
	expect_starter(SENDER_U, SENDER_C);

	/* Same call again from someone else, starter is kept */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 2000,
				   "user-d", "client-d"),
			  "user-d", "client-d"));
	expect_starter(SENDER_U, SENDER_C);

	/* Earlier call wins, along with its starter */
	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000,
				   STARTER_U, STARTER_C),
			  SENDER_U, SENDER_C));
	expect_starter(STARTER_U, STARTER_C);
	EXPECT_EQ(1u, starts);
}

TEST_F(CcallStarter, cleared_on_confend)
{
	struct econn_message *end;

	ASSERT_EQ(0, recv(conf_msg(ECONN_CONF_CHECK, 1000,
				   STARTER_U, STARTER_C),
			  SENDER_U, SENDER_C));
	expect_starter(STARTER_U, STARTER_C);

	end = econn_message_alloc();
	econn_message_init(end, ECONN_CONF_END, ccall->convid_hash);
	ASSERT_EQ(0, recv(end, SENDER_U, SENDER_C));

	EXPECT_TRUE(ccall->starter_userid == nullptr);
	EXPECT_TRUE(ccall->starter_clientid == nullptr);
}
