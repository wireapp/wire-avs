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
#include <gtest/gtest.h>
#include "ztest.h"


/*
 * List of use-cases:
 *
 * [ ok ] FLOW-001, User A to User B
 * [ ok ] FLOW-002, User A to User B with Hangup
 * [ ok ] FLOW-003, 2 party call with CANCEL

 * [ ok ] FLOW-005 2 Party Call: Timeout after Disconnect

 * [ ok ] page 10, Timeout with no Hangup
 * [    ] page 11, A abandons calls, B answers
 * [ ok ] page 12, A calls B at same time B calls A
 * [    ] page 13, A abandons calls, B calls back
 * [    ] page 14, A abandons calls, no Hangup, B calls back
 * [    ] page 15, A abandons calls, Hangup lost, A calls back
 * [    ] page 16, A disconnect no Hangup, then reconnect
 * [    ] page 17, A disconnect without Hangup
 * [ ok ] page 18, A and B disconnect without Hangup
 *
 * [ ok ] add test to simulate transport error
 */


/*
 * These testcases involves multiple clients and a BE mock
 * that fan's out messages. A single 2-party call and a
 * single conversation between User-A and User-B is assumed.
 *
 *
 *  .----.  .----.        .----.        .----.  .----.
 *  | A1 |  | A2 |        | BE |        | B1 |  | B2 |
 *  '----'  '----'        '----'        '----'  '----'
 *     |      |              |             |      |
 *     |-------------------->|             |      |
 *     |      |              |             |      |
 *     |      |<-------------|             |      |
 *     |      |              |------------>|      |
 *     |      |              |             |      |
 *     |      |              |------------------->|
 *     |      |              |             |      |
 */


enum action {
	ACTION_NOTHING = 0,
	ACTION_ANSWER
};

struct client {
	class Econn *fix;
	struct le le;
	struct backend *be;  /* pointer to parent */
	struct econn *econn;
	char userid[64];
	char clientid[32];

	/* The transport is common for all ECONN's */
	struct econn_transp transp;

	unsigned n_conn;
	unsigned n_close;
	int close_err;

	enum action action;
	struct econn_conf conf;
	bool fake_media;

	struct econn_props *props_local;
	struct econn_props *props_remote;
};

struct backend {
	class Econn *fix;
	struct list clientl;  /* struct client */
	struct mqueue *mq;
	unsigned n_setup;
	unsigned n_cancel;
	unsigned re_cancel_msg;
};


static const char dummy_sdp[] = "v=0";

/* prototypes */
static void econn_conn_handler(struct econn *econn,
			       uint32_t msg_time,
			       const char *userid_sender,
			       const char *clientid_sender,
			       uint32_t age,
			       const char *sdp,
			       struct econn_props *props,
			       void *arg);
static void econn_answer_handler(struct econn *conn,
				    bool reset, const char *sdp,
				    struct econn_props *props,
				    void *arg);
static void econn_update_req_handler(struct econn *econn,
                                     const char *userid_sender,
                                     const char *clientid_sender,
                                     const char *sdp,
                                     struct econn_props *props,
				     bool should_reset,
                                     void *arg);
static void econn_update_resp_handler(struct econn *conn,
                                      const char *sdp,
                                      struct econn_props *props, void *arg);
static void econn_close_handler(struct econn *econn, int err,
				uint32_t msg_time, void *arg);
static int transp_send_handler(struct econn *call,
			       struct econn_message *msg, void *arg);
static int datachannel_send(struct econn *conn,
			    struct econn_message *msg, struct client *cli);
static int backend_fanout(struct backend *be,
			  const struct client *sender_cli,
			  struct econn_message *msg);
static struct econn *client_get_econn(const struct client *cli);
static struct client *client_lookup(const struct list *clientl,
				    const char *userid,
				    const char *clientid);
static int client_print(struct re_printf *pf, const struct client *cli);
static int client_name(struct re_printf *pf, const struct client *cli);
static int backend_alloc(struct backend **backendp, class Econn *fix);
static void client_new_econn(struct client *cli);


class Econn : public ::testing::Test {

public:
	virtual void SetUp() override
	{
#if 1
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		/* Create a global BACKEND mock first */
		err = backend_alloc(&be, this);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(be != NULL);

		ASSERT_EQ(0, list_count(&be->clientl));
	}

