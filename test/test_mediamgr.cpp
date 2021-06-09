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
