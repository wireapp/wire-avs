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
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include "complexity_check.h"

struct sync_state{
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool event;
	enum mediamgr_state mm_state;
	struct ztime event_time;
};

static void on_route_changed(enum mediamgr_auplay new_route, void *arg){
	struct sync_state* pss = (struct sync_state*)arg;
    
	pthread_mutex_lock(&pss->mutex);
	ztime_get(&pss->event_time);
	pss->event = true;
	pthread_cond_signal(&pss->cond);
	pthread_mutex_unlock(&pss->mutex);
}

static void on_mcat_changed(enum mediamgr_state new_mcat, void *arg){
	struct sync_state* pss = (struct sync_state*)arg;
    
	pthread_mutex_lock(&pss->mutex);
	ztime_get(&pss->event_time);
	pss->event = true;
	pss->mm_state = new_mcat;
	pthread_cond_signal(&pss->cond);
	pthread_mutex_unlock(&pss->mutex);
}

static void wait_for_event(struct sync_state* pss){
	int ret = 0;
	struct timeval now;
	struct timespec t;
	gettimeofday(&now, NULL);
    
	t.tv_sec = now.tv_sec + 10;
	t.tv_nsec = 0;
    
	pthread_mutex_lock(&pss->mutex);
	while(!pss->event){
		ret = pthread_cond_timedwait(&pss->cond, &pss->mutex, &t);
		if(ret){
			break;
		}
	}
	pthread_mutex_unlock(&pss->mutex);
	ASSERT_EQ(0, ret);
	pss->event = false;
}

TEST(mediamgr, 1)
{
	mediamgr *mm = NULL;
}


static void register_dummy_sounds(struct mediamgr *mm)
{
	static const char * const soundv[] = {
		"ringing_from_me",
		"ready_to_talk"
	};
	size_t i;

	for (i=0; i<ARRAY_SIZE(soundv); i++) {
		mediamgr_register_media(mm,
					soundv[i],
					0,
					false,
					false,
					0,
					0,
					true);
	}
}


class MediamgrTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		int err = 0;

		pthread_mutex_init(&cat_ch_ss.mutex,NULL);
		pthread_cond_init(&cat_ch_ss.cond, NULL);
		cat_ch_ss.event = false;
        
		err = mediamgr_alloc(&mm, on_mcat_changed, &cat_ch_ss);
		ASSERT_EQ(0, err);
        
		pthread_mutex_init(&route_ch_ss.mutex,NULL);
		pthread_cond_init(&route_ch_ss.cond, NULL);
		route_ch_ss.event = false;
        
		mediamgr_register_route_change_h(mm, on_route_changed, &route_ch_ss);

		register_dummy_sounds(mm);
	}

	virtual void TearDown() override
	{
		mem_deref(mm);
	}

	struct sync_state route_ch_ss;
	struct sync_state cat_ch_ss;
    
protected:
	struct mediamgr *mm = nullptr;
};

#if !TARGET_OS_IPHONE

/* Test hooks of the dummy platform, see mm_platform_dummy.c */
extern "C" {
void mm_platform_dummy_reset(void);
void mm_platform_dummy_change_route(enum mediamgr_auplay route);
void mm_platform_dummy_set_fallback(enum mediamgr_auplay route);
void mm_platform_dummy_reject_routes(bool reject);
int mm_platform_dummy_route_requests(void);
}

/* The platform reports its route changes, as Android's AudioRouter does,
 * and the media manager decides which route the call uses.
 */
class MediamgrRouteTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		mm_platform_dummy_reset();
		pthread_mutex_init(&mutex, NULL);
		pthread_cond_init(&cond, NULL);

		ASSERT_EQ(0, mediamgr_alloc(&mm, on_mcat_changed, this));
		mediamgr_register_route_change_h(mm, on_route_changed, this);
	}

	virtual void TearDown() override
	{
		mem_deref(mm);
		pthread_cond_destroy(&cond);
		pthread_mutex_destroy(&mutex);
	}

	static void on_mcat_changed(enum mediamgr_state state, void *arg)
	{
		(void)state;
		(void)arg;
	}

	static void on_route_changed(enum mediamgr_auplay route, void *arg)
	{
		MediamgrRouteTest *test = (MediamgrRouteTest *)arg;

		pthread_mutex_lock(&test->mutex);
		test->routes.push_back(route);
		pthread_cond_signal(&test->cond);
		pthread_mutex_unlock(&test->mutex);
	}

	/* Waits for the media manager to report a route, and to be on it */
	bool wait_for_route(enum mediamgr_auplay route, int times = 1)
	{
		struct timespec deadline;
		int err = 0;

		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 5;

		pthread_mutex_lock(&mutex);
		while (times > 0) {
			while (times > 0 && seen < routes.size()) {
				if (routes[seen++] == route)
					--times;
			}
			if (times == 0 || err == ETIMEDOUT)
				break;
			err = pthread_cond_timedwait(&cond, &mutex, &deadline);
		}
		pthread_mutex_unlock(&mutex);

		return times == 0 && mediamgr_get_route(mm) == route;
	}

	bool wait_for_route_requests(int count)
	{
		for (int i = 0; i < 500; ++i) {
			if (mm_platform_dummy_route_requests() >= count)
				return true;
			usleep(10000);
		}
		return false;
	}

	bool enter_call_with_bluetooth()
	{
		mediamgr_set_call_state(mm, MEDIAMGR_STATE_INCALL);
		mediamgr_bt_device_connected(mediamgr_get(mm), true);

		return wait_for_route(MEDIAMGR_AUPLAY_BT);
	}

protected:
	struct mediamgr *mm = nullptr;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	std::vector<enum mediamgr_auplay> routes;
	size_t seen = 0;
};

TEST_F(MediamgrRouteTest, restores_bluetooth_after_platform_fallback)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));
	ASSERT_EQ(requests + 1, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, restores_speaker_choice_after_platform_fallback)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
}

TEST_F(MediamgrRouteTest, keeps_earpiece_after_speaker_is_turned_off)
{
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_INCALL);
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	mediamgr_enable_speaker(mm, false);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	ASSERT_EQ(requests, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, follows_platform_speaker_instead_of_earpiece)
{
	/* E.g. a tablet without an earpiece */
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_INCALL);
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_SPEAKER);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	ASSERT_EQ(requests, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, video_keeps_explicit_speaker_choice)
{
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_INCALL);
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	mediamgr_enable_speaker(mm, false);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	mediamgr_set_call_state(mm, MEDIAMGR_STATE_INVIDEOCALL);
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	ASSERT_EQ(requests, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, bluetooth_reconnect_replaces_speaker_choice)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	mediamgr_bt_device_connected(mediamgr_get(mm), false);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	mediamgr_bt_device_connected(mediamgr_get(mm), true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));
}

TEST_F(MediamgrRouteTest, wired_headset_replaces_speaker_choice)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	mediamgr_headset_connected(mediamgr_get(mm), true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_HEADSET));

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_HEADSET));
}

TEST_F(MediamgrRouteTest, follows_device_selected_by_platform)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_HEADSET);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_HEADSET));
	ASSERT_EQ(requests, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, stops_restoring_a_route_the_platform_keeps_dropping)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	int requests = mm_platform_dummy_route_requests();
	mm_platform_dummy_set_fallback(MEDIAMGR_AUPLAY_EARPIECE);

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	/* Reported once for the fallback and once per attempt */
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE, 4));
	ASSERT_EQ(requests + 3, mm_platform_dummy_route_requests());

	/* New devices may accept the route again */
	mm_platform_dummy_set_fallback(MEDIAMGR_AUPLAY_UNKNOWN);
	mediamgr_platform_devices_changed(mediamgr_get(mm));

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));
}

TEST_F(MediamgrRouteTest, stops_restoring_a_route_that_does_not_hold)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	int requests = mm_platform_dummy_route_requests();
	for (int i = 0; i < 3; ++i) {
		mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);
		ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));
	}

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	ASSERT_EQ(requests + 3, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, retries_a_rejected_restore)
{
	ASSERT_TRUE(enter_call_with_bluetooth());
	int requests = mm_platform_dummy_route_requests();
	mm_platform_dummy_reject_routes(true);

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);
	ASSERT_TRUE(wait_for_route_requests(requests + 1));
	mm_platform_dummy_reject_routes(false);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_BT));
	ASSERT_EQ(requests + 2, mm_platform_dummy_route_requests());
}