	virtual void TearDown() override
	{
		mem_deref(be);
	}

public:
	int transp_err = 0;
	unsigned n_answer = 0;
	unsigned n_hangup = 0;

protected:
	struct backend *be = nullptr;
	int err = 0;
};


/*
 * Mock client
 */

static void client_destructor(void *data)
{
	struct client *cli = static_cast<struct client *>(data);

	mem_deref(cli->econn);

	mem_deref(cli->props_remote);
	mem_deref(cli->props_local);
}


static void client_register(struct client **clip, struct backend *be,
			    const char *userid, const char *clientid)
{
	struct client *cli;

	ASSERT_TRUE(be != NULL);
	ASSERT_TRUE(str_isset(userid));
	ASSERT_TRUE(str_isset(clientid));

	cli = client_lookup(&be->clientl, userid, clientid);
	ASSERT_TRUE(cli == NULL);

	cli = (struct client *)mem_zalloc(sizeof(*cli), client_destructor);

	cli->fix = be->fix;
	cli->be = be;

	/* default action is to not answer the call */
	cli->action = ACTION_NOTHING;

	str_ncpy(cli->userid, userid, sizeof(cli->userid));
	str_ncpy(cli->clientid, clientid, sizeof(cli->clientid));

	cli->transp.sendh = transp_send_handler;
	cli->transp.arg = cli;

	cli->conf.timeout_setup = 60000;
	cli->conf.timeout_term  =  5000;

	list_append(&be->clientl, &cli->le, cli);

	if (clip)
		*clip = cli;
}


static void client_start(struct client *cli)
{
	int err;

	ASSERT_TRUE(cli != NULL);

	if (cli->econn &&
	    econn_current_state(cli->econn) == ECONN_PENDING_INCOMING){

		err = econn_answer(cli->econn, dummy_sdp, cli->props_local);
		ASSERT_EQ(0, err);
	}
	else {
		client_new_econn(cli);

		err = econn_start(cli->econn, dummy_sdp, cli->props_local);
		if (cli->fix->transp_err) {
			ASSERT_EQ(cli->fix->transp_err, err);
		}
		else {
			ASSERT_EQ(0, err);
		}
	}
}


/* send CANCEL/HANGUP on all ECONN objects */
static void client_end(struct client *cli)
{
	if (!cli)
		return;

	econn_end(cli->econn);
}


static struct client *client_lookup(const struct list *clientl,
				    const char *userid,
				    const char *clientid)
{
	struct le *le;

	for (le = list_head(clientl) ; le; le = le->next) {

		struct client *cli = (struct client *)le->data;

		if (0 == str_casecmp(userid, cli->userid) &&
		    0 == str_casecmp(clientid, cli->clientid)) {
			return cli;
		}
	}

	return NULL;
}


static struct client *client_other(const struct client *cli)
{
	struct backend *be = cli->be;
	struct le *le;

	for (le = list_head(&be->clientl) ; le; le = le->next) {

		struct client *ocli = (struct client *)le->data;

		if (0 != str_casecmp(ocli->userid, cli->userid)) {
			return ocli;
		}
	}

	return NULL;
}


static bool client_eq(const struct client *a, const struct client *b)
{
	if (!a || !b)
		return false;

	return 0 == str_casecmp(a->userid, b->userid) &&
		0 == str_casecmp(a->clientid, b->clientid);
}


static void client_new_econn(struct client *cli)
{
	int err;

	ASSERT_EQ(NULL, cli->econn);

	err = econn_alloc(&cli->econn, &cli->conf,
			  cli->userid,
			  cli->clientid,
			  &cli->transp,
			  econn_conn_handler,
			  econn_answer_handler,
			  econn_update_req_handler,
			  econn_update_resp_handler,
			  NULL,
			  econn_close_handler,
			  cli);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(cli->econn != NULL);
}


