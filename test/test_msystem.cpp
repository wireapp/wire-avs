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



TEST(msystem, 1)
{
	struct msystem *msys = NULL;
	int err;

	err = msystem_get(&msys, "audummy", NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	ASSERT_TRUE(msystem_is_initialized(msys));
	ASSERT_FALSE(msystem_have_datachannel(msys));

	mem_deref(msys);
}

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <cstring>
#include <ctime>
#include <avs_wcall.h>
#include <vector>
#include "ztest.h"
#include "../src/msystem/msystem.h"

namespace {
/* Independent of libre's loop/timers and the workers being joined.
 * Exit 124 is a failed process deadline, never a successful test result. */
class ProcessDeadline {
	std::mutex mutex;
	std::condition_variable changed;
	bool finished = false;
	std::thread watcher;
public:
	ProcessDeadline() : watcher([this] {
		std::unique_lock<std::mutex> lock(mutex);
		if (!changed.wait_for(lock, std::chrono::seconds(30),
		                     [this] { return finished; }))
			std::_Exit(124);
	}) {}
	~ProcessDeadline()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			finished = true;
		}
		changed.notify_one();
		watcher.join();
	}
};

bool backend_muted;
int reset_during_close;

void set_backend_mute(bool muted) { backend_muted = muted; }
bool get_backend_mute() { return backend_muted; }
void closing_backend() { reset_during_close = msystem_request_mute_reset(); }

class MuteReset : public ::testing::Test {
protected:
	ProcessDeadline deadline;
	struct msystem *msys = NULL;
	std::vector<bool> events;
	pthread_t callback_thread;
	bool stop_on_callback = false;
	int worker_error = 0;
	int loop_error = 0;
#if ENABLE_PEERFLOW
	pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t gate_changed = PTHREAD_COND_INITIALIZER;
	bool runtime_mode = false, runtime_ready = false;
	bool runtime_release = false, runtime_done = false;
	pthread_t runtime_tid;
	int runtime_error = 0, runtime_started = 0, backend_error = 0;
	struct log backend_logger = {};
	bool logger_registered = false;
	unsigned backend_log_reads = 0;
	enum log_level previous_log_level;
#endif

	void SetUp() override
	{
		ASSERT_EQ(0, msystem_get(&msys, "voe", NULL, on_mute, this));
		iflow_register_statics(NULL, set_backend_mute, get_backend_mute);
		backend_muted = true;
	}

	void TearDown() override
	{
#if ENABLE_PEERFLOW
		if (logger_registered) {
			log_unregister_handler(&backend_logger);
			log_set_min_level(previous_log_level);
		}
#endif
		if (msys) {
			msystem_unregister_listener(this);
			msys = (struct msystem *)mem_deref(msys);
		}
#if ENABLE_PEERFLOW
		pthread_cond_destroy(&gate_changed);
		pthread_mutex_destroy(&gate_lock);
#endif
	}

	static void on_mute(bool muted, void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		EXPECT_EQ(muted, msystem_get_muted());
		self->callback_thread = pthread_self();
		self->events.push_back(muted);
		if (self->stop_on_callback) {
#if ENABLE_PEERFLOW
			if (self->runtime_mode) {
				if (muted || !pthread_equal(pthread_self(), self->runtime_tid))
					return;
				wcall_close();
				pthread_mutex_lock(&self->gate_lock);
				self->runtime_done = true;
				pthread_cond_signal(&self->gate_changed);
				pthread_mutex_unlock(&self->gate_lock);
			}
#endif
			re_cancel();
		}
	}

	static void *request_on_media_thread(void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		/* Match mediamgr's private libre context, not the calling loop. */
		self->worker_error = re_thread_init();
		if (!self->worker_error) {
			self->worker_error = msystem_request_mute_reset();
			re_thread_close();
		}
		return NULL;
	}

	void request_reset()
	{
		pthread_t producer;
		ASSERT_EQ(0, pthread_create(&producer, NULL, request_on_media_thread, this));
		ASSERT_EQ(0, pthread_join(producer, NULL));
		ASSERT_EQ(0, worker_error);
	}

	static void *service_loop(void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		self->loop_error = re_main_wait(1000);
		return NULL;
	}
#if ENABLE_PEERFLOW
	static void *cold_loop(void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		msystem_set_mute_owner();
		self->loop_error = re_main_wait(1000);
		return NULL;
	}

	static void cold_gate(int, void *, void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		pthread_mutex_lock(&self->gate_lock);
		self->runtime_ready = true;
		pthread_cond_signal(&self->gate_changed);
		while (!self->runtime_release)
			pthread_cond_wait(&self->gate_changed, &self->gate_lock);
		pthread_mutex_unlock(&self->gate_lock);
		msystem_set_muted(true);
		re_cancel();
	}

	static void *actual_runtime(void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		wcall_thread_main(&self->runtime_error, &self->runtime_started);
		return NULL;
	}

