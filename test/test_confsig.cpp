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

#define SELF_USER   "userid_self"
#define SELF_CLIENT "clientid_self"
#define SESSID      "sessid_local"


static void add_user_handler(const struct userinfo *user, void *arg) {}
static void remove_user_handler(const struct userinfo *user,
				bool call_vstateh, void *arg) {}
static void sync_users_handler(void *arg) {}
static void kg_change_handler(struct userinfo *keygenerator,
			      bool is_me, bool is_first, void *arg) {}
static void vstate_handler(const struct userinfo *user,
			   enum icall_vstate new_vstate, void *arg) {}


struct sent_msg {
	enum econn_msg type;
	bool resp;
	bool my_clients_only;
	uint64_t timestamp;
	uint32_t seqno;
	size_t secretlen;
	std::string sessid;
};


class ConfsigTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		static const struct confsig_handlers h = {
			.phaseh    = phase_handler,
			.leaderh   = leader_handler,
			.sendh     = send_handler,
			.encodeh   = encode_handler,
			.adopth    = adopt_handler,
			.updateh   = update_handler,
			.incomingh = incoming_handler,
			.switchh   = switch_handler,
			.leaveh    = leave_handler,
			.closeh    = close_handler,
			.checkh    = check_handler,
		};
		const uint8_t secret[16] = {1, 2, 3, 4};

		ASSERT_EQ(0, userlist_alloc(&userl, SELF_USER, SELF_CLIENT,
					    add_user_handler,
					    remove_user_handler,
					    sync_users_handler,
					    kg_change_handler,
					    vstate_handler,
					    this));
		ASSERT_EQ(0, confsig_alloc(&sig, userl, true, &h, this));
		ASSERT_EQ(0, confsig_set_secret(sig, secret, sizeof(secret),
						SESSID));
	}

	virtual void TearDown() override
	{
		mem_deref(sig);
		mem_deref(userl);
	}

	struct econn_message *confstart(bool resp, uint64_t ts,
					uint32_t seqno, uint32_t age = 0)
	{
		struct econn_message *msg = econn_message_alloc();
		static uint8_t secret[16] = {9, 9, 9, 9};

		msg->msg_type = ECONN_CONF_START;
		msg->resp = resp;
		msg->time = 1000;
		msg->age = age;
		msg->u.confstart.timestamp = ts;
		msg->u.confstart.seqno = seqno;
		msg->u.confstart.secret = (uint8_t *)mem_alloc(16, NULL);
		memcpy(msg->u.confstart.secret, secret, 16);
		msg->u.confstart.secretlen = 16;

		return msg;
	}

	int recv(struct econn_message *msg, const char *userid = "other")
	{
		int err = confsig_recv(sig, 1000, userid, "client", msg);
		mem_deref(msg);
		return err;
	}

	static enum confsig_phase phase_handler(void *arg)
	{
		return ((ConfsigTest *)arg)->phase;
	}

	static bool leader_handler(void *arg)
	{
		return ((ConfsigTest *)arg)->leader;
	}

	static int send_handler(struct econn_message *msg,
				struct list *targets,
				bool my_clients_only,
				void *arg)
	{
		ConfsigTest *t = (ConfsigTest *)arg;
		struct sent_msg s;

		s.type = msg->msg_type;
		s.resp = msg->resp;
		s.my_clients_only = my_clients_only;
		s.timestamp = 0;
		s.seqno = 0;
		s.secretlen = 0;
		s.sessid = msg->sessid_sender;
		if (msg->msg_type == ECONN_CONF_START) {
			s.timestamp = msg->u.confstart.timestamp;
			s.seqno = msg->u.confstart.seqno;
			s.secretlen = msg->u.confstart.secretlen;
		}
		else if (msg->msg_type == ECONN_CONF_CHECK) {
			s.timestamp = msg->u.confcheck.timestamp;
			s.seqno = msg->u.confcheck.seqno;
			s.secretlen = msg->u.confcheck.secretlen;
		}
		t->sent.push_back(s);

		return 0;
	}

	static int encode_handler(struct econn_message *msg, void *arg)
	{
		((ConfsigTest *)arg)->n_encode++;
		return 0;
	}

	static int adopt_handler(const struct econn_message *msg,
				 const uint8_t *secret,
				 size_t secretlen,
				 void *arg)
	{
		ConfsigTest *t = (ConfsigTest *)arg;

		t->n_adopt++;
		return confsig_set_secret(t->sig, secret, secretlen,
					  "sessid_remote");
	}

	static void update_handler(const struct econn_message *msg, void *arg)
	{
		((ConfsigTest *)arg)->n_update++;
	}

	static void incoming_handler(uint32_t msg_time,
				     const char *userid,
				     const char *clientid,
				     bool should_ring,
				     void *arg)
	{
		ConfsigTest *t = (ConfsigTest *)arg;

		t->n_incoming++;
		t->should_ring = should_ring;
		t->phase = CONFSIG_PHASE_INCOMING;
		confsig_ongoing_start(t->sig);
	}

	static int switch_handler(const struct econn_message *msg, void *arg)
	{
		((ConfsigTest *)arg)->n_switch++;
		return 0;
	}

	static void leave_handler(int reason, uint32_t msg_time, void *arg)
	{
		ConfsigTest *t = (ConfsigTest *)arg;

		t->n_leave++;
		t->leave_reason = reason;
	}

	static void close_handler(uint32_t msg_time, void *arg)
	{
		ConfsigTest *t = (ConfsigTest *)arg;

		t->n_close++;
		t->phase = CONFSIG_PHASE_IDLE;
	}

	static void check_handler(void *arg)
	{
		((ConfsigTest *)arg)->n_check++;
	}

