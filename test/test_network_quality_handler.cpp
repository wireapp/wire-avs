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

#include <time.h>
#include <unordered_map>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>
#include <unordered_map>
#include "ztest.h"
#include "fakes.hpp"

struct SimpleCall;

enum State {
    Idle,
	Ready,
	Established,
	Closed,
	Down,
};

struct Client {
	WUSER_HANDLE wuser;
    std::string userId;
    std::string clientId;
    SimpleCall* call;
    std::unordered_map<int, int> qualityCallbacks;
	int timerInterval;
	State state;
};

struct ClientWithInterval {
    Client* client;
	int interval;
};

struct SimpleCall {
    Client caller;
    Client callee;
    std::string callId;

    SimpleCall() {
        caller = {WUSER_INVALID_HANDLE, "caller", "123", this};
        callee = {WUSER_INVALID_HANDLE, "callee", "789", this};
        callId = "aCall";
    }
};

static void shutdown_handler(WUSER_HANDLE wuser, void *arg)
{
	auto cli = (Client *)arg;
	info("[ %s.%s ] Shutdown handler is called.\n", cli->userId.c_str(), cli->clientId.c_str());
	cli->state = Down;

	if ((cli->call->caller.state == Down && cli->call->callee.state == Down)){
		info("[ %s.%s ] Shutdown, all participants done\n", cli->userId.c_str(), cli->clientId.c_str());
        re_cancel();
	}
}

static void ready_handler(int version, void *arg)
{
	auto cli = (Client *)arg;
	const int conv_type = WCALL_CONV_TYPE_GROUP;

	cli->state = Ready;

	info("[ %s.%s ] Ready.\n", cli->userId.c_str(), cli->clientId.c_str());

	if (cli->call->caller.state == Ready && cli->call->callee.state == Ready) {
		info("[ %s.%s ] Ready, all participants are ready\n", cli->userId.c_str(), cli->clientId.c_str());
		int err = wcall_start(cli->wuser, cli->call->callId.c_str(), WCALL_CALL_TYPE_NORMAL,
				conv_type, 0, 0);
		ASSERT_EQ(0, err);
    }

	wcall_set_shutdown_handler(cli->wuser, shutdown_handler, arg);
}

static int send_handler(void *ctx, const char *convid,
			const char *userid_self, const char *clientid_self,
			const char *userid_dest, const char *clientid_dest,
			const uint8_t *data, size_t len, int transient,
		    int my_clients_only,
			void *arg)
{
	auto cli = (Client* )arg;

	info("[ %s.%s ] {%s} Send message from %s.%s ---> %s.%s\n",
		  cli->userId.c_str(), cli->clientId.c_str(),
		  convid,
		  userid_self, clientid_self,
		  userid_dest, clientid_dest);

    /* Reply with success */
	wcall_resp(cli->wuser, 200, "", ctx);

    const uint32_t curr_time = time(0);
    if (NULL != userid_dest && NULL != clientid_dest) {
		warning("[ %s.%s ] {%s} Implement Me \n",
            cli->userId.c_str(), cli->clientId.c_str(), convid);
    } else {
        if (cli->call->callee.userId != userid_self) {
            // send message to callee
			wcall_recv_msg(cli->call->callee.wuser, data, len,
				curr_time,
				curr_time,
				convid,
				userid_self,
				clientid_self,
				WCALL_CONV_TYPE_CONFERENCE_MLS,
				false);
        }
        if (cli->call->caller.userId != userid_self) {
            // send message to callee
			wcall_recv_msg(cli->call->caller.wuser, data, len,
				curr_time,
				curr_time,
				convid,
				userid_self,
				clientid_self,
				WCALL_CONV_TYPE_CONFERENCE_MLS,
				false);
        }
    }
    return 0;
}

static void incoming_handler(const char *convid, uint32_t msg_time,
			     const char *userid, const char *clientid, int video_call /*bool*/,
			     int should_ring /*bool*/, int conv_type, /*WCALL_CONV_TYPE...*/
			     void *arg)
{
	auto cli = (Client *)arg;

	info("[ %s.%s ] {%s} Incoming call from %s\n",
		  cli->userId.c_str(), cli->clientId.c_str(),
		  convid,
		  userid);
    auto err = wcall_answer(cli->wuser, convid, WCALL_CALL_TYPE_NORMAL, 0);
	ASSERT_EQ(0, err);
}

/* called when audio is established */
static void estab_handler(const char *convid,
			  const char *userid, const char *clientid, void *arg)
{
	auto cli = (Client *)arg;

	cli->state = Established;

	info("[ %s.%s ] {%s} Media established with \"%s\"\n",
	     cli->userId.c_str(), cli->clientId.c_str(),
	     convid, userid);

    if (cli->call->caller.state == Established && cli->call->callee.state == Established) {
		info("[ %s.%s ] {%s} Media established, all participants are established\n",
			cli->userId.c_str(), cli->clientId.c_str(), convid);
    }
}


static void close_handler(int reason, const char *convid, uint32_t msg_time,
			  const char *userid, const char *clientid, void *arg)
{
	auto cli = (Client *)arg;
	auto closeReason = std::string(wcall_reason_name(reason));

	info("[ %s.%s ] {%s} Closed handler (%s)\n",
		cli->userId.c_str(), cli->clientId.c_str(),
		convid,
		closeReason.c_str());

	if (closeReason == "Normal") {
		cli->state = Closed;
	}

	if (cli->call->caller.state == Closed && cli->call->callee.state == Closed) {
		info("[ %s.%s ] {%s} Closed handler, all participants are closed\n",
			cli->userId.c_str(), cli->clientId.c_str(), convid);

		wcall_destroy(cli->call->caller.wuser);
		wcall_destroy(cli->call->callee.wuser);
    }
}

