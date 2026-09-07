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
	EXPECT_EQ(nullptr, ecall_get_icall(subscriber)->sendh);
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
}

TEST_F(CcallPair, validates_arguments)
{
	EXPECT_EQ(EINVAL, ccall_set_enable_publish_subscribe(NULL, true));
	EXPECT_EQ(EINVAL, ccall_prepare_ecalls(NULL));
	EXPECT_EQ(nullptr, ccall_get_ecall(NULL, CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(nullptr, ccall_get_ecall(call,
		static_cast<enum ccall_ecall_role>(-1)));
}

TEST_F(CcallPair, legacy_single_call_is_the_default)
{
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	struct ecall *publisher = get(CCALL_ECALL_PUBLISHER);
	ASSERT_NE(nullptr, publisher);
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
	EXPECT_NE(nullptr, ecall_get_icall(publisher)->sendh);
	EXPECT_EQ(EBUSY, ccall_set_enable_publish_subscribe(call, true));
	EXPECT_EQ(publisher, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
}

TEST_F(CcallPair, can_disable_publish_subscribe_before_setup)
{
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
	ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, false));
	ASSERT_EQ(0, ccall_prepare_ecalls(call));
	EXPECT_NE(nullptr, get(CCALL_ECALL_PUBLISHER));
	EXPECT_EQ(nullptr, get(CCALL_ECALL_SUBSCRIBER));
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
