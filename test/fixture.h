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
#ifndef FIXTURE_H__
#define FIXTURE_H__ 1


#include "gtest/gtest.h"


class RestTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		struct sa dns_srv;

		tmr_init(&tmr_watchdog);


#if 1
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		backend = new FakeBackend;

		err = sa_set_str(&dns_srv, "127.0.0.1", 53); /* dummy */
		ASSERT_EQ(0, err);

		err = dnsc_alloc(&dnsc, NULL, &dns_srv, 1);
		ASSERT_EQ(0, err);

		err = http_client_alloc(&http_cli, dnsc);
		ASSERT_EQ(0, err);

		err = http_client_alloc(&http_cli_ws, dnsc);
		ASSERT_EQ(0, err);

		err = rest_client_alloc(&rest_cli, http_cli, backend->uri,
					NULL, 2, NULL);
		ASSERT_EQ(0, err);

		/* create the global Websock instance */
		err = websock_alloc(&websock, websock_shutdown_handler, 0);
		ASSERT_EQ(0, err);

		jobjc = 0;
		memset(jobjv, 0, sizeof(jobjv));
	}

	virtual void TearDown() override
	{
		ASSERT_EQ(0, err);

		for (unsigned i=0; i<jobjc; i++) {
			mem_deref(jobjv[i]);
			jobjv[i] = 0;
		}

		delete backend;
		tmr_cancel(&tmr_watchdog);
		mem_deref(websock);
		mem_deref(login);
		mem_deref(rest_cli);
		mem_deref(http_cli_ws);
		mem_deref(http_cli);
		mem_deref(dnsc);
	}

	void request(const char *method, const char *path,
		     const char *body = NULL)
	{
		err = rest_request(NULL, rest_cli, 0, method,
				   rest_resp_handler, this,
				   path, body);
		ASSERT_EQ(0, err);

		wait();
	}

	static void tmr_watchdog_handler(void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);
		(void)restTest;
		re_fprintf(stderr, "** WATCHDOG TIMER timed out**\n");

		re_cancel();
		ASSERT_TRUE(false);
	}

	void wait()
	{
		tmr_start(&tmr_watchdog, 5000, tmr_watchdog_handler, this);

		/* run the main-loop, stopped by re_cancel() */
		re_main(NULL);

		tmr_cancel(&tmr_watchdog);
	}

	void handle_response(const struct http_msg *msg, struct mbuf *mb,
			     struct json_object *jobj)
	{
		ASSERT_TRUE(jobj != NULL);

		tmr_cancel(&tmr_watchdog);

#if 0
		re_printf("------- response -------\n");
		re_printf("%b", mb->buf, mb->end);
		re_printf("------------------------\n");
#endif

		/* save vital info from the response */
		scode = msg->scode;
		content_length = mbuf_get_left(mb);

		/* save JSON response for later processing */
		jobjv[jobjc++] = (struct json_object *)mem_ref(jobj);

		re_cancel();
	}


	static void rest_resp_handler(int err, const struct http_msg *msg,
				      struct mbuf *mb,
				      struct json_object *jobj, void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);

		if (err || !msg || msg->scode >= 300) {
			restTest->err = err;
			restTest->scode = msg ? msg->scode : 599;

			re_cancel();
			return;
		}

		ASSERT_TRUE(mb != NULL);

		restTest->handle_response(msg, mb, jobj);
	}

	static void login_handler(int err, const struct login_token *token,
				  void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);

		restTest->loginh_called++;

		ASSERT_EQ(0, err);

		ASSERT_GT(token->expires_in, 0);
		ASSERT_TRUE(str_isset(token->access_token));
		ASSERT_STREQ("Bearer", token->token_type);

		rest_client_set_token(restTest->rest_cli, token);

		re_cancel();
	}

	static void websock_shutdown_handler(void *arg)
	{
		(void)arg;

		re_cancel();
	}

	void subscribe()
	{
		ASSERT_TRUE(nevent == NULL);

		/* Subscribe to Notification Events */
		err = nevent_alloc(&nevent, websock,
				   http_cli_ws, backend->uri, "abc-123",
				   nevent_estab_handler,
				   nevent_recv_handler,
				   nevent_close_handler, this);
		ASSERT_EQ(0, err);

		/* start async traffic, stopped by re_cancel() */
		wait();
	}

	static void nevent_estab_handler(void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);

		restTest->nevent_estab_called++;

#if 1
		re_cancel();
#endif
	}

	static void nevent_recv_handler(struct json_object *jobj, void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);

		restTest->nevent_recv_called++;

		re_cancel();
	}

	static void nevent_close_handler(int err, void *arg)
	{
		RestTest *restTest = static_cast<RestTest *>(arg);
		re_printf("nevent closed (%m)\n", err);

		if (err) {
			re_printf("test: nevent establish error (%m)\n", err);
			restTest->err = err;
			re_cancel();
			return;
		}
	}

	void queued_requests(int count, const char *method, const char *path,
			     int* pending)
	{
		*pending = count;

		for (int i = 0; i < count; ++i) {
			err = rest_request(NULL, rest_cli, 0, method,
					   queued_resp_handler, pending,
					   path, NULL);
			ASSERT_EQ(0, err);
		}

		wait();
	}

	static void queued_resp_handler(int err, const struct http_msg *msg,
					struct mbuf *mb,
					struct json_object *jobj, void *arg)
	{
		int *pending = (int *) arg;

		ASSERT_EQ(0, err);

		--(*pending);

		if (*pending == 0)
			re_cancel();
	}


protected:
	FakeBackend *backend;
	struct dnsc *dnsc = NULL;
	struct http_cli *http_cli = NULL;
	struct http_cli *http_cli_ws = NULL;
	struct rest_cli *rest_cli = NULL;
	struct login *login = NULL;
	struct websock *websock = NULL;
	struct nevent *nevent = NULL;
	struct tmr tmr_watchdog;

	int err = 0;
	uint16_t scode = 0;
	size_t content_length = 0;

	unsigned loginh_called = 0;
	unsigned nevent_estab_called = 0;
	unsigned nevent_recv_called = 0;

	struct json_object *jobjv[64];
	unsigned jobjc;
};


#endif