static void client_recv(struct client *cli,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	struct econn *conn = NULL;
	struct le *le;
	int err;

	if (!cli)
		return;

	/* ignore messages from the same userid. */
	if (0 == str_casecmp(cli->userid, userid_sender)) {

		if (msg->msg_type == ECONN_SETUP && msg->resp) {

			/* Received SETUP(r) from other Client.
			 * We must stop the ringing. */
			info("other Client accepted -- stop ringtone\n");

			if (cli->econn &&
		   econn_current_state(cli->econn) == ECONN_PENDING_INCOMING) {

				econn_close(cli->econn, ECANCELED,
					    ECONN_MESSAGE_TIME_UNKNOWN);
			}
			else {
				info("no pending incoming econns\n");
			}
		}
		else {
			info("ignore message from same user (%s)\n",
				  userid_sender);
		}

		return;
	}

	/* if the list of econn's is empty we need to create a new one
	 */

	conn = client_get_econn(cli);
	if (conn) {
		info("find: econn found\n");
	}
	else {
		info("find: econn not found -- create new\n");

		/* only a SETUP Request can create a new ECONN */

		if (msg->msg_type == ECONN_SETUP &&
		    econn_message_isrequest(msg)) {

			client_new_econn(cli);
		}
	}

	/* send the message to ECONN */
	econn_recv_message(cli->econn, userid_sender, clientid_sender, msg);
}


static int client_send_spurious_cancel(struct client *cli,
				       const char *sessid_sender)
{
	struct econn_message msg;
	int err;

	err = econn_message_init(&msg, ECONN_CANCEL, sessid_sender);
	if (err)
		return err;

	err = backend_fanout(cli->be, cli, &msg);
	if (err)
		return err;

	return 0;
}


static int client_name(struct re_printf *pf, const struct client *cli)
{
	if (!cli)
		return 0;

	return re_hprintf(pf, " %s.%s", cli->userid, cli->clientid);
}


static int client_print(struct re_printf *pf, const struct client *cli)
{
	struct le *le;
	int err = 0;

	if (!cli)
		return 0;

	err |= re_hprintf(pf, " %s.%s\n", cli->userid, cli->clientid);
	err |= econn_debug(pf, cli->econn);

	return err;
}


static struct econn *client_get_econn(const struct client *cli)
{
	return cli ? cli->econn : NULL;
}


/* Get the first ECONN, assume that there is only one */
static struct econn *client_econn(const struct client *cli)
{
	if (!cli)
		return NULL;

	return (struct econn *)cli->econn;
}


/*
 * Mock BACKEND
 */


enum {
	MQ_BE_MESSAGE = 1,
	MQ_RE_CANCEL  = 2,
};


struct be_msg {
	const struct client *sender_cli;
	struct econn_message msg;
};


static void backend_destructor(void *data)
{
	struct backend *be = static_cast<struct backend *>(data);

	list_flush(&be->clientl);
	mem_deref(be->mq);
}


static void backend_mqueue_handler(int id, void *data, void *arg)
{
	struct backend *be = (struct backend *)arg;
	struct be_msg *msg = (struct be_msg *)data;
	struct le *le;

	if (id == MQ_RE_CANCEL) {
		re_cancel();
		return;
	}

	if (id != MQ_BE_MESSAGE)
		return;

	/* Count number of messages send via Backend */
	switch (msg->msg.msg_type) {

	case ECONN_SETUP:
		++be->n_setup;
		break;

	case ECONN_CANCEL:
		++be->n_cancel;
		break;

	default:
		assert(false);
		break;
	}

	for (le = be->clientl.head; le; le = le->next) {

		struct client *cli = static_cast<struct client *>(le->data);

		if (client_eq(cli, msg->sender_cli))
			continue;

		info("BE fanout: %s.%s  ---->  %s.%s\n",
			  msg->sender_cli->userid, msg->sender_cli->clientid,
			  cli->userid, cli->clientid);

		/* The backend will append the ClientID of the sender */

		client_recv(cli, msg->sender_cli->userid,
			    msg->sender_cli->clientid, &msg->msg);
	}

	mem_deref(msg);

#if 1
	if (be->re_cancel_msg &&
	    (be->n_setup + be->n_cancel) >= be->re_cancel_msg)
		re_cancel();
#endif
}


static int backend_alloc(struct backend **backendp, class Econn *fix)
{
	struct backend *be;
	int err;

	be = (struct backend *)mem_zalloc(sizeof(*be), backend_destructor);

	be->fix = fix;

	err = mqueue_alloc(&be->mq, backend_mqueue_handler, be);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(be);
	else
		*backendp = be;

	return err;
}


