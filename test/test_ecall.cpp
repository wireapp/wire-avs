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

#define _GNU_SOURCE 1
#include <sys/time.h>
#include <sys/resource.h>

#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include "fakes.hpp"
#include "avs_audio_io.h"
#include "ztest.h"

/*
 * Test-cases for high-level ECALL
 *
 * [ ok ] FLOW-001 2 Party Call: User A to User B
 * [ ok ] FLOW-002 2 Party Call: With HANGUP
 * [ ok ] FLOW-003 2 Party Call: With CANCEL
 * [ ok ] FLOW-004 2 Party Call: Timeout or User Gives Up
 * [    ] FLOW-005 -> ref test_econn
 * [ ok ] FLOW-006 2 Party Call: A Abandons Calls, B Answers
 * [ ok ] FLOW-007 2 Party Call: A Calls B at same time B calls A
 * [ ok ] FLOW-008 2 Party Call: A Abandons Calls, B Calls Back
 *
 * [ ok ] FLOW-014 2 Party Call: A and B Disconnect without Hangup
 */


#define MAX_CLIENTS         4
#define MAX_CONVLOOPS    1024
#define NUM_TURN_SERVERS    4


class Ecall;

enum estab_type {
	ESTAB_MEDIA = 0,
	ESTAB_DATA,
	ESTAB_AUDIO,
};

enum action {
	ACTION_NOTHING = 0,
	ACTION_ANSWER,
	ACTION_ANSWER_AND_END,
	ACTION_END,
	ACTION_TEST_COMPLETE,
	ACTION_DELAY_COMPLETE,
	ACTION_RESTART,
	ACTION_ALERT,
};

enum {
	MQ_BE_MESSAGE = 1,
	MQ_RE_CANCEL  = 2,
};

struct be_msg {
	struct le le;
	struct client *cli_sender;
	char *userid_sender;
	char *msg;
	char convid[64];
};

struct client {
	Ecall *fix;
	struct conv_loop *loop;        /* pointer to conversation */

	struct ecall *ecall;
	const char *userid;
	const char *clientid;

	enum action action_conn;
	enum action action_mestab;
	enum action action_destab;
	enum action action_aestab;
	enum action action_close;

	unsigned n_conn = 0;
	unsigned n_media_estab = 0;
	unsigned n_audio_estab = 0;
	unsigned n_datachan_estab = 0;
	unsigned n_close = 0;
	unsigned n_usr_data_ready = 0;
	int err_close;
	const char *metrics_json;
	char user_data[MAX_USER_DATA_SIZE];
	int user_data_snd_len;
	int user_data_rcv_len;
	int user_data_rcv_files;
	int user_data_snd_files;
	char user_data_snd_file[128];
	char user_data_rcv_file[128];
};

struct conv_loop {
	Ecall *fix;
	struct client clients[MAX_CLIENTS];
	unsigned num_clients;
	char convid[64];
	bool complete;
};


/* prototypes */
void handle_estab_action(struct conv_loop *loop, enum estab_type estabt);


static void convloop_destructor(void *data)
{
	struct conv_loop *loop = (struct conv_loop *)data;

	debug("conv destroyed (%s)\n", loop->convid);
}


static int convloop_alloc(struct conv_loop **loopp, Ecall *fix,
			  size_t num_clients)
{
	struct conv_loop *loop;

	static unsigned counter = 0;  /* NOTE static */

	if (num_clients > MAX_CLIENTS)
		return EINVAL;

	loop = (struct conv_loop *)mem_zalloc(sizeof(*loop),
					      convloop_destructor);

	loop->fix = fix;

	re_snprintf(loop->convid, sizeof(loop->convid),
		    "conv-%08x", ++counter);

	memset(loop->clients, 0, sizeof(loop->clients));
	loop->num_clients = num_clients;

	for (unsigned i=0; i<MAX_CLIENTS; i++) {
		loop->clients[i].fix = fix;
		loop->clients[i].loop = loop;
	}

	*loopp = loop;
	return 0;
}


static void prepare_clients(struct conv_loop *loop)
{
	loop->clients[0].userid   = "A";
	loop->clients[0].clientid = "1";

	loop->clients[1].userid   = "A";
	loop->clients[1].clientid = "2";

	loop->clients[2].userid   = "B";
	loop->clients[2].clientid = "1";

	loop->clients[3].userid   = "B";
	loop->clients[3].clientid = "2";
}


static struct client *convloop_client(struct conv_loop *loop,
				      const char *userid,
				      const char *clientid)
{
	for (size_t i=0; i<MAX_CLIENTS; i++) {

		struct client *cli = &loop->clients[i];

		if (0 == str_casecmp(cli->userid, userid) &&
		    0 == str_casecmp(cli->clientid, clientid))
			return cli;
	}

	return NULL;
}


static unsigned total_conn(const struct conv_loop *loop)
{
	unsigned n_conn = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_conn += cli->n_conn;
	}

	return n_conn;
}


static unsigned total_audio_estab(const struct conv_loop *loop)
{
	unsigned n_estab = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_estab += cli->n_audio_estab;
	}

	return n_estab;
}


static unsigned total_media_estab(const struct conv_loop *loop)
{
	unsigned n_estab = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_estab += cli->n_media_estab;
	}

	return n_estab;
}


static unsigned total_datachan_estab(const struct conv_loop *loop)
{
	unsigned n_estab = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_estab += cli->n_datachan_estab;
	}

	return n_estab;
}

/*
static unsigned total_propsync(const struct conv_loop *loop)
{
	unsigned n = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n += cli->n_propsync;
	}

	return n;
}
*/

static unsigned total_close(const struct conv_loop *loop)
{
	unsigned n_close = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_close += cli->n_close;
	}

	return n_close;
}


static unsigned total_usr_data_ready(const struct conv_loop *loop)
{
	unsigned n_usr_data_ready = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		n_usr_data_ready += cli->n_usr_data_ready;
	}

	return n_usr_data_ready;
}


static unsigned total_usr_data_rcv(const struct conv_loop *loop)
{
	unsigned user_data_rcv_len = 0;

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		user_data_rcv_len += cli->user_data_rcv_len;
	}

	return user_data_rcv_len;
}


static unsigned total_usr_data_rcv_files(const struct conv_loop *loop)
{
	unsigned user_data_rcv_files = 0;
    
	for (unsigned i=0; i<loop->num_clients; i++) {
        
		const struct client *cli = &loop->clients[i];
        
		user_data_rcv_files += cli->user_data_rcv_files;
	}
    
	return user_data_rcv_files;
}