	static void runtime_gate(int id, void *, void *arg)
	{
		auto *self = static_cast<MuteReset *>(arg);
		if (id) {
			wcall_close();
			re_cancel();
			return;
		}
		self->runtime_tid = pthread_self();
		self->backend_error = peerflow_init();
		if (!self->backend_error)
			msystem_set_muted(true);
		pthread_mutex_lock(&self->gate_lock);
		self->runtime_ready = true;
		pthread_cond_signal(&self->gate_changed);
		while (!self->runtime_release)
			pthread_cond_wait(&self->gate_changed, &self->gate_lock);
		pthread_mutex_unlock(&self->gate_lock);
	}

	bool wait_for(bool &flag)
	{
		struct timespec deadline;
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 10;
		int err = 0;
		while (!flag && !err)
			err = pthread_cond_timedwait(&gate_changed, &gate_lock, &deadline);
		return flag;
	}
#endif
};
}

TEST_F(MuteReset, uses_servicing_thread_not_creator_or_private_media_loop)
{
	const pthread_t creator = pthread_self();
	msystem_set_tid(msys, creator);
	request_reset();
	ASSERT_TRUE(backend_muted);
	ASSERT_TRUE(events.empty());
	stop_on_callback = true;
	pthread_t owner;
	ASSERT_EQ(0, pthread_create(&owner, NULL, service_loop, this));
	ASSERT_EQ(0, pthread_join(owner, NULL));
	EXPECT_EQ(0, loop_error);
	ASSERT_EQ(1u, events.size());
	EXPECT_FALSE(events.front());
	EXPECT_TRUE(pthread_equal(owner, callback_thread));
	EXPECT_FALSE(pthread_equal(creator, callback_thread));
}

TEST_F(MuteReset, older_reset_cannot_override_newer_synchronous_mute)
{
	msystem_set_mute_owner();
	request_reset();
	msystem_set_muted(true);
	ASSERT_EQ(2u, events.size());
	EXPECT_FALSE(events[0]);
	EXPECT_TRUE(events[1]);
	EXPECT_TRUE(msystem_get_muted());
	/* Consume the real queued wake; it must not replay an obsolete reset. */
	EXPECT_EQ(ETIMEDOUT, re_main_wait(20));
	EXPECT_EQ(2u, events.size());
	EXPECT_TRUE(msystem_get_muted());
}

TEST_F(MuteReset, full_wake_pipe_still_delivers_reset)
{
	int err = 0;
	unsigned count = 0;
	while (!err && count < 100000) {
		err = msystem_push(msys, 0, NULL);
		if (!err)
			++count;
	}
	ASSERT_GT(count, 0u);
	ASSERT_TRUE(err == EAGAIN || err == EWOULDBLOCK);
	request_reset();
	ASSERT_TRUE(events.empty());
	stop_on_callback = true;
	ASSERT_EQ(0, re_main_wait(1000));
	ASSERT_EQ(1u, events.size());
	EXPECT_FALSE(events.front());
}

#if ENABLE_PEERFLOW
TEST_F(MuteReset, first_reset_cannot_override_newer_cold_loop_mute)
{
	/* No msystem wake has run: bootstrap must precede ordinary B commands. */
	struct mqueue *control = NULL;
	ASSERT_EQ(0, mqueue_alloc(&control, cold_gate, this));
	ASSERT_EQ(0, mqueue_push(control, 0, NULL));
	pthread_t owner;
	ASSERT_EQ(0, pthread_create(&owner, NULL, cold_loop, this));
	pthread_mutex_lock(&gate_lock);
	const bool ready = wait_for(runtime_ready);
	pthread_mutex_unlock(&gate_lock);
	EXPECT_TRUE(ready);
	if (ready)
		request_reset();
	pthread_mutex_lock(&gate_lock);
	runtime_release = true;
	pthread_cond_signal(&gate_changed);
	pthread_mutex_unlock(&gate_lock);
	EXPECT_EQ(0, pthread_join(owner, NULL));
	mem_deref(control);
	EXPECT_EQ(0, loop_error);
	EXPECT_EQ(ETIMEDOUT, re_main_wait(20));
	ASSERT_EQ(2u, events.size());
	EXPECT_FALSE(events.front());
	EXPECT_TRUE(events.back());
	EXPECT_TRUE(msystem_get_muted());
}
#endif

TEST_F(MuteReset, raw_release_cancels_old_reset_before_next_generation)
{
	request_reset();
	msystem_unregister_listener(this);
	msys = (struct msystem *)mem_deref(msys);
	ASSERT_EQ(nullptr, msystem_instance());
	ASSERT_EQ(0, msystem_get(&msys, "voe", NULL, on_mute, this));
	iflow_register_statics(NULL, set_backend_mute, get_backend_mute);
	backend_muted = true;
	EXPECT_EQ(ETIMEDOUT, re_main_wait(20));
	EXPECT_TRUE(events.empty());
	EXPECT_TRUE(msystem_get_muted());
}

