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
#include "fixture.h"


TEST_F(RestTest, invalid_resource)
{
	request("GET", "/invalidresource");
	ASSERT_EQ(401, scode);
}


TEST_F(RestTest, login_unknown_user)
{
	request("POST", "/login",

		"{\r\n"
		"    \"email\": \"user@unknown.com\",\r\n"
		"    \"password\": \"secret\"\r\n"
		"}\r\n");
	ASSERT_EQ(404, scode);
}


TEST_F(RestTest, login)
{
	backend->addUser("user@domain.com", "secret");

	request("POST", "/login",

		"{\r\n"
		"    \"email\": \"user@domain.com\",\r\n"
		"    \"password\": \"secret\"\r\n"
		"}\r\n");
	ASSERT_EQ(200, scode);

	// simple check for now, verify access-token
	ASSERT_TRUE(content_length != 0);
}


TEST_F(RestTest, login_with_json)
{
	backend->addUser("a@b.com", "x");

	err = rest_request_json(NULL, rest_cli, 0, "POST",
				rest_resp_handler, this,
				"/login",
				2,
				"email",    "a@b.com",
				"password", "x"
				);

	ASSERT_EQ(0, err);

	wait();

	ASSERT_EQ(200, scode);
}


TEST_F(RestTest, login_api)
{
	backend->addUser("a@b.com", "x");

	err = login_request(&login, rest_cli, backend->uri,
			    "a@b.com", "x",
			    login_handler, this);
	ASSERT_EQ(0, err);

	wait();

	ASSERT_EQ(1, loginh_called);
}


TEST_F(RestTest, login_chunked)
{
	backend->addUser("user@domain.com", "secret");
	backend->chunked = true;

	request("POST", "/login",

		"{\r\n"
		"    \"email\": \"user@domain.com\",\r\n"
		"    \"password\": \"secret\"\r\n"
		"}\r\n");
	ASSERT_EQ(200, scode);

	// simple check for now, verify access-token
	ASSERT_TRUE(content_length != 0);
}


TEST_F(RestTest, token)
{
	ASSERT_TRUE(NULL == backend->findToken("abc-1"));

	backend->addToken(3600, "abc-2");
	Token *token = backend->findToken("abc-2");
	ASSERT_TRUE(token != NULL);
	ASSERT_EQ(3600, token->expires_in);
	ASSERT_EQ("abc-2", token->access_token);

	backend->removeToken("abc-2");
	ASSERT_TRUE(NULL == backend->findToken("abc-2"));
}
