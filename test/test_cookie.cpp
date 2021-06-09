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


static int cookie_print(struct re_printf *pf, void *arg)
{
	struct cookie_jar *jar = (struct cookie_jar *)arg;
	int err;

	err = cookie_jar_print_to_request(jar, pf, "https://zinfra.io/access");
	if (err) {
		re_fprintf(stderr, "cookie_jar_print_to_request: %m\n", err);
		return err;
	}

	return 0;
}

static const char *cookie_zuid =
	"Cookie: zuid=cfZKGJ3iVeCOKjJgUxp-ppErmRTmeeckuMbIrKMs8_T6NV1avEMtB_z7AoDEU8TIeQIJM0JIibKeVEDUNp6WAA==.v=1.k=1.d=1445004205.t=u.l=.u=6f03a2d4-d7b7-435b-bf29-84570d69a52b.r=eb02920b\r\n";

static const char *resp =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: application/json\r\n"

	"Set-Cookie:"
	  " zuid=cfZKGJ3iVeCOKjJgUxp-ppErmRTmeeckuMbIrKMs8_T6NV1avEMtB_z7AoDEU8TIeQIJM0JIibKeVEDUNp6WAA==.v=1.k=1.d=1445004205.t=u.l=.u=6f03a2d4-d7b7-435b-bf29-84570d69a52b.r=eb02920b; "
	  "Path=/access; "
	  "Expires=Fri, 16-Oct-2015 14:03:25 GMT; "
	  "Domain=.zinfra.io; "
	  "HttpOnly;"
	  " Secure\r\n"

	"Set-Cookie:"
	  " zuid=cfZKGJ3iVeCOKjJgUxp-ppErmRTmeeckuMbIrKMs8_T6NV1avEMtB_z7AoDEU8TIeQIJM0JIibKeVEDUNp6WAA==.v=1.k=1.d=1445004205.t=u.l=.u=6f03a2d4-d7b7-435b-bf29-84570d69a52b.r=eb02920b; "
	  "Path=/access; "
	  "Expires=Fri, 16-Oct-2015 14:03:25 GMT; "
	  "Domain=.zinfra.io; "
	  "HttpOnly;"
	  " Secure\r\n"

	"Content-Length: 0\r\n"
	"\r\n"
	;


TEST(cookie, 1)
{
	struct cookie_jar *jar;
	struct http_msg *msg;
	char req[256];
	int err;

	err = cookie_jar_alloc(&jar, NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, list_count(cookie_jar_list(jar)));

	err = create_http_resp(&msg, resp);
	ASSERT_EQ(0, err);

	err = cookie_jar_handle_response(jar, "https://xxx.zinfra.io/", msg);
	ASSERT_EQ(0, err);

	ASSERT_EQ(1, list_count(cookie_jar_list(jar)));

	re_snprintf(req, sizeof(req), "%H", cookie_print, jar);
	ASSERT_STREQ(cookie_zuid, req);

	mem_deref(msg);
	mem_deref(jar);
}
