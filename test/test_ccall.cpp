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
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "../src/ccall/ccall.h"
}

#define KEYSZ (32)

/* A call config with ICE servers but no SFT: a conference call
 * started with it ends right away (ENOTSUP) and the ccall goes IDLE,
 * as after any call that ended in it.
 */
static const char cfg_no_sft[] =
	"{\"ice_servers\":[{\"urls\":[\"turn:127.0.0.1:3478\"],"
	"\"username\":\"user\",\"credential\":\"cred\"}],"
	"\"ttl\":3600}";

/* The test answers the config requests itself, with config_update */
static int cfg_req_handler(void *arg)
{
	(void)arg;
	return 0;
}

class CcallTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		ASSERT_EQ(0, config_alloc(&cfg, cfg_req_handler, NULL, NULL));
		ASSERT_EQ(0, ccall_alloc(&ccall, NULL,
					 "conv@domain", "user@domain", "client",
					 true, false));
		ASSERT_EQ(0, ccall_set_config(ccall, cfg));
		icall = ccall_get_icall(ccall);
		ks = ccall_get_keystore(ccall);
		ASSERT_TRUE(ks != NULL);

		memset(epoch_key, 0x5a, sizeof(epoch_key));
	}

	virtual void TearDown() override
	{
		mem_deref(ccall);
		mem_deref(cfg);
	}

	/* Media key the ccall's keystore derives from epoch_key */
	void media_key(uint8_t *mkey)
	{
		ASSERT_EQ(0, keystore_set_session_key(ks, 1, epoch_key,
						      KEYSZ));
		ASSERT_EQ(0, keystore_get_media_key(ks, 1, mkey, KEYSZ));
	}

	/* Media key a keystore without salt derives from epoch_key */
	void unsalted_media_key(uint8_t *mkey)
	{
		struct keystore *ref = NULL;

		/* MLS calls do not hash forward (see ccall_alloc) */
		ASSERT_EQ(0, keystore_alloc(&ref, false));
		ASSERT_EQ(0, keystore_set_session_key(ref, 1, epoch_key,
						      KEYSZ));
		ASSERT_EQ(0, keystore_get_media_key(ref, 1, mkey, KEYSZ));
		mem_deref(ref);
	}

protected:
	struct config *cfg = NULL;
	struct ccall *ccall = NULL;
	struct icall *icall = NULL;
	struct keystore *ks = NULL;
	uint8_t epoch_key[KEYSZ];
};


/* A ccall that a call ended in: the keystore reset going IDLE clears the
 * salt, and the next outgoing call must set it again. Without the salt
 * the caller's media keys don't match those of the members joining with
 * its CONFSTART secret, and no frame decrypts either way.
 */
TEST_F(CcallTest, restart_after_ended_call_salts_media_keys)
{
	uint8_t unsalted[KEYSZ];
	uint8_t first[KEYSZ];
	uint8_t second[KEYSZ];

	unsalted_media_key(unsalted);

	/* First call: starts, then ends for want of an SFT */
	ASSERT_EQ(0, ccall_start(icall, ICALL_CALL_TYPE_NORMAL, false, false));
	media_key(first);
	ASSERT_NE(0, memcmp(first, unsalted, KEYSZ));

	ASSERT_EQ(0, config_update(cfg, 0, cfg_no_sft, sizeof(cfg_no_sft) - 1));

	/* Second call on the same ccall */
	ASSERT_EQ(0, ccall_start(icall, ICALL_CALL_TYPE_NORMAL, false, false));
	media_key(second);

	ASSERT_NE(0, memcmp(second, unsalted, KEYSZ))
		<< "media keys derived without salt";
	ASSERT_NE(0, memcmp(second, first, KEYSZ))
		<< "the second call reused the first call's secret";
}


/* Call starter tracking from CONFSTART/CONFCHECK. No SFT or media is used. */
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