static unsigned total_usr_data_snd_files(const struct conv_loop *loop)
{
	unsigned user_data_snd_files = 0;
    
	for (unsigned i=0; i<loop->num_clients; i++) {
        
		const struct client *cli = &loop->clients[i];
        
		user_data_snd_files += cli->user_data_snd_files;
	}
    
	return user_data_snd_files;
}


static void convloop_dump(const struct conv_loop *loop)
{
	re_fprintf(stderr,
		   "********** ECALL DUMP (%u clients) **********\n",
		   loop->num_clients);

	for (unsigned i=0; i<loop->num_clients; i++) {

		const struct client *cli = &loop->clients[i];

		if (!str_isset(cli->userid))
			continue;

		re_fprintf(stderr, "%s.%s\n",
			   cli->userid, cli->clientid);
		re_fprintf(stderr, "%H", ecall_debug, cli->ecall);
		re_fprintf(stderr, "\n");
	}

	re_fprintf(stderr, "********** ********** **********\n");
}


static void msg_destructor(void *data)
{
	struct be_msg *msg = (struct be_msg *)data;

	list_unlink(&msg->le);
	mem_deref(msg->msg);
	mem_deref(msg->userid_sender);
}


class Ecall : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		struct msystem_config config = {
			.data_channel = true
		};

