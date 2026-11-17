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

extern "C" {
void wcall_thread_enter(void);
void wcall_thread_leave(void);
}

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


WUSER_HANDLE create(const char *userid,
		    wcall_ready_h *readyh = NULL,
		    void *arg = NULL)
{
	return wcall_create_ex(userid, "lifecycle-client",
			       0, /* use_mediamgr */
			       "voe",
			       readyh,
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
			       arg);
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


/*
 * Marshal mode, with the test thread as the API thread and a loop
 * thread running the marshal queue.
 */
namespace {

struct ready {
	std::atomic<int> called;
	int delay_ms;
};


void ready_handler(int version, void *arg)
{
	struct ready *r = (struct ready *)arg;

	(void)version;

	if (r->delay_ms)
		sleep_ms(r->delay_ms);
	++r->called;
}


void shutdown_handler(WUSER_HANDLE wuser, void *arg)
{
	(void)wuser;
	(void)arg;

	re_cancel();
}


std::atomic<bool> loop_entered;


void *loop_thread(void *arg)
{
	(void)arg;

	wcall_thread_enter();
	loop_entered = true;
	re_main(NULL);
	wcall_thread_leave();

	return NULL;
}


class WcallLifecycleMarshal : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		wcall_set_mode(WCALL_MODE_MARSHAL);
		ASSERT_EQ(0, wcall_init(0));
	}

	virtual void TearDown() override
	{
		wcall_close();
	}
};

}


TEST_F(WcallLifecycleMarshal, destroy_waits_and_drops_events)
{
	struct ready ra, rb;
	WUSER_HANDLE wa, wb;
	pthread_t tid;
	uint64_t t0, t;

	ra.called = 0;
	ra.delay_ms = 200;
	rb.called = 0;
	rb.delay_ms = 0;

	/* Create before the loop runs, re is not entered here */
	wa = create("lifecycle-user-a", ready_handler, &ra);
	ASSERT_NE(WUSER_INVALID_HANDLE, wa);
	wb = create("lifecycle-user-b", ready_handler, &rb);
	ASSERT_NE(WUSER_INVALID_HANDLE, wb);
	wcall_set_shutdown_handler(wa, shutdown_handler, NULL);

	loop_entered = false;
	ASSERT_EQ(0, pthread_create(&tid, NULL, loop_thread, NULL));
	while (!loop_entered)
		sleep_ms(1);

	/* a's ready handler keeps the loop busy while b's
	 * config update and destroy are queued behind it.
	 */
	wcall_config_update(wa, 0, config_json);
	wcall_config_update(wb, 0, config_json);

	t0 = tmr_jiffies();
	wcall_destroy(wb);
	t = tmr_jiffies() - t0;

	/* Returned only after the loop got to the DESTROY... */
	EXPECT_EQ(1, ra.called);
	EXPECT_LT(t, (uint64_t)2000);
	EXPECT_EQ(EINVAL, wcall_get_state(wb, "lifecycle-conv"));

	/* ...and b's config update queued before it was dropped */
	EXPECT_EQ(0, rb.called);

	/* With a shutdown handler destroy does not wait, the handler
	 * stops the loop.
	 */
	wcall_destroy(wa);
	pthread_join(tid, NULL);
	EXPECT_EQ(EINVAL, wcall_get_state(wa, "lifecycle-conv"));
}


/*
 * Instance refs are taken and dropped on the API threads (handle
 * lookups, queued events) and on the loop thread (handled events)
 * at the same time. A lost update on the refcount frees the instance
 * under a holder or leaks it. The window is small, so this rarely
 * fails on its own; it is meant to be run under TSan/ASan.
 */
namespace {

struct notif_hammer {
	pthread_t tid;
	WUSER_HANDLE wuser;
	std::atomic<bool> *stop;
	int calls;
};


void *notif_thread(void *arg)
{
	struct notif_hammer *h = (struct notif_hammer *)arg;

	while (!*h->stop) {
		wcall_get_state(h->wuser, "lifecycle-conv");
		wcall_process_notifications(h->wuser, h->calls & 1);
		++h->calls;
	}

	return NULL;
}

}