protected:
	struct userlist *userl = NULL;
	struct confsig *sig = NULL;

	enum confsig_phase phase = CONFSIG_PHASE_IDLE;
	bool leader = false;

	std::vector<struct sent_msg> sent;
	int n_encode = 0;
	int n_adopt = 0;
	int n_update = 0;
	int n_incoming = 0;
	int n_switch = 0;
	int n_leave = 0;
	int n_close = 0;
	int n_check = 0;
	bool should_ring = false;
	int leave_reason = -1;
};


TEST_F(ConfsigTest, idle_confstart_rings_and_adopts)
{
	ASSERT_EQ(0, recv(confstart(false, 100, 5)));

	EXPECT_EQ(1, n_adopt);
	EXPECT_EQ(1, n_incoming);
	EXPECT_TRUE(should_ring);
	EXPECT_EQ(1, n_update);
	EXPECT_EQ(100u, confsig_timestamp(sig));
	EXPECT_EQ(5u, confsig_seqno(sig));
	EXPECT_EQ(CONFSIG_PHASE_INCOMING, phase);
}

TEST_F(ConfsigTest, idle_confstart_from_self_or_meeting_does_not_ring)
{
	ASSERT_EQ(0, recv(confstart(false, 100, 5), SELF_USER));
	EXPECT_EQ(1, n_incoming);
	EXPECT_FALSE(should_ring);

	phase = CONFSIG_PHASE_IDLE;
	confsig_set_meeting(sig, true);
	ASSERT_EQ(0, recv(confstart(false, 50, 5)));
	EXPECT_EQ(2, n_incoming);
	EXPECT_FALSE(should_ring);
}

TEST_F(ConfsigTest, old_or_resp_confstart_does_not_ring)
{
	ASSERT_EQ(0, recv(confstart(true, 100, 5)));
	EXPECT_EQ(1, n_incoming);
	EXPECT_FALSE(should_ring);

	phase = CONFSIG_PHASE_IDLE;
	ASSERT_EQ(0, recv(confstart(false, 50, 5, 31)));
	EXPECT_EQ(2, n_incoming);
	EXPECT_FALSE(should_ring);
}

TEST_F(ConfsigTest, expired_confstart_is_ignored)
{
	ASSERT_EQ(0, recv(confstart(false, 100, 5,
				    CONFSIG_CONFSTART_TIMEOUT_S)));
	EXPECT_EQ(0, n_adopt);
	EXPECT_EQ(0, n_incoming);
	EXPECT_EQ(0u, confsig_timestamp(sig));
}