#if 1
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		tmr_init(&tmr_delay);
		tmr_init(&tmr_restart);

		memset(loopv, 0, sizeof(loopv));

		err = msystem_get(&msys, "audummy", &config);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(msys != NULL);

		for (unsigned i=0; i<ARRAY_SIZE(turn_srvv); i++) {
			turn_srvv[i] = new TurnServer;
			ASSERT_TRUE(turn_srvv[i] != NULL);
		}

		err = mqueue_alloc(&mq, backend_mqueue_handler, this);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		if (!list_isempty(&pendingl)) {
			re_fprintf(stderr,
				   "flushing pending messages: %u\n",
				   list_count(&pendingl));
			list_flush(&pendingl);
		}

		tmr_cancel(&tmr_delay);
		tmr_cancel(&tmr_restart);
		list_flush(&lst);
		mem_deref(msys);
		mem_deref(mq);

		for (unsigned i=0; i<ARRAY_SIZE(turn_srvv); i++) {
			if (turn_srvv[i]) {
				delete turn_srvv[i];
				turn_srvv[i] = NULL;
			}
		}

		for (size_t i=0; i<loopc; i++) {

			mem_deref(loopv[i]);
		}
	}

	void prepare_loops(size_t num_loops, size_t num_clients)
	{
		size_t i;

		ASSERT_TRUE(num_loops <= MAX_CONVLOOPS);

		loopc = num_loops;

		for (i=0; i<num_loops; i++) {

			err = convloop_alloc(&loopv[i], this, num_clients);
			ASSERT_EQ(0, err);
		}
	}

	struct conv_loop *conv_lookup(const char *convid)
	{
		for (size_t i=0; i<loopc; i++) {

			struct conv_loop *loop = loopv[i];

			if (0 == str_casecmp(loop->convid, convid))
				return loop;
		}

		return NULL;
	}

	static void backend_mqueue_handler(int id, void *data, void *arg)
	{
		Ecall *fix = (Ecall *)arg;
		struct be_msg *msg = (struct be_msg *)data;
		struct le *le;
		struct conv_loop *conv;
		unsigned i;

		if (id == MQ_RE_CANCEL) {
			re_cancel();
			return;
		}

		if (id != MQ_BE_MESSAGE)
			return;

		/* find the right conversation */
		conv = fix->conv_lookup(msg->convid);
		if (!conv) {
			warning("conv %s not found\n", msg->convid);
			return;
		}

		for (i=0; i<conv->num_clients; i++) {
			struct client *peer = &conv->clients[i];

			if (peer == msg->cli_sender)
				continue;

			if (!str_isset(peer->userid))
				continue;

			ecall_transp_recv(peer->ecall, 0, 0,
					  msg->userid_sender,
					  msg->cli_sender->clientid, msg->msg);
		}

		mem_deref(msg);
	}

	void test_complete(struct conv_loop *conv, int error)
	{
		conv->complete = true;

		err = error;

		if (error) {
			warning("completed with error: %m\n", error);
			mqueue_push(mq, MQ_RE_CANCEL, NULL);
			return;
		}

		for (size_t i=0; i<loopc; i++) {
			if (!loopv[i]->complete)
				return;
		}

		mqueue_push(mq, MQ_RE_CANCEL, NULL);
	}

	static void tmr_delay_handler(void *arg)
	{
		struct conv_loop *conv = (struct conv_loop *)arg;

		conv->fix->test_complete(conv, 0);
	}

	void handle_conn_action(struct client *cli)
	{
#if 0
		if (cli->action_conn != ACTION_NOTHING) {
			re_printf("[%s.%s] handle action %d\n",
				  cli->userid, cli->clientid,
				  cli->action_conn);
		}
#endif

		switch (cli->action_conn) {

		case ACTION_ANSWER:
			err = ecall_answer(cli->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
			ASSERT_EQ(0, err);
			break;

		case ACTION_END:
			ecall_end(cli->ecall);
			break;

		case ACTION_ANSWER_AND_END:
			err = ecall_answer(cli->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
			ASSERT_EQ(0, err);

			ecall_end(cli->ecall);
			break;

		case ACTION_TEST_COMPLETE:
			test_complete(cli->loop, 0);
			break;

		case ACTION_DELAY_COMPLETE:
			tmr_start(&cli->fix->tmr_delay, 1000,
				  tmr_delay_handler, cli->loop);
			break;

		default:
			break;
		}
	}

	static void conn_handler(struct icall *icall,
				 uint32_t msg_time, const char *userid_sender,
				 bool video_call, bool should_ring, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;
		int err;

		info("[%s.%s] incoming %s call from \"%s\"\n",
		     cli->userid, cli->clientid,
		     video_call ? "video" : "audio",
		     userid_sender);

		++cli->n_conn;

		if (fix->exp_total_conn &&
		    total_conn(loop) >= fix->exp_total_conn) {

			for (unsigned i=0; i<loop->num_clients; i++) {
				struct client *cli0 = &loop->clients[i];
				fix->handle_conn_action(cli0);
			}
		}
		else if (fix->exp_total_conn == 0) {
			fix->handle_conn_action(cli);
		}
	}

	static void audio_estab_handler(struct icall *icall,
					const char *userid, const char *clientid,
					bool update, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		info("[%s.%s] audio estab\n", cli->userid, cli->clientid);

		++cli->n_audio_estab;

		ASSERT_TRUE(NULL != ecall_mediaflow(cli->ecall));

		if (fix->exp_total_audio_estab &&
		    total_audio_estab(loop) >= fix->exp_total_audio_estab) {

			handle_estab_action(loop, ESTAB_AUDIO);
		}
	}

	static void media_estab_handler(struct icall *icall,
					const char *userid, const char *clientid,
					bool update, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		info("[%s.%s] media estab\n", cli->userid, cli->clientid);

		/* XXX: all calls are audio-only for now */
		ASSERT_FALSE(ecall_has_video(cli->ecall));

		++cli->n_media_estab;

		ASSERT_TRUE(NULL != ecall_mediaflow(cli->ecall));

		int err = ecall_media_start(cli->ecall);
		if (err) {
			ecall_end(cli->ecall);
		}

		if (fix->exp_total_media_estab &&
	    total_media_estab(loop) >= fix->exp_total_media_estab) {

			handle_estab_action(loop, ESTAB_MEDIA);
		}
	}

	static void datachan_estab_handler(struct icall *icall,
					   const char *userid, const char *clientid,
					   bool update, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		info("[%s.%s] DC estab\n", cli->userid, cli->clientid);

		++cli->n_datachan_estab;

		if (fix->exp_total_datachan_estab &&
	    total_datachan_estab(loop) >= fix->exp_total_datachan_estab) {

			handle_estab_action(loop, ESTAB_DATA);
		}
	}

	static void user_data_rcv_handler(uint8_t *data, size_t len, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		memcpy(cli->user_data, data, len);
		cli->user_data_rcv_len = len;

		if (total_usr_data_ready(loop) >= fix->exp_total_user_data_ready &&
			total_usr_data_rcv(loop) >= fix->exp_total_user_data_rcv &&
			total_usr_data_rcv_files(loop) >= fix->exp_total_user_data_rcv_files &&
			total_usr_data_snd_files(loop) >= fix->exp_total_user_data_snd_files) {

			fix->test_complete(loop, 0);
		}
	}

	static void user_data_rcv_file_handler(const char *location, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		cli->user_data_rcv_files += 1;

		if (total_usr_data_ready(loop) >= fix->exp_total_user_data_ready &&
			total_usr_data_rcv(loop) >= fix->exp_total_user_data_rcv &&
			total_usr_data_rcv_files(loop) >= fix->exp_total_user_data_rcv_files &&
			total_usr_data_snd_files(loop) >= fix->exp_total_user_data_snd_files) {
            
			fix->test_complete(loop, 0);
		}
	}

	static void snd_handler(void *arg)
	{
		struct client *cli = (struct client *)arg;

		if (cli->user_data_snd_len) {
			ecall_user_data_send(cli->ecall, cli->user_data,
					     cli->user_data_snd_len);
			cli->user_data_snd_len = 0;
		}
	}
    
	static void snd_file_handler(void *arg)
	{
		struct client *cli = (struct client *)arg;

		if (str_isset(cli->user_data_snd_file)) {
			ecall_user_data_send_file(cli->ecall,
						  cli->user_data_snd_file,
						  "dummy.dat", -1);
			memset(cli->user_data_snd_file, 0,
			       sizeof(cli->user_data_snd_file));
		}
	}
    
	static void user_data_snd_file_handler(const char *name,
					       bool success, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;
        
		if (success) {
			cli->user_data_snd_files += 1;
		}
            
		if (total_usr_data_ready(loop) >= fix->exp_total_user_data_ready &&
			total_usr_data_rcv(loop) >= fix->exp_total_user_data_rcv &&
			total_usr_data_rcv_files(loop) >= fix->exp_total_user_data_rcv_files &&
			total_usr_data_snd_files(loop) >= fix->exp_total_user_data_snd_files) {
            
			fix->test_complete(loop, 0);
		}
	}
    
	static void user_data_ready_handler(int size, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		Ecall *fix = cli->fix;

		info("[%s.%s] User data ready\n", cli->userid, cli->clientid);

		++cli->n_usr_data_ready;

		if (str_isset(cli->user_data_snd_file)) {
			tmr_start(&cli->fix->tmr_delay, 10,
					snd_file_handler, cli);
		}
        
		if (cli->user_data_snd_len) {
			tmr_start(&cli->fix->tmr_delay, 10,
					snd_handler, cli);
		}
		if (total_usr_data_ready(loop) >= fix->exp_total_user_data_ready &&
			total_usr_data_rcv(loop) >= fix->exp_total_user_data_rcv &&
			total_usr_data_rcv_files(loop) >= fix->exp_total_user_data_rcv_files &&
			total_usr_data_snd_files(loop) >= fix->exp_total_user_data_snd_files) {
            
			fix->test_complete(loop, 0);
		}
	}

	static void close_handler(int err, const char *metrics_json,
				  struct icall* icall,
				  uint32_t msg_time,
				  const char *userid, const char *clientid,
				  void *arg)
	{
		struct client *cli = (struct client *)arg;
		Ecall *fix = cli->fix;

		info("[%s.%s] close (%m)\n",
		     cli->userid, cli->clientid, err);

		ASSERT_TRUE(cli != NULL);
		ASSERT_TRUE(fix != NULL);

		++cli->n_close;
		cli->err_close = err;
		cli->metrics_json = metrics_json;

		if (fix->exp_total_close == 0
		    ||
		    (fix->exp_total_close &&
		     total_close(cli->loop) >= fix->exp_total_close)) {

			switch (cli->action_close) {

			case ACTION_NOTHING:
				break;

			case ACTION_END:
				ecall_end(cli->ecall);
				break;

			case ACTION_TEST_COMPLETE:
				fix->test_complete(cli->loop, 0);
				break;

			default:
				warning("close: action not possible\n");
				break;
			}
		}
	}

	static int transp_send_handler(const char *userid_sender,
				       struct econn_message *msg, void *arg)
	{
		struct client *cli = (struct client *)arg;
		struct conv_loop *loop = cli->loop;
		struct be_msg *be_msg;
		char *buf;
		Ecall *fix = cli->fix;
		unsigned i;
		int err = 0;

		if (0 != str_casecmp(cli->userid, userid_sender))
			return EPROTO;

#if 0
		re_fprintf(stderr, "\033[1;34m"); /* bright blue */
		re_fprintf(stderr, "- - message from %s.%s - - - - - - \n",
			  cli->userid, cli->clientid);
		re_fprintf(stderr, "%H\n", econn_message_brief, msg);
		re_fprintf(stderr, "- - - - - - - - - - - - - - - - - - \n");
		re_fprintf(stderr, "\x1b[;m");
#endif

		if (fix->transp_err)
			return fix->transp_err;

		be_msg = (struct be_msg *)mem_zalloc(sizeof(*be_msg),
						     msg_destructor);
		if (!be_msg)
			return ENOMEM;

		be_msg->cli_sender = cli;
		err = econn_message_encode(&buf, msg);
		if (err)
			goto out;

		be_msg->msg = buf;

		err |= str_dup(&be_msg->userid_sender, userid_sender);
		str_ncpy(be_msg->convid, loop->convid,
			 sizeof(be_msg->convid));

		list_append(&fix->pendingl, &be_msg->le, be_msg);

		/* Count number of messages send via Backend */
		switch (msg->msg_type) {

		case ECONN_SETUP:
			++fix->n_setup;
			break;

		case ECONN_CANCEL:
			++fix->n_cancel;
			break;

		default:
			//assert(false);
			break;
		}

		err = mqueue_push(fix->mq, MQ_BE_MESSAGE, be_msg);
		if (err)
			goto out;

	out:
		if (err)
			mem_deref(buf);
		return err;
	}

	static void send_fake_cancel_from_b_to_a(struct conv_loop *loop)
	{
		struct econn_message *msg = econn_message_alloc();
		char *str;
		int err;

		err = econn_message_init(msg, ECONN_CANCEL, "123abc");
		ASSERT_EQ(0, err);
		err = econn_message_encode(&str, msg);
		ASSERT_EQ(0, err);

		for (int i=0 ;i<loop->num_clients; i++) {

			if (0 != str_casecmp(loop->clients[0].userid,
					     loop->clients[i].userid)) {

				ecall_transp_recv(loop->clients[0].ecall,
						  0, 0,
					  loop->clients[i].userid,
					  loop->clients[i].clientid,
					  str);
				break;
			}
		}
		mem_deref(msg);
		mem_deref(str);
	}

	void prepare_ecalls(struct conv_loop *loop,
			    bool user_data_channel = false)
	{
		struct ecall_conf conf;
		unsigned i;

		ASSERT_TRUE(loop != NULL);

		memset(&conf, 0, sizeof(conf));
		conf.econf.timeout_setup = 60000;
		conf.econf.timeout_term = 5000;
#if 0
		conf.trace = 1;
#endif

		//prepare_clients(loop);

		ASSERT_TRUE(loop->num_clients >= 2);
		for (i=0 ;i<loop->num_clients; i++) {

			struct client *cli = &loop->clients[i];

			if (!str_isset(cli->userid))
				continue;

			err = ecall_alloc(&cli->ecall, &lst, ICALL_CONV_TYPE_ONEONONE, &conf,
					  msys, loop->convid,
					  cli->userid, cli->clientid);

			ASSERT_EQ(0, err);
			ASSERT_TRUE(cli->ecall != NULL);

			icall_set_callbacks(ecall_get_icall(cli->ecall),
					    transp_send_handler,
					    conn_handler, 
					    NULL,
					    media_estab_handler,
					    audio_estab_handler,
					    datachan_estab_handler,
					    NULL, // media_stopped_handler
					    NULL, // group_changed_handler
					    NULL, // leave_handler
					    close_handler,
					    NULL, // metrics_handler
					    NULL, // vstate_handler
					    NULL, // audiocbr_handler
					    NULL, // quality_handler
					    cli);

			if (user_data_channel) {
				err = ecall_add_user_data(cli->ecall,
					  user_data_ready_handler,
					  user_data_rcv_handler, cli);
				ASSERT_EQ(0, err);
				err = ecall_user_data_register_ft_handlers(
					  cli->ecall, "./test/data",
					  user_data_rcv_file_handler,
					  user_data_snd_file_handler);
				ASSERT_EQ(0, err);
			}
		}

		/* Add a fake TURN-Server */
		for (i=0; i<loop->num_clients; i++) {

			struct client *cli = &loop->clients[i];

			if (!loop->clients[i].ecall)
				continue;

			for (unsigned j=0; j<ARRAY_SIZE(turn_srvv); j++) {
				struct zapi_ice_server turn;

				re_snprintf(turn.url, sizeof(turn.url),
					    "turn:%J",
					    &turn_srvv[j]->addr);
				re_snprintf(turn.username,
					    sizeof(turn.username),
					    "user");
				re_snprintf(turn.credential,
					    sizeof(turn.credential),
					    "pass");
				
				err = ecall_add_turnserver(cli->ecall,
							   &turn);
				ASSERT_EQ(0, err);
			}
		}
	}

	void test_base(struct conv_loop *loop, bool user_data = false)
	{
		prepare_ecalls(loop, user_data);

#if 0
		/* Send a "fake" CANCEL from B to A */
		send_fake_cancel_from_b_to_a(loop);
#endif

		/* Call from A to B */
		err = ecall_start(loop->clients[0].ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
		ASSERT_EQ(0, err);

		verify_debug(loop);
	}

	static int dev_null_printer(const char *p, size_t size, void *arg)
	{
		(void)p;
		(void)size;
		(void)arg;
		return 0;
	}

	void verify_debug(struct conv_loop *loop)
	{
		struct re_printf pf = {dev_null_printer, 0};
		int err;

		for (size_t i=0; i<loop->num_clients; i++) {

			struct client *cli = &loop->clients[i];

			if (cli->ecall) {

				err = ecall_debug(&pf, cli->ecall);
				ASSERT_EQ(0, err);
			}
		}
	}

public:
	struct tmr tmr_restart;

protected:
	TurnServer *turn_srvv[NUM_TURN_SERVERS] = {nullptr};
	struct list lst = LIST_INIT;
	struct msystem *msys = nullptr;
	struct mqueue *mq = nullptr;
	struct tmr tmr_delay;
	int err = 0;
	int transp_err = 0;

	struct conv_loop *loopv[MAX_CONVLOOPS] = {0};
	size_t loopc = 0;

	unsigned exp_total_conn = 0;
	unsigned exp_total_media_estab = 0;
	unsigned exp_total_audio_estab = 0;
	unsigned exp_total_datachan_estab = 0;
	//unsigned exp_total_propsyncs = 0;
	unsigned exp_total_close = 0;
	unsigned exp_total_user_data_ready = 0;
	unsigned exp_total_user_data_rcv = 0;
	unsigned exp_total_user_data_rcv_files = 0;
	unsigned exp_total_user_data_snd_files = 0;

	struct list pendingl = LIST_INIT;

	unsigned n_setup = 0;
	unsigned n_cancel = 0;
};


static void tmr_restart_handler(void *arg)
{
	struct ecall *ecall = (struct ecall *)arg;

	ecall_restart(ecall);
}


void handle_estab_action(struct conv_loop *loop, enum estab_type estabt)
{
	Ecall *fix = loop->fix;
	unsigned i;
	int err;

	ASSERT_TRUE(fix != NULL);

	for (i=0; i<loop->num_clients; i++) {

		struct client *cli = &loop->clients[i];
		enum action *action_estab = NULL;

		switch (estabt) {

		case ESTAB_MEDIA:
			action_estab = &cli->action_mestab;
			break;

		case ESTAB_DATA:
			action_estab = &cli->action_destab;
			break;

		case ESTAB_AUDIO:
			action_estab = &cli->action_aestab;
			break;

		default:
			break;
		}

		if (!action_estab) {
			return;
		}

		switch (*action_estab) {

		case ACTION_ANSWER:
			warning("estab: answer not possible\n");
			break;

		case ACTION_END:
			ecall_end(cli->ecall);
			break;

		case ACTION_TEST_COMPLETE:
			fix->test_complete(loop, 0);
			break;

		case ACTION_RESTART:
			tmr_start(&cli->fix->tmr_restart, 100,
				  tmr_restart_handler, cli->ecall);
			*action_estab = ACTION_NOTHING;
			break;

		default:
			break;
		}
	}
}


/*
 * Test-cases below:
 */


TEST_F(Ecall, check_audio_estabh)
{
	struct client *a1, *b1, *b2;

#if 0
	log_set_min_level(LOG_LEVEL_INFO);
#endif

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b1->action_conn = ACTION_ANSWER;

	exp_total_audio_estab = 2;
	a1->action_aestab = ACTION_END;

	exp_total_close = 2;
	a1->action_close = ACTION_TEST_COMPLETE;
	//b1->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_audio_estab);
	ASSERT_EQ(1, a1->n_close);
	ASSERT_TRUE( a1->metrics_json != NULL);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(1, b1->n_audio_estab);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_TRUE( b1->metrics_json != NULL);

	ASSERT_TRUE( b2->metrics_json != NULL);
}


// this test seems a bit unstable .. disable it for now ..
#if 0
TEST_F(Ecall, user_data)
{
	struct client *a1, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b1->action_conn = ACTION_ANSWER;

	#define SND_BYTES 1024

	a1->user_data_snd_len = SND_BYTES;
	int16_t tmp = 5687;
	for (int i = 0; i < a1->user_data_snd_len; i++) {
		tmp = tmp * 5687;
		a1->user_data[i] = (char)tmp;
	}
	exp_total_user_data_ready = 3;
	exp_total_user_data_rcv = a1->user_data_snd_len;

	test_base(conv, true);

	/* Wait .. */
	err = re_main_wait(60000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(2, a1->n_usr_data_ready);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(1, b1->n_usr_data_ready);

	ASSERT_EQ(0, a1->user_data_snd_len);
	ASSERT_EQ(SND_BYTES, b1->user_data_rcv_len);

	tmp = 0;
	for (int i = 0; i < b1->user_data_snd_len; i++) {
		tmp = a1->user_data[i] - b1->user_data[i];
		if (tmp) {
			break;
		}
	}
	ASSERT_EQ(0, tmp);
}
#endif


TEST_F(Ecall, user_data_file_transfer)
{
	struct client *a1, *b1, *b2;

	prepare_loops(1, 4);
    
	struct conv_loop *conv = loopv[0];
    
	prepare_clients(conv);
    
	conv->clients[1].userid = "";
    
	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);
    
	b1->action_conn = ACTION_ANSWER;
    
	sprintf(a1->user_data_snd_file, "./test/data/near16.pcm");

	//exp_total_user_data_ready = 3;
	exp_total_user_data_rcv_files = 1;
	exp_total_user_data_snd_files = 1;
	//exp_total_user_data_rcv = a1->user_data_snd_len;
    
	test_base(conv, true);
    
	/* Wait .. */
	err = re_main_wait(60000);
	ASSERT_EQ(0, err);
    
	ASSERT_EQ(0, a1->n_conn);
	//ASSERT_EQ(2, a1->n_usr_data_ready);
    
	ASSERT_EQ(1, b1->n_conn);
	//ASSERT_EQ(1, b1->n_usr_data_ready);
}


TEST_F(Ecall, transport_error)
{
	prepare_loops(1, 3);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	/* we dont want to see the warnings.. */
	log_set_min_level(LOG_LEVEL_ERROR);

	exp_total_close = 1;
	conv->clients[0].action_close = ACTION_TEST_COMPLETE;

	transp_err = EIO;
	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, conv->clients[0].n_conn);
	ASSERT_EQ(0, conv->clients[0].n_datachan_estab);
	ASSERT_EQ(1, conv->clients[0].n_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[0].ecall));

	ASSERT_EQ(0, conv->clients[2].n_conn);
	ASSERT_EQ(0, conv->clients[2].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[2].n_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[1].ecall));
}