TEST_F(WcallLifecycleMarshal, concurrent_refs)
{
	struct notif_hammer hv[4];
	std::atomic<bool> stop;
	WUSER_HANDLE wa, wb;
	pthread_t tid;
	size_t i;

	wa = create("lifecycle-user-a");
	ASSERT_NE(WUSER_INVALID_HANDLE, wa);
	wb = create("lifecycle-user-b");
	ASSERT_NE(WUSER_INVALID_HANDLE, wb);
	wcall_set_shutdown_handler(wa, shutdown_handler, NULL);

	loop_entered = false;
	ASSERT_EQ(0, pthread_create(&tid, NULL, loop_thread, NULL));
	while (!loop_entered)
		sleep_ms(1);

	stop = false;
	for (i = 0; i < sizeof(hv) / sizeof(hv[0]); ++i) {
		hv[i].wuser = wb;
		hv[i].stop = &stop;
		hv[i].calls = 0;
		ASSERT_EQ(0, pthread_create(&hv[i].tid, NULL,
					    notif_thread, &hv[i]));
	}

	sleep_ms(300);

	/* Destroy while the hammers are still going */
	wcall_destroy(wb);
	EXPECT_EQ(EINVAL, wcall_get_state(wb, "lifecycle-conv"));

	stop = true;
	for (i = 0; i < sizeof(hv) / sizeof(hv[0]); ++i) {
		pthread_join(hv[i].tid, NULL);
		EXPECT_GT(hv[i].calls, 0);
	}

	wcall_destroy(wa);
	pthread_join(tid, NULL);
}


/*
 * msystem calls the mute/activate handlers on the thread that changed
 * the state (the app's main thread on iOS). The instance is only
 * touched on the loop thread, so the handlers post an event to it.
 */
namespace {

struct mute_state {
	pthread_t loop_tid;
	std::atomic<int> n_mute;
	std::atomic<int> n_mute_other;
};

struct mute_state mst;


void mute_handler(int muted, void *arg)
{
	(void)muted;
	(void)arg;

	++mst.n_mute;
	if (!pthread_equal(pthread_self(), mst.loop_tid))
		++mst.n_mute_other;
}


void *mute_loop_thread(void *arg)
{
	mst.loop_tid = pthread_self();

	return loop_thread(arg);
}

}


TEST_F(WcallLifecycleMarshal, msystem_handlers_on_loop_thread)
{
	struct ready rb;
	WUSER_HANDLE wa, wb;
	pthread_t tid;
	int i;

	mst.n_mute = 0;
	mst.n_mute_other = 0;
	rb.called = 0;
	rb.delay_ms = 200;

	wa = create("lifecycle-user-a");
	ASSERT_NE(WUSER_INVALID_HANDLE, wa);
	wb = create("lifecycle-user-b", ready_handler, &rb);
	ASSERT_NE(WUSER_INVALID_HANDLE, wb);
	wcall_set_shutdown_handler(wa, shutdown_handler, NULL);
	wcall_set_mute_handler(wa, mute_handler, NULL);
	wcall_set_mute_handler(wb, mute_handler, NULL);

	loop_entered = false;
	ASSERT_EQ(0, pthread_create(&tid, NULL, mute_loop_thread, NULL));
	while (!loop_entered)
		sleep_ms(1);

	/* Changed on this thread, handled on the loop thread */
	msystem_set_muted(true);
	msystem_activate(true);
	for (i = 0; i < 200 && mst.n_mute < 2; ++i)
		sleep_ms(5);
	EXPECT_EQ(2, mst.n_mute);
	EXPECT_EQ(0, mst.n_mute_other);

	/* b's ready handler keeps the loop busy while a mute change is
	 * queued, then b is destroyed: only a's handler runs.
	 */
	mst.n_mute = 0;
	wcall_config_update(wb, 0, config_json);
	sleep_ms(50);
	msystem_set_muted(false);
	msystem_activate(false);
	wcall_destroy(wb);
	EXPECT_EQ(1, rb.called);

	for (i = 0; i < 200 && mst.n_mute < 1; ++i)
		sleep_ms(5);
	sleep_ms(50);
	EXPECT_EQ(1, mst.n_mute);
	EXPECT_EQ(0, mst.n_mute_other);

	wcall_destroy(wa);
	pthread_join(tid, NULL);
}