static struct client *backend_find_peer_client(const struct backend *be,
					       const char *userid_local)
{
	struct le *le;

	for (le = be->clientl.head; le; le = le->next) {

		struct client *cli = (struct client *)le->data;

		if (0 == str_casecmp(userid_local, cli->userid))
			continue;

		return cli;
	}

	return NULL;
}


static int backend_fanout(struct backend *be,
			  const struct client *sender_cli,
			  struct econn_message *msg)
{
	struct be_msg *be_msg;
	int err;

	if (!be || !sender_cli)
		return EINVAL;

	if (be->fix->transp_err)
		return be->fix->transp_err;

#if 0
	re_printf("\x1b[35m"
		  "%H\n"
		  "\x1b[;m"
		  ,
		  econn_message_print, msg);
#endif

	be_msg = (struct be_msg *)mem_zalloc(sizeof(*be_msg), 0);
	be_msg->sender_cli = sender_cli;
	be_msg->msg = *msg;

	err = mqueue_push(be->mq, MQ_BE_MESSAGE, be_msg);
	if (err)
		return err;

	return 0;
}


static void backend_dump(const struct backend *be)
{
	struct le *le;

	if (!be)
		return;

	re_printf("Backend status (%u clients):\n",
		  list_count(&be->clientl));

	for (le = be->clientl.head; le; le = le->next) {

		const struct client *cli = (const struct client *)le->data;

		re_printf(" %H\n", client_print, cli);
	}

	re_printf("\n");
}


/*
 * Client Transport
 */


/* SyncEngine / Backend transport */
static int transp_send_handler(struct econn *call,
			       struct econn_message *msg, void *arg)
{
	struct client *cli = (struct client *)arg;
	int err;

	// todo: add function to resolve transport instead

	switch (msg->msg_type) {

	case ECONN_SETUP:
	case ECONN_CANCEL:
		/* Fanout message to all clients except itself */
		err = backend_fanout(cli->be, cli, msg);
		break;

	case ECONN_HANGUP:
		err = datachannel_send(call, msg, cli);
		if (err) {
			warning("test: datachannel_send failed (%m)\n", err);
		}
		break;

	default:
		warning("test: transp_send_handler: message not supported"
			" (%s)\n",
			econn_msg_name(msg->msg_type));
		err = EPROTO;
		break;
	}

	if (err)
		goto out;

 out:
	return err;
}


/* Direct transp via DataChannel */
static int datachannel_send(struct econn *conn,
				 struct econn_message *msg, struct client *cli)
{
	struct backend *be = cli->be;
	class Econn *fix = be->fix;
	struct client *cli_peer;
	struct econn *conn_peer;

	info("DataChannel: send %s %s\n", econn_msg_name(msg->msg_type),
	     msg->resp ? "Response" : "Request");

	switch (msg->msg_type) {

	case ECONN_HANGUP:
		++fix->n_hangup;
		break;

	default:
		warning("test: datachannel_send: unknown message %d\n",
			msg->msg_type);
		return EPROTO;
	}

	if (ECONN_DATACHAN_ESTABLISHED != econn_current_state(conn) &&
	    ECONN_HANGUP_RECV != econn_current_state(conn)) {

		warning("test: chsend: cannot send in wrong state `%s'\n",
			econn_state_name(econn_current_state(conn)));
		return EPROTO;
	}

	/* find the peer client */
	cli_peer = backend_find_peer_client(cli->be, cli->userid);
	if (!cli_peer) {
		info("test: datachannel_send: peer client not found\n");
		return ENOENT;
	}

	/* find the mapped econn for the remote user */
	conn_peer = client_get_econn(cli_peer);
	if (!conn_peer) {
		info("peer econn not found\n");
		return ENOENT;
	}

	econn_recv_message(conn_peer, "na",
			   econn_clientid_remote(conn_peer), msg);

	return 0;
}