TEST_F(Ecall, a_calling_b_no_answer)
{
	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	exp_total_conn = 2;
	conv->clients[0].action_conn = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	/* A1 */
	ASSERT_EQ(0, conv->clients[0].n_conn);
	ASSERT_EQ(0, conv->clients[0].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[0].n_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[0].ecall));

	/* B1 */
	ASSERT_EQ(1, conv->clients[2].n_conn);
	ASSERT_EQ(0, conv->clients[2].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[2].n_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[2].ecall));

	/* B2 */
	ASSERT_EQ(1, conv->clients[3].n_conn);
	ASSERT_EQ(0, conv->clients[3].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[3].n_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[3].ecall));
}


TEST_F(Ecall, a_calling_b_and_b1_answer)
{
	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	exp_total_conn = 2;
	conv->clients[3].action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	conv->clients[0].action_destab = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	/* A1 */
	ASSERT_EQ(0, conv->clients[0].n_conn);
	ASSERT_EQ(1, conv->clients[0].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[0].n_close);
	ASSERT_TRUE(ecall_is_answered(conv->clients[0].ecall));

	/* B5 */
	ASSERT_EQ(1, conv->clients[2].n_conn);
	ASSERT_EQ(0, conv->clients[2].n_datachan_estab);
	ASSERT_EQ(1, conv->clients[2].n_close);
	ASSERT_EQ(EALREADY, conv->clients[2].err_close);
	ASSERT_FALSE(ecall_is_answered(conv->clients[2].ecall));

	/* B6 */
	ASSERT_EQ(1, conv->clients[3].n_conn);
	ASSERT_EQ(1, conv->clients[3].n_datachan_estab);
	ASSERT_EQ(0, conv->clients[3].n_close);
	ASSERT_TRUE(ecall_is_answered(conv->clients[3].ecall));
}


