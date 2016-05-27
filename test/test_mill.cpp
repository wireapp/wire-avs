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


class MillTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
#if 1
		log_set_min_level(LOG_LEVEL_INFO);
		log_enable_stderr(false);
#endif

		backend = new FakeBackend;
	}

	virtual void TearDown() override
	{
		mem_deref(mill);
		delete backend;
	}

	static void mill_ready_handler(struct mill *mill, void *arg)
	{
		MillTest *mt = static_cast<MillTest *>(arg);

		mt->n_readyh++;

		re_cancel();
	}

	static void mill_error_handler(int err, struct mill *mill, void *arg)
	{
		MillTest *mt = static_cast<MillTest *>(arg);

		mt->n_errorh++;
		mt->error_err = err;

		re_cancel();
	}

	static void mill_shut_handler(struct mill *mill, void *arg)
	{
		MillTest *mt = static_cast<MillTest *>(arg);

		mt->n_shuth++;
	}

protected:
	FakeBackend *backend = nullptr;
	struct mill *mill = 0;
	int err = 0;
	int error_err = 0;               /* saved error-code from error_handler */

	/* handler counters: */
	unsigned n_readyh = 0;
	unsigned n_errorh = 0;
	unsigned n_shuth  = 0;
};


TEST_F(MillTest, basic)
{
	err = mill_alloc(&mill, backend->uri, backend->uri, false,
			 "unknown@user.com", "password", NULL,
			 NULL, NULL, NULL, NULL);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(mill != NULL);

	ASSERT_TRUE(NULL != mill_get_rest(mill));
}


TEST_F(MillTest, login_unknown_user)
{
	err = mill_alloc(&mill, backend->uri, backend->uri, false,
			 "unknown@user.com", "password", NULL,
			 mill_ready_handler, mill_error_handler, mill_shut_handler, this);
	ASSERT_EQ(0, err);

	/* wait for network traffic */
	re_main(0);

	ASSERT_EQ(0, n_readyh);
	ASSERT_EQ(1, n_errorh);
	ASSERT_EQ(0, n_shuth);

	ASSERT_EQ(EPROTO, error_err);
}


TEST_F(MillTest, login_known_user)
{
	backend->addUser("known@user.com", "password");

	err = mill_alloc(&mill, backend->uri, backend->uri, false,
			 "known@user.com", "password", NULL,
			 mill_ready_handler, mill_error_handler, mill_shut_handler, this);
	ASSERT_EQ(0, err);

	/* wait for network traffic */
	re_main(0);

	ASSERT_EQ(1, n_readyh);
	ASSERT_EQ(0, n_errorh);

	/* verify that event-channel is up now */
	ASSERT_TRUE(NULL != mill_get_nevent(mill));
}
