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
#include <string.h>
#include "fakes.hpp"
#include "ztest.h"


#define FM_MSYS "audummy"


/*
  test: only 1 call on a conversation.
  test: "device handover"

 */


struct request {

public:
	request(const char *path_, const char *method_,
		const char *ctype_, const char *content_, size_t clen_)
		: path(path_)
		, method(method_)
	{
		if (ctype_)
			ctype = ctype_;
		if (content_ && clen_)
			content.append(content_, clen_);
	}

	void print()
	{
		std::cout << path << ' ' << method << ' ' <<
			ctype << ' ' << content;
	}

	std::string path;
	std::string method;
	std::string ctype;
	std::string content;
};

static const char empty_json[] = "{}";
static const char flow_resp_json[] =
				 "{"
				 "  \"flows\":["
				 "  ]"
				 "}";

static const char call_config[] =
  "{"
  "  \"ice_servers\" : ["
  "    {"
  "      \"url\" : \"turn:54.155.57.143:3478\","
  "      \"credential\" : \"Gdh6DtpIVGFYXs96cudsaPvFGb0legAYnSd"
                           "46Qt7Df5VxK6463ut2bregT60qMxMM7Lbk"
                           "H6expTqlLMed9g==\","
  "      \"username\":\"d=1444213017.v=1.k=0.t=s.r=zlfocyzsmlxzjfca\""
  "    },"
  "    {"
  "      \"url\" : \"stun:54.155.57.143:3478\","
  "    },"
  "  ]"
  "}";


class FlowmgrTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
#if 1
		log_set_min_level(LOG_LEVEL_INFO);
		log_enable_stderr(false);
#endif

		log_srv = new HttpServer;
		ASSERT_TRUE(log_srv != NULL);

		err = flowmgr_init(FM_MSYS, NULL, CERT_TYPE_ECDSA);
		ASSERT_EQ(0, err);

		flowmgr_enable_loopback(true);

		err = flowmgr_alloc(&fm, request_handler, error_handler,
				    this);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(fm != NULL);

		flowmgr_enable_metrics(fm, false);
	}

	virtual void TearDown() override
	{
		mem_deref(fm);
		flowmgr_close();
		delete log_srv;
	}

	static int request_handler(struct rr_resp *ctx,
				   const char *path, const char *method,
				   const char *ctype,
				   const char *content, size_t clen,
				   void *arg)
	{
		FlowmgrTest *ft = static_cast<FlowmgrTest *>(arg);
		int err;

#if 0
		re_printf("@@ request: %s %s\n", method, path);
#endif

		if (ft->req_err)
			return ft->req_err;

#if 0
		debug_req(path, method, ctype, content, clen);
#endif

		ft->requestv.push_back(std::make_shared<request>
				   (path, method, ctype, content, clen));

		if (ft->no_resp)
			return 0;

		if (0==str_cmp(path, "/calls/config")) {

			err = flowmgr_resp(ft->fm, ft->srv_status,
					   "", "application/json",
					   call_config,
					   strlen(call_config), ctx);
			return err;
		}

		err = flowmgr_resp(ft->fm, ft->srv_status,
				   "", "application/json",
				   flow_resp_json, strlen(flow_resp_json), ctx);

		return err;
	}

	static void error_handler(int err, const char *convid,
				  void *arg)
	{
		FlowmgrTest *ft = static_cast<FlowmgrTest *>(arg);

		warning("flowmgr ERROR: %m\n", err);

		++ft->n_errh;
		ft->last_err = err;
	}

	static void debug_req(const char *path, const char *method,
			      const char *ctype,
			      const char *content, size_t clen)
	{
		re_printf("PATH=%s\n", path);
		re_printf("METHOD=%s\n", method);
		re_printf("Content-Type: %s\n", ctype ? ctype : "None");
		re_printf("Content-Length: %d\n", clen);
		if (clen > 0) {
			re_printf("\t%b\n", content, clen);
		}
		re_printf("\n");
	}

protected:
	struct flowmgr *fm = nullptr;
	int err = 0;
	int req_err = 0;
	int last_err = 0;
	int srv_status = 200;
	bool no_resp = false;

	unsigned n_estabh = 0;
	unsigned n_errh = 0;

	static char convid[];

	std::vector<std::shared_ptr<request> > requestv;

	HttpServer *log_srv = nullptr;
};


char FlowmgrTest::convid[] = "9a088c8f-1731-4794-b76e-42ba57d917e2";


TEST_F(FlowmgrTest, allocate)
{
	ASSERT_TRUE(fm != NULL);

	ASSERT_EQ(0, requestv.size());
	ASSERT_EQ(0, n_estabh);
	ASSERT_EQ(0, n_errh);
}


