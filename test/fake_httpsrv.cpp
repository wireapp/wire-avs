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

#include <gtest/gtest.h>
#include <re.h>
#include "fakes.hpp"


static void http_req_handler(struct http_conn *conn, const struct http_msg *msg,
			     void *arg)
{
	HttpServer *srv = static_cast<HttpServer *>(arg);

	++srv->n_req;

	http_reply(conn, 200, "OK", NULL);

	if (srv->n_cancel_after && srv->n_req >= srv->n_cancel_after) {
		re_cancel();
	}
}


void HttpServer::init(bool secure)
{
	int err;

	err = sa_set_str(&addr, "127.0.0.1", 0);
	ASSERT_EQ(0, err);

	if (secure) {
		err = https_listen(&sock, &addr, "test/data/cert_ecdsa.pem",
				   http_req_handler, this);
		ASSERT_EQ(0, err);
	}
	else {
		err = http_listen(&sock, &addr, http_req_handler, this);
		ASSERT_EQ(0, err);
	}

	err = tcp_sock_local_get(http_sock_tcp(sock), &addr);
	ASSERT_EQ(0, err);

	re_snprintf(url, sizeof(url), "%s://%J",
		    secure ? "https" : "http", &addr);
}


HttpServer::HttpServer(bool secure)
	: sock(NULL)
	, n_req(0)
{
	init(secure);
}


HttpServer::~HttpServer()
{
	mem_deref(sock);
}