TEST_F(MuteReset, closing_rejects_reset_without_reanimating_queue)
{
	iflow_register_statics(closing_backend, set_backend_mute, get_backend_mute);
	msystem_unregister_listener(this);
	msys = (struct msystem *)mem_deref(msys);
	EXPECT_EQ(ECANCELED, reset_during_close);
	EXPECT_EQ(nullptr, msystem_instance());
	EXPECT_EQ(ENOSYS, msystem_request_mute_reset());
}

#ifdef __APPLE__
namespace {
void ignore_category(enum mediamgr_state, void *) {}
}

TEST_F(MuteReset, real_media_thread_can_join_without_calling_listeners)
{
	struct mediamgr *mm = NULL;
	ASSERT_EQ(0, mediamgr_alloc(&mm, ignore_category, NULL));
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_HOLD);
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_NORMAL);
	/* EXIT follows NORMAL on the media queue. This joins the real producer. */
	mem_deref(mm);
	ASSERT_TRUE(events.empty());
	ASSERT_TRUE(backend_muted);
	stop_on_callback = true;
	ASSERT_EQ(0, re_main_wait(1000));
	ASSERT_EQ(1u, events.size());
	EXPECT_FALSE(events.front());
}
#endif

#if ENABLE_PEERFLOW && defined(__APPLE__)
namespace {
void read_mute_from_backend_log(uint32_t, const char *msg, void *arg)
{
	if (strstr(msg, "pf: set_mute:")) {
		(void)wcall_get_mute(WUSER_INVALID_HANDLE);
		++*static_cast<unsigned *>(arg);
	}
}
}

TEST_F(MuteReset, real_wcall_runtime_services_reset_after_media_join)
{
	/* The real runtime creates global_re on A and services it on B. */
	previous_log_level = log_get_min_level();
	ASSERT_EQ(0, wcall_setup());
	backend_logger.h = read_mute_from_backend_log;
	backend_logger.arg = &backend_log_reads;
	log_set_min_level(LOG_LEVEL_INFO);
	log_register_handler(&backend_logger);
	logger_registered = true;
	struct mqueue *control = NULL;
	ASSERT_EQ(0, mqueue_alloc(&control, runtime_gate, this));
	ASSERT_EQ(0, mqueue_push(control, 0, NULL));
	runtime_mode = true;
	stop_on_callback = true;
	pthread_t owner;
	ASSERT_EQ(0, pthread_create(&owner, NULL, actual_runtime, this));
	pthread_mutex_lock(&gate_lock);
	const bool ready = wait_for(runtime_ready);
	pthread_mutex_unlock(&gate_lock);
	EXPECT_TRUE(ready);
	if (ready && !backend_error) {
		EXPECT_TRUE(msystem_get_muted());
		struct mediamgr *mm = NULL;
		const int err = mediamgr_alloc(&mm, ignore_category, NULL);
		EXPECT_EQ(0, err);
		if (!err) {
			mediamgr_set_call_state(mm, MEDIAMGR_STATE_HOLD);
			mediamgr_set_call_state(mm, MEDIAMGR_STATE_NORMAL);
			mem_deref(mm);
		}
		/* B remains gated: C joined without touching real backend/listeners. */
		EXPECT_TRUE(msystem_get_muted());
		EXPECT_EQ(1u, events.size());
	}
	pthread_mutex_lock(&gate_lock);
	runtime_release = true;
	pthread_cond_signal(&gate_changed);
	const bool done = ready && !backend_error && wait_for(runtime_done);
	pthread_mutex_unlock(&gate_lock);
	if (!done)
		EXPECT_EQ(0, mqueue_push(control, 1, NULL));
	EXPECT_EQ(0, pthread_join(owner, NULL));
	mem_deref(control);
	EXPECT_EQ(0, runtime_error);
	EXPECT_EQ(0, backend_error);
	EXPECT_TRUE(done);
	EXPECT_GE(backend_log_reads, 2u);
	/* wcall_thread_main owns avs_close; restore the enclosing test harness. */
	EXPECT_EQ(0, avs_init(0));
	ASSERT_EQ(2u, events.size());
	EXPECT_TRUE(events.front());
	EXPECT_FALSE(events.back());
	EXPECT_TRUE(pthread_equal(owner, callback_thread));
}
#endif

TEST_F(MuteReset, foreign_synchronous_setter_does_not_consume_reset)
{
	msystem_set_mute_owner();
	request_reset();
	pthread_t caller;
	ASSERT_EQ(0, pthread_create(&caller, NULL, [](void *) -> void * {
		msystem_set_muted(true);
		return NULL;
	}, NULL));
	ASSERT_EQ(0, pthread_join(caller, NULL));
	ASSERT_EQ(1u, events.size());
	EXPECT_TRUE(events.front());
	EXPECT_TRUE(pthread_equal(caller, callback_thread));
	stop_on_callback = true;
	ASSERT_EQ(0, re_main_wait(1000));
	ASSERT_EQ(2u, events.size());
	EXPECT_FALSE(events.back());
	EXPECT_TRUE(pthread_equal(pthread_self(), callback_thread));
}
#endif // native concurrency tests; wasm intentionally uses the inline setter