/* Incoming call */
static void econn_conn_handler(struct econn *econn,
			       uint32_t msg_time,
			       const char *userid_sender,
			       const char *clientid_sender,
			       uint32_t age,
			       const char *sdp,
			       struct econn_props *props,
			       void *arg)
{
	struct client *cli = (struct client *)arg;

	++cli->n_conn;

	info("[ %H ] incoming call from %s.%s\n", client_name, cli,
		  userid_sender, clientid_sender);

	/* Verify, the state should be an incoming call */
	ASSERT_EQ(ECONN_PENDING_INCOMING, econn_current_state(econn));

	/* We should not receive calls from ourselves */
	ASSERT_TRUE(0 != str_casecmp(cli->userid, userid_sender));

	ASSERT_TRUE(cli->props_remote == NULL);
	cli->props_remote = (struct econn_props *)mem_ref(props);

	switch (cli->action) {

	case ACTION_ANSWER:

		econn_answer(econn, sdp, cli->props_local);

		if (cli->fake_media) {
			econn_set_datachan_established(econn);
		}
		break;

	default:
		break;
	}
}


static void econn_answer_handler(struct econn *conn,
				 bool reset, const char *sdp,
				 struct econn_props *props, void *arg)
{
	struct client *cli = (struct client *)arg;

	info("[ %H ] answer_handler\n", client_name, cli);

	ASSERT_STREQ(dummy_sdp, sdp);

	ASSERT_TRUE(cli->props_remote == NULL);
	cli->props_remote = (struct econn_props *)mem_ref(props);

	if (cli->fake_media) {

		ASSERT_FALSE(econn_can_send_propsync(conn));

		econn_set_datachan_established(conn);

		/* After DataChannel is established, it should be
		   possible to send PROPSYNC */
		ASSERT_TRUE(econn_can_send_propsync(conn));
	}

	++cli->fix->n_answer;

	if (cli->fix->n_answer >= 1)
		re_cancel();
}


static void econn_update_req_handler(struct econn *econn,
				     const char *userid_sender,
				     const char *clientid_sender,
				     const char *sdp,
				     struct econn_props *props,
				     bool should_reset,
				     void *arg)
{
	/* should not be called with the current testcases */
	ASSERT_TRUE(false);
}


static void econn_update_resp_handler(struct econn *conn,
                                 const char *sdp,
                                 struct econn_props *props, void *arg)
{
	/* should not be called with the current testcases */
	ASSERT_TRUE(false);
}


static void econn_close_handler(struct econn *econn, int err,
				uint32_t msg_time, void *arg)
{
	struct client *cli = (struct client *)arg;

	++cli->n_close;
	cli->close_err = err;

	/* Verify, the state should be terminated */
	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(econn));

	if (err) {
		info("[ %H ] econn closed (%m)\n", client_name, cli, err);
	}
	else {
		info("[ %H ] econn closed (normal)\n", client_name, cli);
	}

	mqueue_push(cli->be->mq, MQ_RE_CANCEL, NULL);
}


/*
 * One testcase for each usecase:
 */


TEST_F(Econn, flow001_user_a_to_user_b)
{
	struct client *a1, *b1, *b2;
	struct econn *conn;

	client_register(&a1, be, "A", "1");
	client_register(&b1, be, "B", "1");
	client_register(&b2, be, "B", "2");

	a1->fake_media = true;
	b2->fake_media = true;

	/* User B answers on Client 2 */
	b2->action = ACTION_ANSWER;

	client_start(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify A1 */
	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_close);
	ASSERT_EQ(ECONN_DATACHAN_ESTABLISHED, econn_current_state(a1->econn));
	ASSERT_EQ(ECONN_DIR_OUTGOING, econn_current_dir(a1->econn));

	/* Verify B1 */
	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(ECANCELED, b1->close_err);
	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(b1->econn));
	ASSERT_EQ(ECONN_DIR_INCOMING, econn_current_dir(b1->econn));

	/* Verify B2 */
	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(0, b2->n_close);
	ASSERT_EQ(ECONN_DATACHAN_ESTABLISHED, econn_current_state(b2->econn));
	ASSERT_EQ(ECONN_DIR_INCOMING, econn_current_dir(b2->econn));
}