/* test forking */
TEST_F(Ecall, a_calling_b_and_both_answer)
{
	struct client *a1, *a2, *b1, *b2;

	/* we dont want to see the warnings.. */
	log_set_min_level(LOG_LEVEL_ERROR);

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b1->action_conn = ACTION_ANSWER;
	b2->action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	/* A1 */
	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_datachan_estab);
	ASSERT_EQ(0, a1->n_close);
	ASSERT_TRUE(ecall_is_answered(a1->ecall));

	/* A2 */
	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);
	ASSERT_FALSE(ecall_is_answered(a2->ecall));

	/* B1 */
	ASSERT_EQ(1, b1->n_conn);
	ASSERT_TRUE(ecall_is_answered(b1->ecall));

	/* B2 */
	ASSERT_EQ(1, b2->n_conn);
	ASSERT_TRUE(ecall_is_answered(b2->ecall));

	/* Depending on timing, either B1 or B2 should have a working call */
	if (b1->n_datachan_estab) {
		ASSERT_EQ(1, b1->n_datachan_estab);
		ASSERT_EQ(0, b2->n_datachan_estab);
	}
	else if (b2->n_datachan_estab) {
		ASSERT_EQ(0, b1->n_datachan_estab);
		ASSERT_EQ(1, b2->n_datachan_estab);
	}
	else {
		ASSERT_TRUE(false);
	}
}