TEST_F(ConfsigTest, confcheck_request_is_ignored)
{
	struct econn_message *msg = econn_message_alloc();

	msg->msg_type = ECONN_CONF_CHECK;
	msg->resp = false;
	msg->u.confcheck.timestamp = 100;
	msg->u.confcheck.seqno = 5;
	ASSERT_EQ(0, recv(msg));
	EXPECT_EQ(0, n_adopt);
	EXPECT_EQ(0, n_incoming);
}

TEST_F(ConfsigTest, active_earlier_remote_switches)
{
	confsig_set_ts(sig, 100, 5);
	phase = CONFSIG_PHASE_ACTIVE;

	ASSERT_EQ(0, recv(confstart(true, 90, 5)));
	EXPECT_EQ(1, n_adopt);
	EXPECT_EQ(1, n_switch);
	EXPECT_EQ(90u, confsig_timestamp(sig));
	EXPECT_EQ(0u, sent.size());
}

TEST_F(ConfsigTest, active_same_ts_lower_seqno_switches)
{
	confsig_set_ts(sig, 100, 5);
	phase = CONFSIG_PHASE_ACTIVE;

	ASSERT_EQ(0, recv(confstart(true, 100, 3)));
	EXPECT_EQ(1, n_switch);
	EXPECT_EQ(3u, confsig_seqno(sig));
}

TEST_F(ConfsigTest, active_later_remote_asserts_when_leader)
{
	confsig_set_ts(sig, 100, 5);
	phase = CONFSIG_PHASE_ACTIVE;

	ASSERT_EQ(0, recv(confstart(true, 110, 5)));
	EXPECT_EQ(0, n_adopt);
	EXPECT_EQ(0, n_switch);
	EXPECT_EQ(0u, sent.size());

	leader = true;
	ASSERT_EQ(0, recv(confstart(true, 110, 5)));
	ASSERT_EQ(1u, sent.size());
	EXPECT_EQ(ECONN_CONF_START, sent[0].type);
	EXPECT_TRUE(sent[0].resp);
	EXPECT_EQ(100u, sent[0].timestamp);
	EXPECT_EQ(1, n_encode);
}

TEST_F(ConfsigTest, joining_switches_only_to_earlier_call)
{
	confsig_set_ts(sig, 100, 5);
	phase = CONFSIG_PHASE_JOINING;

	ASSERT_EQ(0, recv(confstart(true, 110, 5)));
	EXPECT_EQ(0, n_switch);

	ASSERT_EQ(0, recv(confstart(true, 90, 5)));
	EXPECT_EQ(1, n_switch);
}

TEST_F(ConfsigTest, incoming_answered_elsewhere_stops_ringing)
{
	ASSERT_EQ(0, recv(confstart(false, 100, 5)));
	ASSERT_TRUE(should_ring);

	ASSERT_EQ(0, recv(confstart(false, 100, 5), SELF_USER));
	EXPECT_EQ(1, n_leave);
	EXPECT_EQ(ICALL_REASON_ANSWERED_ELSEWHERE, leave_reason);

	/* not ringing anymore: no second leave */
	ASSERT_EQ(0, recv(confstart(false, 100, 5), SELF_USER));
	EXPECT_EQ(1, n_leave);
}

TEST_F(ConfsigTest, incoming_reject_from_self_stops_ringing)
{
	struct econn_message *msg;

	ASSERT_EQ(0, recv(confstart(false, 100, 5)));

	msg = econn_message_alloc();
	msg->msg_type = ECONN_REJECT;
	ASSERT_EQ(0, recv(msg, "other"));
	EXPECT_EQ(0, n_leave);

	msg = econn_message_alloc();
	msg->msg_type = ECONN_REJECT;
	ASSERT_EQ(0, recv(msg, SELF_USER));
	EXPECT_EQ(1, n_leave);
	EXPECT_EQ(ICALL_REASON_STILL_ONGOING, leave_reason);
}

