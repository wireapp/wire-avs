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


#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>

class CcallPair : public ::testing::Test {
protected:
	void SetUp() override
	{
		ASSERT_EQ(0, msystem_get(&msys, "audummy", NULL, NULL, NULL));
		ASSERT_EQ(0, ccall_alloc(&call, NULL, "conference", "user",
					"client", false, false));
	}

	void TearDown() override
	{
		mem_deref(call);
		mem_deref(msys);
	}

	struct ecall *get(enum ccall_ecall_role role)
	{
		return ccall_get_ecall(call, role);
	}

	struct msystem *msys = NULL;
	struct ccall *call = NULL;
};

TEST_F(CcallPair, prepares_distinct_calls_without_starting_signaling)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	struct ecall *subscriber = get(CCALL_ECALL_SUBSCRIBER);
	ASSERT_NE(nullptr, publisher);
	ASSERT_NE(nullptr, subscriber);
	EXPECT_NE(publisher, subscriber);
	EXPECT_EQ(nullptr, ecall_get_econn(publisher));
	EXPECT_EQ(nullptr, ecall_get_econn(subscriber));

	/* The receive role must not accidentally use legacy SFT signaling. */
	EXPECT_NE(nullptr, ecall_get_icall(publisher)->sendh);
	EXPECT_NE(nullptr, ecall_get_icall(subscriber)->sendh);
	EXPECT_NE(ecall_get_icall(publisher)->arg, ecall_get_icall(subscriber)->arg);
	EXPECT_EQ(nullptr, get(CCALL_ECALL_BIDIRECTIONAL));
}

TEST_F(CcallPair, duplicate_prepare_preserves_both_calls)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	struct ecall *subscriber = get(CCALL_ECALL_SUBSCRIBER);
	EXPECT_EQ(EALREADY, ccall_prepare_ecalls(call));
	EXPECT_EQ(publisher, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(subscriber, get(CCALL_ECALL_SUBSCRIBER));
}

TEST_F(CcallPair, subscriber_close_preserves_publisher)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	struct icall *subscriber = ecall_get_icall(get(CCALL_ECALL_SUBSCRIBER));
	ASSERT_NE(nullptr, subscriber->closeh);
	subscriber->closeh(subscriber, EIO, NULL, 0, NULL, NULL,
			   subscriber->arg);
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	EXPECT_EQ(publisher, get(CCALL_ECALL_PUBLISHER));
}

TEST_F(CcallPair, end_clears_unstarted_pair_and_allows_recreation)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	ccall_end(ccall_get_icall(call));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	ccall_end(ccall_get_icall(call));
	EXPECT_EQ(0, ccall_prepare_ecalls(call));
}

TEST_F(CcallPair, old_subscriber_close_does_not_clear_replacement)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	/* Keep the old callback owner alive while replacing the pair. */
	struct ecall *old = static_cast<struct ecall *>(
		mem_ref(get(CCALL_ECALL_SUBSCRIBER)));
	struct icall *old_icall = ecall_get_icall(old);
	ccall_end(ccall_get_icall(call));
	int err = ccall_prepare_ecalls(call);
	if (err) {
		mem_deref(old);
		FAIL() << "recreating pair failed: " << err;
	}
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	struct ecall *subscriber = get(CCALL_ECALL_SUBSCRIBER);
	old_icall->closeh(old_icall, EIO, NULL, 0, NULL, NULL, old_icall->arg);
	EXPECT_EQ(publisher, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(subscriber, get(CCALL_ECALL_SUBSCRIBER));
	mem_deref(old);
}

TEST_F(CcallPair, validates_arguments)
{
	EXPECT_EQ(EINVAL, ccall_set_enable_publish_subscribe(NULL, true));
	EXPECT_EQ(EINVAL, ccall_prepare_ecalls(NULL));
	EXPECT_EQ(nullptr, ccall_get_ecall(NULL, CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(nullptr, ccall_get_ecall(call,
		static_cast<enum ccall_ecall_role>(-1)));
}

TEST_F(CcallPair, bidirectional_single_call_is_the_default)
{
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *bidirectional = get(CCALL_ECALL_BIDIRECTIONAL);
	ASSERT_NE(nullptr, bidirectional);
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	EXPECT_NE(nullptr, ecall_get_icall(bidirectional)->sendh);
	EXPECT_EQ(EBUSY, ccall_set_enable_publish_subscribe(call, true));
	EXPECT_EQ(bidirectional, get(CCALL_ECALL_BIDIRECTIONAL));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_PUBLISHER));
}