TEST_F(Ecall, no_answer_and_no_dtls_packets)
{
	prepare_loops(1, 3);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[2].action_conn = ACTION_DELAY_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	/*
	 * The remote client did not answer the incoming call.
	 * Verify that the DTLS connection has not started yet.
	 */

	{
		const struct mediaflow *mf;
		const struct mediaflow_stats *mfs;
		mf = ecall_mediaflow(conv->clients[0].ecall);
		ASSERT_TRUE(mf != NULL);
		mfs = mediaflow_stats_get(mf);
		ASSERT_TRUE(mfs != NULL);
		ASSERT_FALSE(mediaflow_is_ready(mf));
		ASSERT_EQ(0, mfs->dtls_pkt_sent);
		ASSERT_EQ(0, mfs->dtls_pkt_recv);
	}

	{
		const struct mediaflow *mf;
		const struct mediaflow_stats *mfs;
		mf = ecall_mediaflow(conv->clients[2].ecall);
		ASSERT_TRUE(mf != NULL);
		mfs = mediaflow_stats_get(mf);
		ASSERT_TRUE(mfs != NULL);
		ASSERT_FALSE(mediaflow_is_ready(mf));
		ASSERT_EQ(0, mfs->dtls_pkt_sent);
		ASSERT_EQ(0, mfs->dtls_pkt_recv);
	}
}


TEST_F(Ecall, hundreds_of_calls_in_parallel)
{
#define NUM_CONV 8
	size_t i;

	/* This is needed for multiple-calls test */
	err = ztest_set_ulimit(512);
	ASSERT_EQ(0, err);

	prepare_loops(NUM_CONV, 4);

	exp_total_datachan_estab = 2;

	for (i=0; i<loopc; i++) {

		struct conv_loop *conv = loopv[i];
		struct client *a1, *b1;

		prepare_clients(conv);

		a1 = convloop_client(conv, "A", "1");
		b1 = convloop_client(conv, "B", "1");
		ASSERT_TRUE(a1 != NULL);
		ASSERT_TRUE(b1 != NULL);

		b1->action_conn = ACTION_ANSWER;

		a1->action_destab = ACTION_END;

		b1->action_close = ACTION_TEST_COMPLETE;

		test_base(conv);
	}

	/* Wait .. */
	err = re_main_wait(60000);
	ASSERT_EQ(0, err);

	/* XXX: should we wait for audio/datachan in this test? */

	for (i=0; i<loopc; i++) {

		struct conv_loop *conv = loopv[i];
		struct client *a1, *a2, *b1, *b2;

		a1 = convloop_client(conv, "A", "1");
		a2 = convloop_client(conv, "A", "2");
		b1 = convloop_client(conv, "B", "1");
		b2 = convloop_client(conv, "B", "2");
		ASSERT_TRUE(a1 != NULL);
		ASSERT_TRUE(a2 != NULL);
		ASSERT_TRUE(b1 != NULL);
		ASSERT_TRUE(b2 != NULL);

		/* A1 -- Outgoing call */
		ASSERT_EQ(0, a1->n_conn);
		ASSERT_EQ(1, a1->n_media_estab);
		ASSERT_EQ(1, a1->n_datachan_estab);
		//ASSERT_GE(a1->n_propsync, 1);
		ASSERT_LE(a1->n_close, 1);     /* zero or one */
		ASSERT_EQ(0, a1->err_close);

		/* A2 -- ignore */
		ASSERT_EQ(0, a2->n_conn);
		ASSERT_EQ(0, a2->n_media_estab);
		ASSERT_EQ(0, a2->n_audio_estab);
		ASSERT_EQ(0, a2->n_datachan_estab);
		//ASSERT_EQ(0, a2->n_propsync);
		ASSERT_EQ(0, a2->n_close);
		ASSERT_EQ(0, a2->err_close);

		/* B1 -- Answering the incoming call */
		ASSERT_EQ(1, b1->n_conn);
		ASSERT_EQ(1, b1->n_media_estab);
		ASSERT_EQ(1, b1->n_datachan_estab);
		//ASSERT_GE(b1->n_propsync, 1);
		ASSERT_EQ(1, b1->n_close);
		ASSERT_EQ(0, b1->err_close);

		/* B2 -- Incoming call, cancelled */
		ASSERT_EQ(1, b2->n_conn);
		ASSERT_EQ(0, b2->n_media_estab);
		ASSERT_EQ(0, b2->n_audio_estab);
		ASSERT_EQ(0, b2->n_datachan_estab);
		//ASSERT_EQ(0, b2->n_propsync);
		ASSERT_EQ(1, b2->n_close);
		ASSERT_EQ(EALREADY, b2->err_close);
	}
}


TEST_F(Ecall, flow001)
{
	struct client *a1, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	/* B2 */
	b2->action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_TEST_COMPLETE;
	b2->action_destab = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_datachan_estab);
	ASSERT_EQ(0, a1->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(EALREADY, b1->err_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(1, b2->n_datachan_estab);
	ASSERT_EQ(0, b2->n_close);

	ASSERT_EQ(2, n_setup);
	ASSERT_EQ(0, n_cancel);
}


/*
 * 2 Party Call: With HANGUP FLOW-002
 */
TEST_F(Ecall, flow002)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	/* B2 */
	b2->action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_END;

	exp_total_close = 2;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_datachan_estab);
	//ASSERT_EQ(1, a1->n_close);
	ASSERT_EQ(0, a1->err_close);

	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(EALREADY, b1->err_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(1, b2->n_datachan_estab);
	ASSERT_EQ(1, b2->n_close);
	ASSERT_EQ(0, b2->err_close);

	ASSERT_EQ(2, n_setup);
	ASSERT_EQ(0, n_cancel);
}


