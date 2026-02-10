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
#include "ztest.h"
#include "fakes.hpp"

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

static void wcall_quality_handler(const char *convid,
				  const char *userid,
				  const char *clientid,
				  const char *quality_info,
				  void *arg)
{
	(void)arg;

	info("call_quality: convid=%s userid=%s quality=%s\n",
	     convid, userid, quality_info);
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
