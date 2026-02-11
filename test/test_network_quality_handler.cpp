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
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>
#include <unordered_map>
#include "ztest.h"
#include "fakes.hpp"

struct SimpleCall;

struct Client {
	WUSER_HANDLE wuser;
    std::string userId;
    std::string clientId;
    SimpleCall* call;
    int noOfQualityReports;
};

struct SimpleCall {
    Client caller;
    Client callee;
    std::string callId;
    TurnServer turnServer;

    SimpleCall() {
        caller = {WUSER_INVALID_HANDLE, "caller", "123", this};
        callee = {WUSER_INVALID_HANDLE, "callee", "123", this};
        callId = "anImportantCall";
    }
};

class NetworkQuality : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		int err;

		/* This is needed for multiple-calls test */
		err = ztest_set_ulimit(512);
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
	TurnServer turn_srv;
};

static void ready_handler(int version, void *arg)
{
	auto cli = (Client *)arg;
	static const char *convid = "00cc";
	const int conv_type = WCALL_CONV_TYPE_GROUP;

	info("[ %s.%s ] WPB-XXX ready.\n", cli->userId.c_str(), cli->clientId.c_str());

    if (cli->userId == "caller") {
	int err = wcall_start(cli->wuser, convid, WCALL_CALL_TYPE_NORMAL,
				conv_type, 0);
			ASSERT_EQ(0, err);
    }
}

static int send_handler(void *ctx, const char *convid,
			const char *userid_self, const char *clientid_self,
			const char *userid_dest, const char *clientid_dest,
			const uint8_t *data, size_t len, int transient,
		    int my_clients_only,
			void *arg)
{
	auto cli = (Client* )arg;

	info("[ %s.%s ] {%s} WPB-XXX send message from %s.%s ---> %s.%s\n",
		  cli->userId.c_str(), cli->clientId.c_str(),
		  convid,
		  userid_self, clientid_self,
		  userid_dest, clientid_dest);

    std::cout << transient << " " << my_clients_only << std::endl;

    /* Reply with success */
	wcall_resp(cli->wuser, 200, "", ctx);

    const uint32_t curr_time = time(0);
    if (NULL != userid_dest && NULL != clientid_dest) {
        std::cout << "----------- Implement Me (send_handler)" << std::endl;
    } else {
        if (cli->call->callee.userId != userid_self) {
                    std::cout << "----------- Send to callee" << std::endl;
            // send message to callee
    			wcall_recv_msg(cli->call->callee.wuser, data, len,
				       curr_time,
				       curr_time,
				       convid,
				       userid_self,
				       clientid_self,
					   WCALL_CONV_TYPE_CONFERENCE_MLS);
        } 
        if (cli->call->caller.userId != userid_self) {
                                std::cout << "----------- Send to caller" << std::endl;
            // send message to callee
    			wcall_recv_msg(cli->call->caller.wuser, data, len,
				       curr_time,
				       curr_time,
				       convid,
				       userid_self,
				       clientid_self,
					   WCALL_CONV_TYPE_CONFERENCE_MLS);
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

	info("[ %s.%s ] {%s} WPB-XXX incoming call from %s\n",
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

	info("[ %s.%s ] {%s} WPB-XXX audio established with \"%s\"\n",
	     cli->userId.c_str(), cli->clientId.c_str(),
	     convid, userid);
}


static void close_handler(int reason, const char *convid, uint32_t msg_time,
			  const char *userid, const char *clientid, void *arg)
{
	auto cli = (Client *)arg;

	info("[ %s.%s ] {%s} WPB-XXX call closed (%s)\n",
		cli->userId.c_str(), cli->clientId.c_str(),
		convid,
		wcall_reason_name(reason));
}

std::unordered_map<std::string, int> qualityTriggers;

static void wcall_quality_handler(const char *convid,
				  const char *userid,
				  const char *clientid,
				  const char *quality_info,
				  void *arg)
{
    auto cli = (Client *)arg;
	info("[ %s.%s ] {%s} WPB-XXX call_quality report: %s\n",
	     userid, clientid, convid, quality_info);

    if (NULL != cli) {
    const auto numberOfRecords = cli->noOfQualityReports++;
    info("[ %s.%s ] {%s} WPB-XXX call_quality report: %d\n",
	     userid, clientid, convid, numberOfRecords);
    } else {
    const auto numberOfRecords = ++qualityTriggers[userid];
        info("[ %s.%s ] {%s} WPB-XXX call_quality report: %d\n",
            	     userid, clientid, convid, numberOfRecords);
    }


}

/* TODO: add turns url */
static const char *json_config_fmt =
" {"
"  \"ice_servers\" : ["
"    {"
"    \"urls\"       : [\"turns:%J?transport=tcp\"],"
"    \"username\"   : \"user\","
"    \"credential\" : \"secret\""
"    }"
"  ],"
"  \"ttl\":3600"
"  }"
	;


static int config_req_handler(WUSER_HANDLE wuser, void *arg)
{
	auto *cli = (Client *)arg;
	//char *json;
	int err;

	//err = re_sdprintf(&json, json_config_fmt, cli->turnServer->addr_tls);
	//if (err)
	//	return err;

    char emptyCfg[] = "{}";
    info("WPB-XXX config request handler: userid=%s config=%s\n",
	     cli->userId.c_str(), emptyCfg);

	wcall_config_update(wuser, 0, emptyCfg);

	//mem_deref(json);

	return 0;
}

TEST(networkQuality, settingHandlerMultipleTimes)
{
	int err;

	err = wcall_init(0);
	ASSERT_EQ(0, err);

    log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(true);

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

	wcall_close();
}

TEST(networkQuality, getHandlerTriggered)
{
	int err;

    		/* This is needed for multiple-calls test */
		err = ztest_set_ulimit(512);
		ASSERT_EQ(0, err);

		err = flowmgr_init("audummy");
		ASSERT_EQ(0, err);

		msystem_enable_kase(flowmgr_msystem(), true);

	err = wcall_init(0);
	ASSERT_EQ(0, err);

    log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(true);

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

    wcall_set_network_quality_handler(call.caller.wuser,
                        wcall_quality_handler,
                        1,
                        NULL);



    err = re_main_wait(60000);
	ASSERT_EQ(0, err);
    

	wcall_destroy(call.caller.wuser);
    wcall_destroy(call.callee.wuser);

	wcall_close();
}