TEST_F(Ecall, flow003)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	/* B2 */
	b2->action_conn = ACTION_ANSWER;

	exp_total_media_estab = 2;
	a1->action_mestab = ACTION_END;

	exp_total_close = 3;
	a1->action_close = ACTION_TEST_COMPLETE;
	b1->action_close = ACTION_TEST_COMPLETE;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

#if 0
	convloop_dump(conv);
#endif

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_media_estab);
	ASSERT_EQ(1, a1->n_close);
	ASSERT_EQ(0, a1->err_close);

	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_media_estab);
	ASSERT_EQ(0, a2->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_media_estab);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_EQ(EALREADY, b1->err_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(1, b2->n_media_estab);
	ASSERT_EQ(1, b2->n_close);
	ASSERT_EQ(ECANCELED, b2->err_close);

	ASSERT_EQ(2, n_setup);
	ASSERT_EQ(1, n_cancel);
}


/*
 * FLOW-004 2 Party Call: Timeout or User Gives Up
 */
TEST_F(Ecall, flow004)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	exp_total_conn = 2;
	a1->action_conn = ACTION_END;

	exp_total_close = 2;
	b1->action_close = ACTION_TEST_COMPLETE;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_media_estab);
	ASSERT_EQ(0, a1->n_datachan_estab);
	//ASSERT_EQ(1, a1->n_close);
	ASSERT_FALSE(ecall_is_answered(a1->ecall));

	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_media_estab);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);
	ASSERT_FALSE(ecall_is_answered(a2->ecall));

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_media_estab);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_FALSE(ecall_is_answered(b1->ecall));

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(0, b2->n_media_estab);
	ASSERT_EQ(0, b2->n_datachan_estab);
	ASSERT_EQ(1, b2->n_close);
	ASSERT_FALSE(ecall_is_answered(b2->ecall));

	ASSERT_EQ(1, n_setup);
	ASSERT_EQ(1, n_cancel);
}


/*
 * FLOW-006 -- 2 Party Call: A Abandons Calls, B Answers
 */
TEST_F(Ecall, flow006)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	exp_total_conn = 2;
	a1->action_conn = ACTION_END;
	b2->action_conn = ACTION_ANSWER;

	exp_total_close = 3;
	a1->action_close = ACTION_TEST_COMPLETE;
	b1->action_close = ACTION_TEST_COMPLETE;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_media_estab);
	ASSERT_EQ(0, a1->n_datachan_estab);
	ASSERT_EQ(1, a1->n_close);

	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_media_estab);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_media_estab);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(0, b2->n_media_estab);
	ASSERT_EQ(0, b2->n_datachan_estab);
	ASSERT_EQ(1, b2->n_close);

	ASSERT_TRUE(n_setup >= 1);
	ASSERT_EQ(1, n_cancel);
}


// TODO: this testcase is not working correctly
TEST_F(Ecall, hangup_before_datachannel)
{
	struct client *a1, *a2, *b1;

#if 0
	log_set_min_level(LOG_LEVEL_INFO);
#endif

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[3].userid = "";

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);

	b1->action_conn = ACTION_ANSWER_AND_END;

	//exp_total_datachan_estab = 2;
	//a1->action_estab = ACTION_END;

	exp_total_close = 1;
	a1->action_close = ACTION_TEST_COMPLETE;
	a2->action_close = ACTION_TEST_COMPLETE;
	b1->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(15000);
	ASSERT_EQ(0, err);

	/* After the test we should end up in a state with no mediaflows */
	const struct mediaflow *mf;
	//mf = ecall_mediaflow(a1->ecall);
	//ASSERT_EQ(NULL, mf);
	mf = ecall_mediaflow(a2->ecall);
	ASSERT_EQ(NULL, mf);
	mf = ecall_mediaflow(b1->ecall);
	ASSERT_EQ(NULL, mf);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(0, a1->n_datachan_estab);
	//ASSERT_EQ(1, a1->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);
}


/*
 * FLOW-007 2 Party Call: A Calls B at same time B calls A (conflict)
 */