static void wcall_quality_handler(const char *convid,
				  const char *userid,
				  const char *clientid,
				  int quality, /*  WCALL_QUALITY_ */
				  int rtt, /* round trip time in ms */
				  int uploss, /* upstream pkt loss % */
				  int downloss, /* dnstream pkt loss */
				  void *arg)
{
    auto cli = (Client *)arg;

    info("[ %s.%s ] {%s with interval %d} Call_quality report\n",
	     userid, clientid, convid, cli->timerInterval);

    cli->qualityCallbacks[cli->timerInterval]++;
}


static int config_req_handler(WUSER_HANDLE wuser, void *arg)
{
	auto *cli = (Client *)arg;

    char emptyCfg[] = "{}";
    info("Config request handler: userid=%s config=%s\n",
	     cli->userId.c_str(), emptyCfg);

	wcall_config_update(wuser, 0, emptyCfg);

	return 0;
}

static void set_quality_interval(void *arg)
{
	auto cli = (ClientWithInterval *)arg;

    info("\n\n[ %s.%s ] Setting quality interval : %d\n",
			cli->client->userId.c_str(), cli->client->clientId.c_str(), cli->interval);

	cli->client->timerInterval = cli->interval;
	wcall_set_network_quality_handler(cli->client->wuser,
        wcall_quality_handler, cli->interval, cli->client);
}

static void cleanup_function(void *arg)
{
	auto call = (SimpleCall *)arg;
	wcall_end(call->caller.wuser, call->callId.c_str());
}


class NetworkQuality : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		/* This is needed for multiple-calls test */
		auto err = ztest_set_ulimit(512);
		ASSERT_EQ(0, err);

		err = flowmgr_init("audummy");
		ASSERT_EQ(0, err);

		msystem_enable_kase(flowmgr_msystem(), true);

		err = wcall_init(0);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		wcall_close();
		flowmgr_close();
	}

public:

};

TEST_F(NetworkQuality, settingHandlerMultipleTimes)
{
	WUSER_HANDLE wuser = wcall_create_ex("user", "123", 0, "voe", NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    ASSERT_NE(WUSER_INVALID_HANDLE, wuser);

    std::vector<int> intervals = {1, 2, 0, 1};
    for (const auto& i : intervals) {
        wcall_set_network_quality_handler(wuser,
                        wcall_quality_handler,
                        i,
                        NULL);
    }

	wcall_destroy(wuser);
}

TEST_F(NetworkQuality, checkHandlerForDifferentIntervals)
{
#if 0
	log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(true);
#endif

    SimpleCall call;
    call.callee.wuser = wcall_create_ex(call.callee.userId.c_str(),
                                        call.callee.clientId.c_str(),
                                        0,
                                        "voe",
                                        ready_handler,
                                        send_handler,
                                        NULL,
                                        incoming_handler,
                                        NULL,
                                        NULL,
		                                estab_handler,
                                        close_handler,
                                        NULL,
                                        config_req_handler,
                                        NULL,
                                        NULL,
                                        &call.callee);
    ASSERT_NE(WUSER_INVALID_HANDLE, call.callee.wuser);

	call.caller.wuser = wcall_create_ex(call.caller.userId.c_str(),
                                        call.caller.clientId.c_str(),
                                        0,
                                        "voe",
                                        ready_handler,
                                        send_handler,
                                        NULL,
                                        incoming_handler,
                                        NULL,
                                        NULL,
		                                estab_handler,
                                        close_handler,
                                        NULL,
                                        config_req_handler,
                                        NULL,
                                        NULL,
                                        &call.caller);

    ASSERT_NE(WUSER_INVALID_HANDLE, call.caller.wuser);

	// Set an handler with 1 second interval
	auto timerInterval = 1;
	call.caller.timerInterval = timerInterval;
	wcall_set_network_quality_handler(call.caller.wuser,
                        wcall_quality_handler,
                        timerInterval,
                        &call.caller);

    // And use a timer to set interval to 2 after 4 seconds
	timerInterval = 2;
	struct tmr timer4Sec;
	tmr_init(&timer4Sec);
	uint32_t in4Sec = 4000;
	auto callerWith2secInterval = ClientWithInterval{&call.caller, timerInterval};
    tmr_start(&timer4Sec, in4Sec, set_quality_interval, &callerWith2secInterval);

    // And use a timer to set interval to 0 after 9 seconds
	timerInterval = 0;
	struct tmr timer9Sec;
	tmr_init(&timer9Sec);
	uint32_t in9Sec = 9000;
	auto callerWith0secInterval = ClientWithInterval{&call.caller, timerInterval};
    tmr_start(&timer9Sec, in9Sec, set_quality_interval, &callerWith0secInterval);

    // And use a timer to initiate cleanup after 15 seconds
	struct tmr cleanupTimer;
	tmr_init(&cleanupTimer);
	uint32_t cleanupTime = 15000;
    tmr_start(&cleanupTimer, cleanupTime, cleanup_function, &call);

	// Initiate runtime with a timout of 1 min as fallback in case test fails
	auto err = re_main_wait(60000);

    // Check that 1 sec interval callback is called 4 times during initial 4 sec
	ASSERT_EQ(4, call.caller.qualityCallbacks[1]);

    // 2 sec interval callback is called 2 times (during 4-9 seconds)
	ASSERT_EQ(2, call.caller.qualityCallbacks[2]);

    // And that should be all, we should not have any other callbacks
	ASSERT_EQ(2, call.caller.qualityCallbacks.size());

    // And call is ended successfully
	ASSERT_EQ(0, err);

    tmr_cancel(&cleanupTimer);
	tmr_cancel(&timer4Sec);
	tmr_cancel(&timer9Sec);
}