TEST_F(Econn, flow002_two_party_call_with_hangup)
{
	struct client *a1 = NULL;
	struct client *b1 = NULL;
	struct econn *conn;
	int err;

	/* Register a bunch of clients */
	client_register(&a1, be, "A", "4");
	client_register(&b1, be, "B", "8");

	b1->action = ACTION_ANSWER;
	a1->fake_media = true;
	b1->fake_media = true;

	client_start(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify A */
	ASSERT_EQ(ECONN_DATACHAN_ESTABLISHED, econn_current_state(a1->econn));

	/* Verify B */
	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(ECONN_DATACHAN_ESTABLISHED, econn_current_state(b1->econn));

	/* User A hangs up */
	client_end(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify A */
	ASSERT_EQ(ECONN_HANGUP_SENT, econn_current_state(a1->econn));

	/* Verify B */
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(b1->econn));

	/* Number of HANGUP messages via DataChannel should be
	   either 1 or 2 */
	ASSERT_TRUE(n_hangup >= 1);
}


TEST_F(Econn, flow003_two_party_call_timeout_with_no_hangup)
{
	struct client *a1 = NULL, *a2 = NULL;
	struct client *b1 = NULL, *b2 = NULL;
	int err;

	be->re_cancel_msg = 1;

	client_register(&a1, be, "A", "1");
	client_register(&a2, be, "A", "2");
	client_register(&b1, be, "B", "1");
	client_register(&b2, be, "B", "2");

	client_start(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify A1 */
	ASSERT_EQ(ECONN_PENDING_OUTGOING,
		  econn_current_state(client_econn(a1)));

	/* Verify A2 -- state should not be changed */
	ASSERT_TRUE(NULL == a2->econn);
	ASSERT_EQ(ECONN_IDLE, econn_current_state(client_econn(a2)));

	/* Verify B1 */
	ASSERT_EQ(ECONN_PENDING_INCOMING,
		  econn_current_state(client_econn(b1)));

	/* Verify B2 */
	ASSERT_EQ(ECONN_PENDING_INCOMING,
		  econn_current_state(client_econn(b2)));
}


TEST_F(Econn, page12_a_calls_b_at_same_time_b_calls_a)
{
	struct client *a1, *b2;

	client_register(&a1, be, "A", "1");
	client_register(&b2, be, "B", "2");

	/* Both A and B calls at the same time */
	client_start(a1);
	client_start(b2);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, n_answer);
	ASSERT_EQ(0, n_hangup);

	/* Verify A1 */
	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_close);
	ASSERT_EQ(ECONN_CONFLICT_RESOLUTION, econn_current_state(a1->econn));

	/* Verify B2 */
	ASSERT_EQ(0, b2->n_conn);
	ASSERT_EQ(0, b2->n_close);
	ASSERT_EQ(ECONN_PENDING_OUTGOING, econn_current_state(b2->econn));
}


TEST_F(Econn, verify_initial_states)
{
	struct client *cli = NULL;

	client_register(&cli, be, "A", "1");

	/* Backend */
	ASSERT_EQ(1, list_count(&be->clientl));

	/* Client */
	ASSERT_TRUE(str_isset(cli->userid));
	ASSERT_TRUE(str_isset(cli->clientid));
	ASSERT_EQ(0, cli->n_conn);
	ASSERT_EQ(0, cli->n_close);

	/* Econn */
	client_new_econn(cli);
	ASSERT_TRUE(cli->econn != NULL);
	ASSERT_EQ(ECONN_IDLE, econn_current_state(cli->econn));
	ASSERT_EQ(ECONN_DIR_UNKNOWN, econn_current_dir(cli->econn));
	ASSERT_TRUE(str_isset(econn_sessid_local(cli->econn)));
}


TEST_F(Econn, connect_and_answer)
{
	struct client *cli_a1 = NULL, *cli_b1 = NULL;
	int err;

	/* Register a bunch of clients */
	client_register(&cli_a1, be, "A", "1");
	client_register(&cli_b1, be, "B", "1");

	cli_b1->action = ACTION_ANSWER;

	client_new_econn(cli_a1);

	ASSERT_EQ(ECONN_IDLE, econn_current_state(cli_a1->econn));

	err = econn_start(cli_a1->econn, dummy_sdp, 0);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* NOTE: auto-accept should have created a new econn for B */
	ASSERT_TRUE(cli_b1->econn != NULL);

	/* Verify A */
	ASSERT_EQ(0, cli_a1->n_conn);
	ASSERT_EQ(ECONN_ANSWERED, econn_current_state(cli_a1->econn));

	/* Verify B */
	ASSERT_EQ(1, cli_b1->n_conn);
	ASSERT_EQ(ECONN_ANSWERED, econn_current_state(cli_b1->econn));
}


/*
 * B has multiple clients
 */