TEST_F(CcallPair, can_disable_publish_subscribe_before_setup)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, false));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	EXPECT_NE(nullptr, get(CCALL_ECALL_BIDIRECTIONAL));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_PUBLISHER));
}

TEST_F(CcallPair, cannot_switch_to_legacy_after_pair_is_created)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	struct ecall *subscriber = get(CCALL_ECALL_SUBSCRIBER);
	EXPECT_EQ(EBUSY, ccall_set_enable_publish_subscribe(call, false));
	EXPECT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	EXPECT_EQ(publisher, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(subscriber, get(CCALL_ECALL_SUBSCRIBER));
}


struct TransportObserver {
	unsigned events[2] = {};
	unsigned sends[2] = {};
	unsigned quality[2] = {};
	unsigned levels[2] = {};
	struct ccall_transport_state last[2] = {};
	struct econn_message *message = NULL;
};

static int transport_send(struct ccall *, enum ccall_ecall_role role,
		struct econn_message *msg, void *arg)
{
	auto *observer = static_cast<TransportObserver *>(arg);
	++observer->sends[role];
	observer->message = msg;
	return EACCES;
}

static void transport_event(struct ccall *, enum ccall_ecall_role role,
		enum ccall_transport_event, const struct ccall_transport_state *state,
		void *arg)
{
	auto *observer = static_cast<TransportObserver *>(arg);
	++observer->events[role];
	observer->last[role] = *state;
}

static void transport_quality(struct ccall *, enum ccall_ecall_role role,
		const struct stats_report *, void *arg)
{
	++static_cast<TransportObserver *>(arg)->quality[role];
}

static void transport_levels(struct ccall *, enum ccall_ecall_role role,
		struct list *, void *arg)
{
	++static_cast<TransportObserver *>(arg)->levels[role];
}

TEST_F(CcallPair, routes_signaling_and_propagates_adapter_errors)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct econn_message msg = {};
	TransportObserver observer;
	struct ccall_transport_handlers handlers = {};
	handlers.sendh = transport_send;
	for (auto role : {CCALL_ECALL_PUBLISHER, CCALL_ECALL_SUBSCRIBER}) {
		auto *icall = ecall_get_icall(get(role));
		EXPECT_EQ(ENOSYS, icall->sendh(icall, "user", &msg, NULL, false, icall->arg));
	}
	ASSERT_EQ(0, ccall_set_transport_handlers(call, &handlers, &observer));
	for (auto role : {CCALL_ECALL_PUBLISHER, CCALL_ECALL_SUBSCRIBER}) {
		auto *icall = ecall_get_icall(get(role));
		EXPECT_EQ(EACCES, icall->sendh(icall, "user", &msg, NULL, false, icall->arg));
		EXPECT_EQ(1u, observer.sends[role]);
		EXPECT_EQ(&msg, observer.message);
	}
}

TEST_F(CcallPair, records_media_and_signaling_per_role)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	TransportObserver observer;
	struct ccall_transport_handlers handlers = {};
	handlers.eventh = transport_event;
	handlers.qualityh = transport_quality;
	handlers.audio_levelh = transport_levels;
	ASSERT_EQ(0, ccall_set_transport_handlers(call, &handlers, &observer));
	auto *pub = ecall_get_icall(get(CCALL_ECALL_PUBLISHER));
	auto *sub = ecall_get_icall(get(CCALL_ECALL_SUBSCRIBER));
	sub->starth(sub, 0, "sfu", "client", false, false,
		    ICALL_CONV_TYPE_CONFERENCE, sub->arg);
	pub->answerh(pub, pub->arg);
	sub->media_estabh(sub, "sfu", "client", false, sub->arg);
	sub->audio_estabh(sub, "sfu", "client", false, sub->arg);
	sub->datachan_estabh(sub, "sfu", "client", false, sub->arg);
	struct ccall_transport_state publisher = {}, subscriber = {};
	ASSERT_EQ(0, ccall_get_transport_state(call, CCALL_ECALL_PUBLISHER, &publisher));
	ASSERT_EQ(0, ccall_get_transport_state(call, CCALL_ECALL_SUBSCRIBER, &subscriber));
	EXPECT_TRUE(publisher.answer_received);
	EXPECT_FALSE(publisher.media_ready);
	EXPECT_FALSE(publisher.offer_received);
	EXPECT_TRUE(subscriber.offer_received);
	EXPECT_TRUE(subscriber.media_ready);
	EXPECT_TRUE(subscriber.audio_ready);
	EXPECT_TRUE(subscriber.data_ready);
	EXPECT_EQ(1u, observer.events[0]);
	EXPECT_EQ(4u, observer.events[1]);

	struct stats_report stats = {};
	struct list levels = LIST_INIT;
	sub->qualityh(sub, "sfu", "client", stats, ICALL_CONV_TYPE_CONFERENCE, sub->arg);
	sub->audio_levelh(sub, &levels, sub->arg);
	EXPECT_EQ(0u, observer.quality[0]);
	EXPECT_EQ(1u, observer.quality[1]);
	EXPECT_EQ(0u, observer.levels[0]);
	EXPECT_EQ(1u, observer.levels[1]);
	sub->media_stoppedh(sub, sub->arg);
	EXPECT_FALSE(observer.last[1].media_ready);
	EXPECT_FALSE(observer.last[1].audio_ready);
	EXPECT_TRUE(observer.last[1].data_ready);

	/* Publisher failure must leave the receive connection intact. */
	pub->closeh(pub, EIO, NULL, 0, NULL, NULL, pub->arg);
	EXPECT_TRUE(observer.last[0].closed);
	EXPECT_EQ(EIO, observer.last[0].error);
	EXPECT_EQ(nullptr, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(sub, ecall_get_icall(get(CCALL_ECALL_SUBSCRIBER)));
}