TEST_F(ConfsigTest, confend_closes_incoming_of_same_session)
{
	struct econn_message *msg;

	ASSERT_EQ(0, recv(confstart(false, 100, 5)));

	msg = econn_message_alloc();
	msg->msg_type = ECONN_CONF_END;
	str_ncpy(msg->sessid_sender, "wrong", ECONN_ID_LEN);
	ASSERT_EQ(0, recv(msg));
	EXPECT_EQ(0, n_close);

	msg = econn_message_alloc();
	msg->msg_type = ECONN_CONF_END;
	str_ncpy(msg->sessid_sender, "sessid_remote", ECONN_ID_LEN);
	ASSERT_EQ(0, recv(msg));
	EXPECT_EQ(1, n_close);
}

TEST_F(ConfsigTest, start_sends_confstart)
{
	ASSERT_EQ(0, confsig_start(sig, 200, 7));

	ASSERT_EQ(1u, sent.size());
	EXPECT_EQ(ECONN_CONF_START, sent[0].type);
	EXPECT_FALSE(sent[0].resp);
	EXPECT_EQ(200u, sent[0].timestamp);
	EXPECT_EQ(7u, sent[0].seqno);
	EXPECT_EQ(16u, sent[0].secretlen);
	EXPECT_EQ(SESSID, sent[0].sessid);
	EXPECT_EQ(1, n_encode);

	ASSERT_EQ(0, confsig_end(sig));
	ASSERT_EQ(2u, sent.size());
	EXPECT_EQ(ECONN_CONF_END, sent[1].type);
	EXPECT_EQ(SESSID, sent[1].sessid);
	EXPECT_EQ(1, n_encode);
}

TEST_F(ConfsigTest, check_sends_only_when_active_leader)
{
	confsig_set_ts(sig, 100, 5);

	phase = CONFSIG_PHASE_ACTIVE;
	confsig_check_now(sig);
	EXPECT_EQ(0u, sent.size());
	EXPECT_EQ(0, n_check);

	leader = true;
	confsig_check_now(sig);
	ASSERT_EQ(1u, sent.size());
	EXPECT_EQ(ECONN_CONF_CHECK, sent[0].type);
	EXPECT_TRUE(sent[0].resp);
	EXPECT_EQ(100u, sent[0].timestamp);
	EXPECT_EQ(1, n_check);

	confsig_check_stop(sig);
}

TEST_F(ConfsigTest, stop_others_ringing)
{
	/* nothing pending: nothing sent */
	confsig_stop_others_ringing(sig);
	EXPECT_EQ(0u, sent.size());

	confsig_set_stop_ringing(sig, CONFSIG_STOP_RINGING_REJECTED);
	EXPECT_TRUE(confsig_stop_ringing_pending(sig));
	confsig_stop_others_ringing(sig);
	EXPECT_FALSE(confsig_stop_ringing_pending(sig));
	ASSERT_EQ(1u, sent.size());
	EXPECT_EQ(ECONN_REJECT, sent[0].type);
	EXPECT_TRUE(sent[0].my_clients_only);

	confsig_set_stop_ringing(sig, CONFSIG_STOP_RINGING_ANSWERED);
	confsig_stop_others_ringing(sig);
	ASSERT_EQ(2u, sent.size());
	EXPECT_EQ(ECONN_CONF_START, sent[1].type);
	EXPECT_TRUE(sent[1].resp);
}

TEST_F(ConfsigTest, alloc_validates_handlers)
{
	struct confsig_handlers h;
	struct confsig *s = NULL;

	memset(&h, 0, sizeof(h));
	EXPECT_EQ(EINVAL, confsig_alloc(&s, userl, true, &h, NULL));
	EXPECT_EQ(EINVAL, confsig_alloc(&s, NULL, true, &h, NULL));
}

TEST_F(ConfsigTest, other_messages_are_not_handled)
{
	struct econn_message *msg = econn_message_alloc();

	msg->msg_type = ECONN_SETUP;
	EXPECT_EQ(EPROTO, recv(msg));
}