TEST_F(FlowmgrTest, start_call)
{
	err = flowmgr_user_add(fm, convid, "1", "A");
	ASSERT_EQ(0, err);
	
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, requestv.size());
	ASSERT_EQ(0, n_estabh);
	ASSERT_EQ(0, n_errh);

	request *req = requestv[0].get();

	ASSERT_TRUE(strstr(req->path.c_str(), convid));
	ASSERT_STREQ("POST", req->method.c_str());

	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, start_two_calls_on_same_convid)
{
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, start_many_calls)
{
#define NUM_CALLS 10
	struct {
		char *convid;
		char *userid;
	} testv[NUM_CALLS];
	unsigned i;

	for (i=0; i<NUM_CALLS; i++) {

		err = uuid_v4(&testv[i].convid);
		ASSERT_EQ(0, err);


		err = uuid_v4(&testv[i].userid);
		ASSERT_EQ(0, err);
		

		err = flowmgr_user_add(fm, testv[i].convid,
				       testv[i].userid, NULL);
		ASSERT_EQ(0, err);
		
		err = flowmgr_acquire_flows(fm, testv[i].convid,
					    NULL, NULL, NULL);
					 
		ASSERT_EQ(0, err);
	}

	ASSERT_EQ(NUM_CALLS, requestv.size());

	for (i=0; i<NUM_CALLS; i++) {
		request *req = requestv[i].get();

		ASSERT_TRUE(strstr(req->path.c_str(), testv[i].convid));
		ASSERT_STREQ("POST", req->method.c_str());
	}

	for (i=0; i<NUM_CALLS; i++) {
		flowmgr_release_flows(fm, testv[i].convid);
		mem_deref(testv[i].convid);
		mem_deref(testv[i].userid);
	}
}


#if 0
TEST_F(FlowmgrTest, request_handler_failure)
{
	req_err = ENOSYS;

	err = flowmgr_user_add(fm, convid,  "1", "A");
	ASSERT_EQ(0, err);
	
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(req_err, err);
	
	flowmgr_release_flows(fm, convid);
}
#endif


TEST_F(FlowmgrTest, accept_call)
{
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);
	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, end_call)
{
	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, start_and_end_call)
{
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(req_err, err);

	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, flow_add_event)
{
	const char flow_add_json[] =
		"{"
		"\"conversation\":\"c1ac3155-c865-4da5-b571-5a6004fc3e96\","
		"\"type\":\"call.flow-add\","
		"\"flows\":["
		  "{"
		    "\"creator\":\"b1b4efa0-4204-4fd0-be42-b8b64f0fbdcb\","
		    "\"active\":false,"
                    "\"remote_user\":\"b1b4efa0-4204-4fd0-be42-b8b64f0fbdcb\","
                    "\"sdp_step\":\"pending\","
		    "\"id\":\"49bdf339-9d96-4db2-89bb-f7982e10f96c\","
		    "\"ice_servers\":["
		    "    {"
		    "      \"url\":\"turn:127.0.0.1:3478\","
		    "      \"credential\":\"FMcOwOnvVxm3QEGqs97Yd\","
		    "      \"username\":\"d=1445079310.v=1.k=0.t=s.r=jhvcyfuiozgdgihf\""
		    "    }"
		    "],"
		  "}"
		"]"
		"}";

	int err;
	bool handled;

	err = flowmgr_process_event(&handled, fm, "application/json",
				    flow_add_json, strlen(flow_add_json));
	ASSERT_EQ(0, err);
	ASSERT_TRUE(handled);

	flowmgr_release_flows(fm, "c1ac3155-c865-4da5-b571-5a6004fc3e96");
}


TEST_F(FlowmgrTest, handle_bogus_response)
{
	struct rr_resp *rr = (struct rr_resp *)0x0000beef;

	err = flowmgr_resp(fm, 200, "OK", "application/json",
			   empty_json, strlen(empty_json), rr);
	ASSERT_EQ(ENOENT, err);
}


TEST_F(FlowmgrTest, start_call_with_http_failure)
{
	/* the HTTP server will reply error to all requests */
	srv_status = 500;

	err = flowmgr_user_add(fm, convid,  "1", "A");	
	ASSERT_EQ(0, err);

	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, requestv.size());
	ASSERT_EQ(0, n_estabh);
	ASSERT_EQ(1, n_errh);
	ASSERT_EQ(EPROTO, last_err);

	flowmgr_release_flows(fm, convid);
}


TEST_F(FlowmgrTest, acquire_flow_but_dont_release_flow)
{
	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);
}


TEST_F(FlowmgrTest, acquire_flow_request_but_no_response)
{
	no_resp = true;

	err = flowmgr_acquire_flows(fm, convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, n_estabh);
}


TEST_F(FlowmgrTest, call_config)
{
	struct zapi_ice_server *srvv = NULL;
	size_t srvc = 0;

#if 1
	log_enable_stderr(false);
#endif

	/* verify empty before start */
	srvv = flowmgr_config_iceservers(fm, &srvc);
	ASSERT_TRUE(srvv == NULL);
	ASSERT_EQ(0, srvc);

	err = flowmgr_config_start(fm);
	ASSERT_EQ(0, err);

	/* check request */
	ASSERT_EQ(1, requestv.size());
	request *req = requestv[0].get();
	ASSERT_STREQ("GET", req->method.c_str());
	ASSERT_STREQ("/calls/config", req->path.c_str());

	/* check response and ICE-servers */
	srvv = flowmgr_config_iceservers(fm, &srvc);
	ASSERT_TRUE(srvv != NULL);
	ASSERT_EQ(2, srvc);

	ASSERT_STREQ("turn:54.155.57.143:3478", srvv[0].url);
	ASSERT_STREQ("Gdh6DtpIVGFYXs96cudsaPvFGb0legAYnSd"
		     "46Qt7Df5VxK6463ut2bregT60qMxMM7Lbk"
		     "H6expTqlLMed9g=="
		     ,srvv[0].credential
		     );
	ASSERT_STREQ("d=1444213017.v=1.k=0.t=s.r=zlfocyzsmlxzjfca",
		     srvv[0].username);
	ASSERT_STREQ("stun:54.155.57.143:3478", srvv[1].url);
}