TEST_F(Ecall, flow007)
{
	struct client *a1, *b2;

#if 1
	/* silence 487 Role Conflict warnings */
	log_set_min_level(LOG_LEVEL_ERROR);
#endif

	msystem_enable_kase(msys, true);

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";
	conv->clients[2].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_TEST_COMPLETE;

	prepare_ecalls(conv);

	/* Call from A to B */
	err = ecall_start(a1->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
	ASSERT_EQ(0, err);

	err = ecall_start(b2->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
	ASSERT_EQ(0, err);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

#if 0
	convloop_dump(conv);
#endif

	ASSERT_EQ(0, a1->n_conn);            /* NOTE: no incoming call */
	ASSERT_EQ(1, a1->n_media_estab);
	ASSERT_EQ(1, a1->n_datachan_estab);
	ASSERT_EQ(0, a1->n_close);
	ASSERT_TRUE(ecall_is_answered(a1->ecall));

	ASSERT_EQ(0, b2->n_conn);            /* NOTE: no incoming call */
	ASSERT_EQ(1, b2->n_media_estab);
	ASSERT_EQ(1, b2->n_datachan_estab);
	ASSERT_EQ(0, b2->n_close);
	ASSERT_TRUE(ecall_is_answered(b2->ecall));

	ASSERT_EQ(ECONN_DIR_OUTGOING,
		  econn_current_dir(ecall_get_econn(a1->ecall)));
	ASSERT_EQ(ECONN_DIR_OUTGOING,
		  econn_current_dir(ecall_get_econn(b2->ecall)));

	ASSERT_EQ(3, n_setup);
	ASSERT_EQ(0, n_cancel);
}


/*
 * FLOW-007 2 Party Call: A Calls B at same time B calls A
 */
TEST_F(Ecall, flow007_check_audio_estabh)
{
	struct client *a1, *b2;

#if 1
	/* silence 487 Role Conflict warnings */
	log_set_min_level(LOG_LEVEL_ERROR);
#endif

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";
	conv->clients[2].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	exp_total_audio_estab = 2;
	a1->action_aestab = ACTION_TEST_COMPLETE;

	prepare_ecalls(conv);

	/* Call from A to B */
	err = ecall_start(a1->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
	ASSERT_EQ(0, err);

	err = ecall_start(b2->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
	ASSERT_EQ(0, err);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

#if 0
	convloop_dump(conv);
#endif

	ASSERT_EQ(0, a1->n_conn);            /* NOTE: no incoming call */
	ASSERT_EQ(1, a1->n_media_estab);
	ASSERT_EQ(1, a1->n_audio_estab);
	ASSERT_EQ(0, a1->n_close);

	ASSERT_EQ(0, b2->n_conn);            /* NOTE: no incoming call */
	ASSERT_EQ(1, b2->n_media_estab);
	ASSERT_EQ(1, b2->n_audio_estab);
	ASSERT_EQ(0, b2->n_close);

	ASSERT_EQ(ECONN_DIR_OUTGOING,
				econn_current_dir(ecall_get_econn(a1->ecall)));
	ASSERT_EQ(ECONN_DIR_OUTGOING,
				econn_current_dir(ecall_get_econn(b2->ecall)));

	ASSERT_EQ(3, n_setup);
	ASSERT_EQ(0, n_cancel);
}


/*
 * 2 Party Call: A Abandons Calls, B Calls Back FLOW-008
 */
TEST_F(Ecall, flow008)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	/* User A gives up on call */
	exp_total_conn = 2;
	a1->action_conn = ACTION_END;

	exp_total_close = 2;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	// todo: check if we need this?
	b2->ecall = (struct ecall *)mem_deref(b2->ecall);
	prepare_ecalls(conv);

	/* Call from B to A */
	err = ecall_start(b2->ecall, ICALL_CALL_TYPE_NORMAL, false, NULL);
	ASSERT_EQ(0, err);

	exp_total_conn = 4;
	a1->action_conn = ACTION_TEST_COMPLETE;

	/* Wait again .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, a1->n_conn);
	ASSERT_EQ(0, a1->n_media_estab);
	ASSERT_EQ(0, a1->n_datachan_estab);
	//ASSERT_EQ(0, a1->n_close);

	ASSERT_EQ(1, a2->n_conn);
	ASSERT_EQ(0, a2->n_media_estab);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_media_estab);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(0, b2->n_media_estab);
	ASSERT_EQ(0, b2->n_datachan_estab);
	ASSERT_EQ(1, b2->n_close);

	ASSERT_EQ(2, n_setup);
	ASSERT_EQ(1, n_cancel);
}


/*
 * 2 Party Call: A and B Disconnect without Hangup FLOW-014
 */
TEST_F(Ecall, flow014)
{
	struct client *a1, *a2, *b1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	a2 = convloop_client(conv, "A", "2");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(a2 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	/* User B answers on Client 2 */
	b2->action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_TEST_COMPLETE;
	b2->action_destab = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_media_estab);
	ASSERT_EQ(1, a1->n_datachan_estab);
	ASSERT_EQ(0, a1->n_close);

	ASSERT_EQ(0, a2->n_conn);
	ASSERT_EQ(0, a2->n_media_estab);
	ASSERT_EQ(0, a2->n_datachan_estab);
	ASSERT_EQ(0, a2->n_close);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(0, b1->n_media_estab);
	ASSERT_EQ(0, b1->n_datachan_estab);
	ASSERT_EQ(1, b1->n_close);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(1, b2->n_media_estab);
	ASSERT_EQ(1, b2->n_datachan_estab);
	ASSERT_EQ(0, b2->n_close);

	ASSERT_EQ(2, n_setup);
	ASSERT_EQ(0, n_cancel);
}

#if 0
TEST_F(Ecall, propsync)
{
	struct client *a1, *b1;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);

	b1->action_conn = ACTION_ANSWER;

	exp_total_propsyncs = 4;
	a1->action_destab = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(2, a1->n_propsync);
	ASSERT_EQ(2, b1->n_propsync);

	/* change a local prop */

	/* send UPDATE from A1 */
	ASSERT_TRUE(a1->ecall != NULL);
	err = ecall_propsync_request(a1->ecall);
	ASSERT_EQ(0, err);

	/* expect change */

	exp_total_propsyncs = 6;

	/* Wait again .. */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(3, a1->n_propsync);
	ASSERT_EQ(3, b1->n_propsync);
}
#endif

TEST_F(Ecall, ice)
{
	struct client *a1, *b2;

#if 0
	log_set_min_level(LOG_LEVEL_INFO);
#endif

	msystem_enable_kase(msys, true);

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";
	conv->clients[2].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b2->action_conn = ACTION_ANSWER;

	exp_total_datachan_estab = 2;
	a1->action_destab = ACTION_END;

	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_datachan_estab);
	ASSERT_TRUE(ecall_is_answered(a1->ecall));

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(1, b2->n_datachan_estab);
	ASSERT_TRUE(ecall_is_answered(b2->ecall));
}


TEST_F(Ecall, audio_io_error)
{
	struct client *a1, *b1, *b2;

	/* we dont want to see the warnings.. */
	log_set_min_level(LOG_LEVEL_ERROR);

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	audummy_force_error();

	conv->clients[1].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b1 = convloop_client(conv, "B", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b1->action_conn = ACTION_ANSWER;

	exp_total_close = 3;
	a1->action_close = ACTION_TEST_COMPLETE;
	b1->action_close = ACTION_TEST_COMPLETE;
	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(10000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(1, a1->n_close);
	ASSERT_TRUE( a1->metrics_json != NULL);

	ASSERT_EQ(1, b1->n_conn);
	ASSERT_EQ(1, b1->n_close);
	ASSERT_TRUE( b1->metrics_json != NULL);
}


TEST_F(Ecall, restart)
{
	struct client *a1, *b2;

	prepare_loops(1, 4);

	struct conv_loop *conv = loopv[0];

	prepare_clients(conv);

	conv->clients[1].userid = "";
	conv->clients[2].userid = "";

	a1 = convloop_client(conv, "A", "1");
	b2 = convloop_client(conv, "B", "2");
	ASSERT_TRUE(a1 != NULL);
	ASSERT_TRUE(b2 != NULL);

	b2->action_conn = ACTION_ANSWER;

	exp_total_audio_estab = 2;
	a1->action_aestab = ACTION_RESTART;

	exp_total_datachan_estab = 4;
	a1->action_destab = ACTION_END;

	b2->action_close = ACTION_TEST_COMPLETE;

	test_base(conv);

	/* Wait .. */
	err = re_main_wait(60000);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, a1->n_conn);
	ASSERT_EQ(2, a1->n_datachan_estab);

	ASSERT_EQ(1, b2->n_conn);
	ASSERT_EQ(2, b2->n_datachan_estab);
}

