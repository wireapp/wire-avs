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
#include <string>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>

/*
 * Reproduces a crash where ending the last call made the mediamgr thread
 * reset the mute state, which ran the wcall mute handlers on that thread.
 * They walk the instance's calls and read their userlists, while the
 * calling thread may be destroying the instance:
 *
 *   mediamgr thread: msystem_set_muted -> msys_mute_handler ->
 *                    call_group_change_json -> userlist_get_members
 *   calling thread:  wcall_destroy -> mm_destructor -> pthread_join
 *
 * The mute handlers must only ever run on the thread driving wcall.
 */

namespace {

const char *convid = "mute-race-conv";

struct state {
	pthread_t tid;
	std::atomic<int> n_mute;
	std::atomic<int> n_mute_other;
	std::atomic<int> n_part_other;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool inside;
};

struct state st;


bool on_wcall_thread(void)
{
	return pthread_equal(pthread_self(), st.tid);
}


void mute_handler(int muted, void *arg)
{
	++st.n_mute;
	if (!on_wcall_thread())
		++st.n_mute_other;
}


void participant_handler(const char *convid, const char *mjson, void *arg)
{
	struct timespec ts = {0, 200 * 1000 * 1000};

	if (on_wcall_thread())
		return;

	++st.n_part_other;

	/* We are inside the loop over the instance's calls, on another
	 * thread. Hold it here while the test thread destroys the
	 * instance, then carry on with the freed calls, as in the crash.
	 * Waiting for the destroy to finish would deadlock, as it joins
	 * the mediamgr thread.
	 */
	pthread_mutex_lock(&st.mutex);
	st.inside = true;
	pthread_cond_signal(&st.cond);
	pthread_mutex_unlock(&st.mutex);

	nanosleep(&ts, NULL);
}


bool wait_inside(int ms)
{
	struct timespec ts;
	bool inside;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += ms / 1000;
	ts.tv_nsec += (ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		++ts.tv_sec;
		ts.tv_nsec -= 1000000000L;
	}

	pthread_mutex_lock(&st.mutex);
	while (!st.inside) {
		if (pthread_cond_timedwait(&st.cond, &st.mutex, &ts))
			break;
	}
	inside = st.inside;
	pthread_mutex_unlock(&st.mutex);

	return inside;
}


class WcallMute : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		st.tid = pthread_self();
		st.n_mute = 0;
		st.n_mute_other = 0;
		st.n_part_other = 0;
		st.inside = false;
		pthread_mutex_init(&st.mutex, NULL);
		pthread_cond_init(&st.cond, NULL);

		wcall_set_mode(WCALL_MODE_DIRECT);
		ASSERT_EQ(0, wcall_init(0));
	}

	virtual void TearDown() override
	{
		wcall_close();
		wcall_set_mode(WCALL_MODE_MARSHAL);

		pthread_cond_destroy(&st.cond);
		pthread_mutex_destroy(&st.mutex);
	}
};

}


TEST_F(WcallMute, destroy_after_call_end)
{
	WUSER_HANDLE wuser;
	std::string msg;
	int err;

	wuser = wcall_create_ex("mute-user", "mute-client",
				1, /* use_mediamgr */
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
	ASSERT_NE(WUSER_INVALID_HANDLE, wuser);

	/* Calls are only accepted once there is a config */
	wcall_config_update(wuser, 0,
			    "{\"ice_servers\":[{"
			    "\"urls\":[\"turn:127.0.0.1:3478\"],"
			    "\"username\":\"user\","
			    "\"credential\":\"secret\"}],"
			    "\"sft_servers\":[{"
			    "\"urls\":[\"https://sft.example.invalid\"]}],"
			    "\"ttl\":3600}");

	wcall_set_mute_handler(wuser, mute_handler, NULL);
	wcall_set_participant_changed_handler(wuser, participant_handler, NULL);

	/* An incoming conference call, which rings and moves mediamgr
	 * out of its normal state.
	 */
	msg = "{\"version\":\"3.0\",\"type\":\"CONFSTART\","
		"\"sessid\":\"mute-race\",\"resp\":false,"
		"\"sft_url\":\"https://sft.example.invalid\","
		"\"secret\":\"c2VjcmV0\",\"timestamp\":\"100\","
		"\"seqno\":\"1\"}";
	err = wcall_recv_msg(wuser, (const uint8_t *)msg.data(), msg.size(),
			     100, 100, convid, "remote-user", "remote-client",
			     WCALL_CONV_TYPE_CONFERENCE, 0);
	ASSERT_EQ(0, err);
	ASSERT_EQ(WCALL_STATE_INCOMING, wcall_get_state(wuser, convid));

	/* Ending the last call resets the mute state */
	wcall_reject(wuser, convid);

	/* Destroy while the mute handlers may still run on the mediamgr
	 * thread. Without the bug nothing runs there, and this just
	 * times out.
	 */
	wait_inside(500);
	wcall_destroy(wuser);

	EXPECT_GT(st.n_mute, 0);
	EXPECT_EQ(0, st.n_mute_other);
	EXPECT_EQ(0, st.n_part_other);
}