TEST_F(CcallPair, ignores_wrong_and_retired_callback_contexts)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	auto *pub = ecall_get_icall(get(CCALL_ECALL_PUBLISHER));
	auto *sub = ecall_get_icall(get(CCALL_ECALL_SUBSCRIBER));
	sub->media_estabh(sub, "sfu", "client", false, pub->arg);
	struct ccall_transport_state state = {};
	ASSERT_EQ(0, ccall_get_transport_state(call, CCALL_ECALL_SUBSCRIBER, &state));
	EXPECT_FALSE(state.media_ready);

	auto *old = static_cast<struct ecall *>(mem_ref(get(CCALL_ECALL_SUBSCRIBER)));
	void *saved_arg = sub->arg;
	ccall_end(ccall_get_icall(call));
	ASSERT_EQ(nullptr, sub->arg);
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	sub->media_estabh(sub, "sfu", "client", false, saved_arg);
	ASSERT_EQ(0, ccall_get_transport_state(call, CCALL_ECALL_SUBSCRIBER, &state));
	EXPECT_FALSE(state.media_ready);
	call = static_cast<struct ccall *>(mem_deref(call));
	/* Even an event with a saved argument must not dereference freed ccall. */
	sub->media_estabh(sub, "sfu", "client", false, saved_arg);
	mem_deref(old);
}

TEST_F(CcallPair, end_observer_can_reenter_and_destroy_conference)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ccall_transport_handlers handlers = {};
	handlers.eventh = [](struct ccall *ccall, enum ccall_ecall_role,
			enum ccall_transport_event event,
			const struct ccall_transport_state *, void *arg) {
		if (event != CCALL_TRANSPORT_CLOSED)
			return;
		EXPECT_EQ(EBUSY, ccall_prepare_ecalls(ccall));
		ccall_end(ccall_get_icall(ccall));
		auto **owner = static_cast<struct ccall **>(arg);
		*owner = static_cast<struct ccall *>(mem_deref(*owner));
	};
	ASSERT_EQ(0, ccall_set_transport_handlers(call, &handlers, &call));
	ccall_end(ccall_get_icall(call));
	EXPECT_EQ(nullptr, call);
}

TEST_F(CcallPair, transport_state_validates_mode_and_arguments)
{
	struct ccall_transport_state state = {};
	EXPECT_EQ(ENOTSUP, ccall_get_transport_state(call, CCALL_ECALL_PUBLISHER, &state));
	EXPECT_EQ(EINVAL, ccall_get_transport_state(NULL, CCALL_ECALL_PUBLISHER, &state));
	EXPECT_EQ(EINVAL, ccall_get_transport_state(call, CCALL_ECALL_PUBLISHER, NULL));
	EXPECT_EQ(EINVAL, ccall_set_transport_handlers(NULL, NULL, NULL));
}

TEST_F(CcallPair, bidirectional_role_has_no_publish_subscribe_state)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ccall_transport_state state = {};
	EXPECT_EQ(EINVAL, ccall_get_transport_state(call,
		CCALL_ECALL_BIDIRECTIONAL, &state));
}
