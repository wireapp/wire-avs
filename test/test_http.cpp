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
#include "fakes.hpp"
#include "ztest.h"


#define BODY_SIZE (32*1024)


#define NUM_REQUEST 20
#define NUM_SERVER  10


class HttpTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		err = dns_init(&dnsc);
		ASSERT_EQ(0, err);

		err = http_client_alloc(&cli, dnsc);
		ASSERT_EQ(0, err);

		mb = mbuf_alloc(1024);
		ASSERT_TRUE(mb != NULL);

		err = mbuf_fill(mb, 0xa5, BODY_SIZE);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		mem_deref(mb);
		mem_deref(req);
		mem_deref(cli);
		mem_deref(dnsc);
		delete srv;

		for (int i=0; i<NUM_REQUEST; i++)
			mem_deref(reqv[i]);
		for (int i=0; i<NUM_SERVER; i++)
			delete srvv[i];
	}

	static void http_resp_handler(int err, const struct http_msg *msg,
				      void *arg)
	{
		HttpTest *ht = static_cast<HttpTest *>(arg);

		++ht->n_resp;
		ht->err_resp = err;

		if (err) {
			warning("HTTP Response: %m\n", err);
			goto out;
		}
		ASSERT_TRUE(msg != NULL);

#if 0
		re_printf("response: %u %r\n", msg->scode, &msg->reason);
#endif

		if (ht->n_request > 0) {
			ht->send_request();
		}

	out:
		if (ht->n_expected_resp) {

			if (ht->n_resp >= ht->n_expected_resp)
				re_cancel();
		}
		else {
			re_cancel();
		}
	}

	void request(bool secure)
	{
		srv = new HttpServer(secure);
		ASSERT_TRUE(srv != NULL);

		re_snprintf(url, sizeof(url), "%s/", srv->url);

		err = http_request(&req, cli, "GET", url,
				   http_resp_handler, NULL, this,
				   "Content-Type: application/x-octet-stream\r\n"
				   "Content-Length: %zu\r\n"
				   "\r\n"
				   "%b",
				   mb->end, mb->buf, mb->end);
		ASSERT_EQ(0, err);

		err = re_main_wait(5000);
		ASSERT_EQ(0, err);

		/* verify result after traffic is complete */
		ASSERT_EQ(1, srv->n_req);
		//ASSERT_EQ(1, n_resp);
		ASSERT_EQ(0, err_resp);
	}

	void send_request()
	{
		unsigned srv_ix = reqc % NUM_SERVER;
		char url[256];

		if (n_request <= 0)
			return;

		if (reqc >= NUM_REQUEST)
			return;

		if (srv_ix >= ARRAY_SIZE(srvv)) {
			re_printf("srv_ix %u too large\n", srv_ix);
			return;
		}

		re_snprintf(url, sizeof(url), "%s/", srvv[srv_ix]->url);

		err = http_request(&reqv[reqc], cli, "GET", url,
				   http_resp_handler, NULL, this,
				   "Content-Type: application/x-octet-stream\r\n"
				   "Content-Length: %zu\r\n"
				   "\r\n"
				   "%b",
				   mb->end, mb->buf, mb->end);
		ASSERT_EQ(0, err);

		++reqc;

		--n_request;
	}

public:
	HttpServer *srv = nullptr;
	struct dnsc *dnsc = nullptr;
	struct http_cli *cli = nullptr;
	struct http_req *req = nullptr;
	struct mbuf *mb = nullptr;
	char url[256] = "";
	int err = 0;
	int err_resp = 0;
	unsigned n_resp = 0;

	int n_request = 0;
	unsigned n_expected_resp = 0;
	struct http_req *reqv[NUM_REQUEST] = {0};
	size_t reqc = 0;
	HttpServer *srvv[NUM_SERVER] = {0};
};



TEST_F(HttpTest, http)
{
	request(false);
}


TEST_F(HttpTest, https)
{
	request(true);
}


#if 0
TEST_F(HttpTest, connection_reuse)
{
#define REQ_PER_SERVER NUM_REQUEST / NUM_SERVER

	for (unsigned i=0; i<NUM_SERVER; i++) {
		bool secure = i & 0x1;

		srvv[i] = new HttpServer(secure);
		ASSERT_TRUE(srvv[i] != NULL);
	}

	n_request       = NUM_REQUEST;
	n_expected_resp = NUM_REQUEST;

	/* start sending requests.. */
	send_request();

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* verify result after traffic is complete */
	for (unsigned i=0; i<NUM_SERVER; i++) {
		ASSERT_EQ(REQ_PER_SERVER, srvv[i]->n_req);
	}
	ASSERT_EQ(NUM_REQUEST, n_resp);

	ASSERT_EQ(0, err_resp);
}
#endif