TEST_F(Econn, forking_b)
{
	struct client *cli_a1 = NULL;
	struct client *cli_b1 = NULL;
	struct client *cli_b2 = NULL;
	int err;

	be->re_cancel_msg = 1;

	/* Register a bunch of clients */
	client_register(&cli_a1, be, "A", "1");
	client_register(&cli_b1, be, "B", "1");
	client_register(&cli_b2, be, "B", "2");

	client_new_econn(cli_a1);

	err = econn_start(cli_a1->econn, dummy_sdp, 0);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify A */
	ASSERT_EQ(0, cli_a1->n_conn);
	ASSERT_EQ(ECONN_PENDING_OUTGOING, econn_current_state(cli_a1->econn));

	/* Verify B1 */
	ASSERT_EQ(1, cli_b1->n_conn);
	ASSERT_EQ(ECONN_PENDING_INCOMING, econn_current_state(cli_b1->econn));
	ASSERT_EQ(ECONN_DIR_INCOMING, econn_current_dir(cli_b1->econn));

	/* Verify B2 */
	ASSERT_EQ(1, cli_b2->n_conn);
	ASSERT_EQ(ECONN_PENDING_INCOMING, econn_current_state(cli_b2->econn));
	ASSERT_EQ(ECONN_DIR_INCOMING, econn_current_dir(cli_b2->econn));
}


TEST_F(Econn, forking_a)
{
	client_register(0, be, "A", "4");
	client_register(0, be, "A", "5");
	client_register(0, be, "B", "8");

	be->re_cancel_msg = 2;

	/* Both A's clients call B at the "same" time */
	struct client *cli = client_lookup(&be->clientl, "A", "4");
	client_new_econn(cli);
	err = econn_start(cli->econn, dummy_sdp, 0);
	ASSERT_EQ(0, err);

	cli = client_lookup(&be->clientl, "A", "5");
	client_new_econn(cli);
	err = econn_start(cli->econn, dummy_sdp, 0);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* B should have 1 pending ECONN's */
	struct client *b8 = client_lookup(&be->clientl, "B", "8");
	ASSERT_EQ(ECONN_PENDING_INCOMING, econn_current_state(b8->econn));
	ASSERT_EQ(ECONN_DIR_INCOMING, econn_current_dir(b8->econn));
}


TEST_F(Econn, spurious_cancel)
{
	struct client *cli_a1 = NULL;
	struct client *cli_a666 = NULL;
	struct client *cli_b1 = NULL;
	int err;

	be->re_cancel_msg = 2;

	/* Register a bunch of clients */
	client_register(&cli_a1,   be, "A", "1");
	client_register(&cli_a666, be, "A", "666");
	client_register(&cli_b1,   be, "B", "1");

	client_new_econn(cli_a1);

	err = econn_start(cli_a1->econn, dummy_sdp, 0);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(cli_a1->econn != NULL);

	/* send a spurious CANCEL from evil Ax to B */
	{
		const char *sessid_stolen;

		sessid_stolen = econn_sessid_local(cli_a1->econn);
		ASSERT_TRUE(str_isset(sessid_stolen));

		client_send_spurious_cancel(cli_a666, sessid_stolen);

		err = re_main_wait(5000);
		ASSERT_EQ(0, err);
	}

	/* Verify that B state is unchanged */
	ASSERT_EQ(1, cli_b1->n_conn);
	ASSERT_EQ(0, cli_b1->n_close);

	ASSERT_EQ(ECONN_PENDING_OUTGOING, econn_current_state(cli_a1->econn));
	ASSERT_EQ(ECONN_PENDING_INCOMING, econn_current_state(cli_b1->econn));
}


TEST_F(Econn, timeout_a)
{
	struct client *cli_a1 = NULL;
	int err;

	client_register(&cli_a1, be, "A", "1");

	/* set a short timeout */
	cli_a1->conf.timeout_setup = 100;

	client_start(cli_a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* The client's econn should fail with a timeout error */
	ASSERT_EQ(0, cli_a1->n_conn);
	ASSERT_EQ(1, cli_a1->n_close);
	ASSERT_EQ(ETIMEDOUT_ECONN, cli_a1->close_err);

	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(cli_a1->econn));

	ASSERT_EQ(1, be->n_setup);
	ASSERT_EQ(1, be->n_cancel);  /* client should have sent a CANCEL */
}


