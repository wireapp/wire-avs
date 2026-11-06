/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <time.h>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>
#include "ztest.h"

namespace {

struct ConfigReplay;

struct Client {
	ConfigReplay *test;
	WUSER_HANDLE handle = WUSER_INVALID_HANDLE;
	const char *user;
	const char *client;
};

struct ConfigReplay {
	Client caller{this, WUSER_INVALID_HANDLE, "caller", "caller-client"};
	Client callee{this, WUSER_INVALID_HANDLE, "callee", "callee-client"};
	unsigned caller_messages = 0;
	unsigned callee_closes = 0;
};

static int config_req_handler(WUSER_HANDLE, void *)
{
	/* The test supplies configuration after SETUP and CANCEL are queued. */
	return 0;
}

static void ready_handler(int, void *arg)
{
	auto *client = static_cast<Client *>(arg);
	if (client != &client->test->caller)
		return;

	ASSERT_EQ(0, wcall_start(client->handle, "conversation",
				 WCALL_CALL_TYPE_NORMAL,
				 WCALL_CONV_TYPE_ONEONONE, 0, 0));
}

static int send_handler(void *ctx, const char *convid,
			const char *userid_self, const char *clientid_self,
			const char *, const char *,
			const uint8_t *data, size_t len, int, int, void *arg)
{
	auto *client = static_cast<Client *>(arg);
	auto *test = client->test;
	const uint32_t now = time(nullptr);

	wcall_resp(client->handle, 200, "", ctx);
	if (client != &test->caller)
		return 0;

	EXPECT_EQ(0, wcall_recv_msg(test->callee.handle, data, len, now, now,
				    convid, userid_self, clientid_self,
				    WCALL_CONV_TYPE_ONEONONE, 0));

	++test->caller_messages;
	if (test->caller_messages == 1) {
		wcall_end(client->handle, convid);
	}
	else if (test->caller_messages == 2) {
		wcall_config_update(test->callee.handle, 0, "{}");
	}

	return 0;
}

static void close_handler(int reason, const char *, uint32_t,
			  const char *, const char *, void *arg)
{
	auto *client = static_cast<Client *>(arg);
	if (client != &client->test->callee)
		return;

	EXPECT_EQ(WCALL_REASON_CANCELED, reason);
	++client->test->callee_closes;
	re_cancel();
}

class WcallConfigReplay : public testing::Test {
protected:
	ConfigReplay test;

	void SetUp() override
	{
		ASSERT_EQ(0, ztest_set_ulimit(512));
		ASSERT_EQ(0, flowmgr_init("audummy"));
		msystem_enable_kase(flowmgr_msystem(), true);
		ASSERT_EQ(0, wcall_init(0));
	}

	void TearDown() override
	{
		wcall_close();
		flowmgr_close();
	}

	static void create(Client &client)
	{
		client.handle = wcall_create_ex(
			client.user, client.client, 0, "voe", ready_handler,
			send_handler, nullptr, nullptr, nullptr, nullptr, nullptr,
			close_handler, nullptr, config_req_handler, nullptr, nullptr,
			&client);
		ASSERT_NE(WUSER_INVALID_HANDLE, client.handle);
	}
};

TEST_F(WcallConfigReplay, appliesCancelQueuedDuringConfigRefresh)
{
	create(test.callee);
	create(test.caller);

	wcall_config_update(test.caller.handle, 0, "{}");

	EXPECT_EQ(0, re_main_wait(5000));
	EXPECT_EQ(2u, test.caller_messages);
	EXPECT_EQ(1u, test.callee_closes);
}

} // namespace
