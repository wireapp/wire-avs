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


#define INTERVAL_MS    1
#define NUM_PACKETS    2


class Netprobe : public ::testing::Test {

public:
	virtual void SetUp() override
	{
	}

	virtual void TearDown() override
	{
		mem_deref(np);
	}

	static void netprobe_handler(int err,
				     const struct netprobe_result *result,
				     void *arg)
	{
		Netprobe *np = static_cast<Netprobe *>(arg);

		if (err) {
			warning("netprobe: error %m\n", err);
			np->np_err = err;
			goto out;
		}

		np->result = *result;

	out:
		re_cancel();
	}

protected:
	class TurnServer srv;
	struct netprobe *np = nullptr;
	struct netprobe_result result;
	int np_err = 0;
};


TEST_F(Netprobe, udp)
{
	int err;

	err = netprobe_alloc(&np, &srv.addr, IPPROTO_UDP, false,
			     "", "", NUM_PACKETS, INTERVAL_MS,
			     netprobe_handler, this);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify results after test is complete */
	ASSERT_EQ(0, np_err);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_sent);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_recv);
}


TEST_F(Netprobe, tcp)
{
	int err;

	err = netprobe_alloc(&np, &srv.addr_tcp, IPPROTO_TCP, false,
			     "", "", NUM_PACKETS, INTERVAL_MS,
			     netprobe_handler, this);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* Verify results after test is complete */
	ASSERT_EQ(0, np_err);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_sent);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_recv);
}


TEST_F(Netprobe, tls)
{
	int err;

	err = netprobe_alloc(&np, &srv.addr_tls, IPPROTO_TCP, true,
			     "", "", NUM_PACKETS, INTERVAL_MS,
			     netprobe_handler, this);
	ASSERT_EQ(0, err);

	err = re_main_wait(15000);
	ASSERT_EQ(0, err);

	/* Verify results after test is complete */
	ASSERT_EQ(0, np_err);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_sent);
	ASSERT_EQ(NUM_PACKETS, result.n_pkt_recv);
}