TEST_F(Econn, flow005_timeout_b)
{
	struct client *a1 = NULL;
	struct client *b1 = NULL;
	int err;

	client_register(&a1, be, "A", "1");
	client_register(&b1, be, "B", "1");

	/* set a short timeout */
	b1->conf.timeout_setup = 100;

	client_start(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* The client's econn should fail with a timeout error */
	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(ETIMEDOUT_ECONN, b1->close_err);
	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(b1->econn));

	ASSERT_EQ(1, be->n_setup);
	ASSERT_EQ(0, be->n_cancel);  /* client should not have sent CANCEL */
}


TEST_F(Econn, transport_error)
{
	struct client *a1 = NULL;
	int err;

	/* we dont want to see the warnings.. */
	log_set_min_level(LOG_LEVEL_ERROR);

	/* Set a simulated Transport-Error */
	transp_err = EIO;

	client_register(&a1, be, "A", "1");

	client_start(a1);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_close);
	ASSERT_EQ(ECONN_TERMINATING, econn_current_state(a1->econn));
	ASSERT_EQ(0, be->n_cancel);  /* client should NOT send a CANCEL */
}


TEST(econn, conflict_resolution)
{
	ASSERT_TRUE(econn_iswinner("B", "1", "A", "1"));
	ASSERT_FALSE(econn_iswinner("A", "1", "B", "1"));

	ASSERT_TRUE(econn_iswinner("A", "2", "A", "1"));
	ASSERT_FALSE(econn_iswinner("A", "1", "A", "2"));

	ASSERT_FALSE(econn_iswinner("193ac375-d7f0-4a0e-a237-e409297c7c9f",
				    "fcf510876f3349e4",
				    "293ac375-d7f0-4a0e-a237-e409297c7c9f",
				    "fcf510876f3349e4"));
}


TEST_F(Econn, properties)
{
	struct client *a1 = NULL;
	struct client *b1 = NULL;
	int err;

	client_register(&a1, be, "A", "1");
	client_register(&b1, be, "B", "1");

	b1->action = ACTION_ANSWER;

	/* Add some properties for testing.. */
	err = econn_props_alloc(&a1->props_local, NULL);
	ASSERT_EQ(0, err);
	err = econn_props_alloc(&b1->props_local, NULL);
	ASSERT_EQ(0, err);

	err = econn_props_add(a1->props_local, "videosend", "yes,please");
	ASSERT_EQ(0, err);
	err = econn_props_add(b1->props_local, "videosend", "no,thanks");
	ASSERT_EQ(0, err);

	client_start(a1);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_close);

	ASSERT_EQ(2, be->n_setup);

	ASSERT_STREQ("yes,please",
		     econn_props_get(b1->props_remote, "videosend"));
	ASSERT_STREQ("no,thanks",
		     econn_props_get(a1->props_remote, "videosend"));
}


TEST(econn, message_is_creator)
{
	struct econn_message msg_setup_req, msg_setup_resp, msg_cancel;
	char userid_a[] = "A";
	char userid_b[] = "B";
	int err;

	err = econn_message_init(&msg_setup_req, ECONN_SETUP, "sessid");
	msg_setup_req.resp = false;
	ASSERT_EQ(0, err);

	err = econn_message_init(&msg_setup_resp, ECONN_SETUP, "sessid");
	msg_setup_resp.resp = true;
	ASSERT_EQ(0, err);

	err = econn_message_init(&msg_cancel, ECONN_CANCEL, "sessid");
	ASSERT_EQ(0, err);

	ASSERT_TRUE(econn_is_creator(userid_b, userid_a, &msg_setup_req));
	ASSERT_FALSE(econn_is_creator(userid_b, userid_a, &msg_setup_resp));
	ASSERT_FALSE(econn_is_creator(userid_a, userid_a, &msg_setup_req));
	ASSERT_FALSE(econn_is_creator("n/a", "also n/a", &msg_cancel));
}


TEST(econn, transp_resolve)
{
	ASSERT_EQ(ECONN_TRANSP_BACKEND, econn_transp_resolve(ECONN_SETUP));
	ASSERT_EQ(ECONN_TRANSP_BACKEND, econn_transp_resolve(ECONN_CANCEL));
	ASSERT_EQ(ECONN_TRANSP_DIRECT, econn_transp_resolve(ECONN_HANGUP));
}
