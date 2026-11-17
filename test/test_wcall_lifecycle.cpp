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

#include <pthread.h>
#include <time.h>
#include <atomic>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>

/*
 * calling_instance lifecycle: API calls hold a reference on the
 * instance, so wcall_destroy on one thread must not free it under an
 * API call running on another thread.
 */

namespace {

const char *config_json =
	"{\"ice_servers\":[{"
	"\"urls\":[\"turn:127.0.0.1:3478\"],"
	"\"username\":\"user\","
	"\"credential\":\"secret\"}],"
	"\"ttl\":3600}";

struct hammer {
	pthread_t tid;
	WUSER_HANDLE wuser;
	std::atomic<bool> stop;
	std::atomic<int> calls;
};


void state_handler(const char *convid, int state, void *arg)
{
	(void)convid;
	(void)state;
	(void)arg;
}


void *hammer_thread(void *arg)
{
	struct hammer *h = (struct hammer *)arg;

	while (!h->stop) {
		wcall_get_state(h->wuser, "lifecycle-conv");
		wcall_is_video_call(h->wuser, "lifecycle-conv");
		wcall_free_members(wcall_get_members(h->wuser,
						     "lifecycle-conv"));
		wcall_set_trace(h->wuser, 0);
		wcall_set_state_handler(h->wuser, state_handler);
		wcall_iterate_state(h->wuser, state_handler, NULL);
		++h->calls;
	}

	return NULL;
}


WUSER_HANDLE create(const char *userid)
{
	return wcall_create_ex(userid, "lifecycle-client",
			       0, /* use_mediamgr */
			       "voe",
			       NULL, /* ready */
			       NULL, /* send */
			       NULL, /* sft */
			       NULL, /* incoming */
			       NULL, /* missed */
			       NULL, /* answer */
			       NULL, /* estab */
			       NULL, /* close */
			       NULL, /* metrics */
			       NULL, /* config_req */
			       NULL, /* audio_cbr */
			       NULL, /* video_state */
			       NULL);
}


void sleep_ms(int ms)
{
	struct timespec ts = {0, ms * 1000L * 1000L};

	nanosleep(&ts, NULL);
}


class WcallLifecycle : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		wcall_set_mode(WCALL_MODE_DIRECT);
		ASSERT_EQ(0, wcall_init(0));
	}

	virtual void TearDown() override
	{
		wcall_close();
		wcall_set_mode(WCALL_MODE_MARSHAL);
	}
};

}


TEST_F(WcallLifecycle, api_calls_during_destroy)
{
	int i;

	for (i = 0; i < 20; ++i) {
		struct hammer h;

		h.wuser = create("lifecycle-user");
		ASSERT_NE(WUSER_INVALID_HANDLE, h.wuser);
		h.stop = false;
		h.calls = 0;

		ASSERT_EQ(0, pthread_create(&h.tid, NULL, hammer_thread, &h));
		sleep_ms(2);

		wcall_destroy(h.wuser);

		/* Keep calling with the now stale handle for a bit */
		sleep_ms(2);
		h.stop = true;
		pthread_join(h.tid, NULL);

		EXPECT_GT(h.calls, 0);
		EXPECT_EQ(EINVAL, wcall_get_state(h.wuser, "lifecycle-conv"));
	}
}


TEST_F(WcallLifecycle, destroy_with_pending_start)
{
	WUSER_HANDLE wuser1, wuser2;

	wuser1 = create("lifecycle-user1");
	ASSERT_NE(WUSER_INVALID_HANDLE, wuser1);
	wuser2 = create("lifecycle-user2");
	ASSERT_NE(WUSER_INVALID_HANDLE, wuser2);

	/* Without a config a 1:1 start waits for the instance to become
	 * ready.
	 */
	ASSERT_EQ(0, wcall_start(wuser1, "lifecycle-conv",
				 WCALL_CALL_TYPE_NORMAL,
				 WCALL_CONV_TYPE_ONEONONE, 0, 0));

	/* Destroying drops the pending start with the instance... */
	wcall_destroy(wuser1);
	EXPECT_EQ(EINVAL, wcall_get_state(wuser1, "lifecycle-conv"));

	/* ...so the other instance becoming ready does not run it */
	wcall_config_update(wuser2, 0, config_json);
	EXPECT_EQ(WCALL_STATE_UNKNOWN,
		  wcall_get_state(wuser2, "lifecycle-conv"));

	wcall_destroy(wuser2);
}