TEST_F(MediamgrRouteTest, follows_platform_outside_a_call)
{
	mediamgr_enable_speaker(mm, true);
	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_SPEAKER));
	int requests = mm_platform_dummy_route_requests();

	mm_platform_dummy_change_route(MEDIAMGR_AUPLAY_EARPIECE);

	ASSERT_TRUE(wait_for_route(MEDIAMGR_AUPLAY_EARPIECE));
	ASSERT_EQ(requests, mm_platform_dummy_route_requests());
}

#endif

#if 0
TEST_F(MediamgrTest, allocate)
{
	ASSERT_TRUE(mm != NULL);
}

TEST_F(MediamgrTest, routing)
{
    mediamgr_auplay route;
    mediamgr_state state = MEDIAMGR_STATE_NORMAL;

    mediamgr_set_call_state(mm, state);
    wait_for_event(&route_ch_ss);
    
    mediamgr_enable_speaker(mm, true);
    wait_for_event(&route_ch_ss);
    
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);

    mediamgr_enable_speaker(mm, false);
    wait_for_event(&route_ch_ss);
    
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);
    
    mediamgr_bt_device_connected(mediamgr_get(mm), true);
    wait_for_event(&route_ch_ss);
    
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);

    mediamgr_bt_device_connected(mediamgr_get(mm), false);
    wait_for_event(&route_ch_ss);
    
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);
    
    state = MEDIAMGR_STATE_INCALL;
    
    mediamgr_set_call_state(mm, state);    
    wait_for_event(&route_ch_ss);
    
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);
    
    mediamgr_headset_connected(mediamgr_get(mm), true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_HEADSET);
    
    mediamgr_enable_speaker(mm, true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);
    
    mediamgr_bt_device_connected(mediamgr_get(mm), true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_BT);
    
    mediamgr_headset_connected(mediamgr_get(mm), false);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_BT);
    
    mediamgr_bt_device_connected(mediamgr_get(mm), false);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);
    
    mediamgr_enable_speaker(mm, false);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);
    
    //Following 3 tests must be performed in sequence to test that media
    //manager does not keep audio routing state to be speaker
    mediamgr_enable_speaker(mm, true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);

    mediamgr_headset_connected(mediamgr_get(mm), true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_HEADSET);
    
    mediamgr_headset_connected(mediamgr_get(mm), false);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_EARPIECE);
    
    state = MEDIAMGR_STATE_INVIDEOCALL;
    mediamgr_set_call_state(mm, state);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);

    mediamgr_headset_connected(mediamgr_get(mm), true);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_HEADSET);
    
    mediamgr_headset_connected(mediamgr_get(mm), false);
    wait_for_event(&route_ch_ss);
    route = mediamgr_get_route(mm);
    ASSERT_TRUE(route == MEDIAMGR_AUPLAY_SPEAKER);
}

TEST_F(MediamgrTest, catagory_change_timing)
{
	struct ztime cur_time;

    ztime_get(&cur_time);
    mediamgr_state state = MEDIAMGR_STATE_NORMAL;
    mediamgr_set_call_state(mm, state);
    wait_for_event(&cat_ch_ss);
    int64_t t = ztime_diff(&cat_ch_ss.event_time, &cur_time);
    COMPLEXITY_CHECK(t,2);
    
    ztime_get(&cur_time);
    state = MEDIAMGR_STATE_INCALL;
    mediamgr_set_call_state(mm, state);
    wait_for_event(&cat_ch_ss);
    t = ztime_diff(&cat_ch_ss.event_time, &cur_time);
    COMPLEXITY_CHECK(t,2);
    
    ztime_get(&cur_time);
    state = MEDIAMGR_STATE_NORMAL;
    mediamgr_set_call_state(mm, state);
    wait_for_event(&cat_ch_ss);
    t = ztime_diff(&cat_ch_ss.event_time, &cur_time);
    COMPLEXITY_CHECK(t,2);
}

TEST(mediamgr, alloc_and_free)
{
	struct mediamgr *mm = NULL;
	int err;

	err = mediamgr_alloc(&mm, on_mcat_changed, NULL);	
	ASSERT_EQ(0, err);
	ASSERT_TRUE(mm != NULL);

	mem_deref(mm);
}
#endif
