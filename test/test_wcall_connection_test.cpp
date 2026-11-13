/* Connection-test start contracts. No SFT transport or media device is used. */
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <gtest/gtest.h>
#include <string>
#include "ztest.h"

extern "C" {
#include "../src/wcall/wcall.h"
}

namespace {
const char config[] =
	"{\"sft_servers\":[{\"urls\":[\"https://sft.example.invalid\"]}],"
	"\"ttl\":3600}";

class ConnectionTestStart : public testing::Test {
protected:
	WUSER_HANDLE user = WUSER_INVALID_HANDLE;
	unsigned backend_messages = 0;
	unsigned sft_messages = 0;
	unsigned closed_calls = 0;
	int close_reason = -1;
	bool stop_on_close = false;
	int config_error = 0;
	void *sft_context = nullptr;
	std::string sft_payload;

	static int request_config(WUSER_HANDLE, void *arg)
	{
		return static_cast<ConnectionTestStart *>(arg)->config_error;
	}
	static void closed(int reason, const char *, uint32_t, const char *,
			   const char *, const char *, void *arg)
	{
		auto *test = static_cast<ConnectionTestStart *>(arg);
		++test->closed_calls;
		test->close_reason = reason;
		if (test->stop_on_close)
			re_cancel();
	}
	static int send(void *, const char *, const char *, const char *,
			const char *, const char *, const uint8_t *, size_t,
			int, int, void *arg)
	{
		++static_cast<ConnectionTestStart *>(arg)->backend_messages;
		return 0;
	}
	static int send_sft(void *ctx, const char *, const uint8_t *data,
			    size_t len, void *arg)
	{
		auto *test = static_cast<ConnectionTestStart *>(arg);
		++test->sft_messages;
		test->sft_context = ctx;
		test->sft_payload.assign(reinterpret_cast<const char *>(data), len);
		return 0;
	}
	void SetUp() override
	{
		ASSERT_EQ(0, flowmgr_init("audummy"));
		ASSERT_EQ(0, wcall_init(0));
		wcall_set_mode(WCALL_MODE_DIRECT);
		user = wcall_create_ex("test-user", "test-client", false, "voe",
			nullptr, send, send_sft, nullptr, nullptr, nullptr,
			nullptr, closed, nullptr, request_config, nullptr, nullptr, this);
		ASSERT_NE(WUSER_INVALID_HANDLE, user);
	}
	void TearDown() override
	{
		if (user != WUSER_INVALID_HANDLE)
			wcall_destroy(user);
		wcall_close();
		wcall_set_mode(WCALL_MODE_MARSHAL);
		flowmgr_close();
	}
	void configure() { wcall_config_update(user, 0, config); }
	std::string test_convid()
	{
		char *id = nullptr;
		const int err = wcall_alloc_test_convid(wuser2inst(user), &id);
		EXPECT_EQ(0, err);
		std::string result = id ? id : "";
		mem_deref(id);
		return result;
	}
};

TEST_F(ConnectionTestStart, rejectsInvalidParameters)
{
	EXPECT_EQ(EINVAL, wcall_start_test(WUSER_INVALID_HANDLE, 0));
	EXPECT_EQ(EINVAL, wcall_start_test(user, -1));
	EXPECT_EQ(EINVAL, wcall_start_test(user, 2));
	EXPECT_EQ(0u, backend_messages);
	EXPECT_EQ(0u, sft_messages);
}

TEST_F(ConnectionTestStart, rejectsDuplicateQueuedStart)
{
	wcall_set_mode(WCALL_MODE_MARSHAL);
	ASSERT_EQ(0, wcall_start_test(user, 0));
	EXPECT_EQ(EALREADY, wcall_start_test(user, 1));
	EXPECT_EQ(0u, sft_messages);
}

TEST_F(ConnectionTestStart, usesStableClientConversation)
{
	char *first = nullptr;
	char *second = nullptr;
	auto *instance = wuser2inst(user);
	ASSERT_EQ(0, wcall_alloc_test_convid(instance, &first));
	ASSERT_EQ(0, wcall_alloc_test_convid(instance, &second));
	EXPECT_STREQ("avs-connection-test:test-user:test-client", first);
	EXPECT_STREQ(first, second);
	mem_deref(first);
	mem_deref(second);
}

TEST_F(ConnectionTestStart, startsMarkedConferenceWithoutBackendMessages)
{
	configure();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	char *convid = nullptr;
	auto *instance = wuser2inst(user);
	ASSERT_EQ(0, wcall_alloc_test_convid(instance, &convid));
	auto *call = wcall_lookup(instance, convid);
	ASSERT_NE(nullptr, call);
	EXPECT_TRUE(wcall_is_test(call));
	EXPECT_EQ(EALREADY, wcall_start_test(user, 1));
	/* Supply the configuration requested by ccall_start. */
	configure();
	EXPECT_EQ(1u, sft_messages);
	EXPECT_NE(std::string::npos, sft_payload.find("CONFCONN"));
	EXPECT_EQ(0u, backend_messages);
	mem_deref(convid);
}

TEST_F(ConnectionTestStart, neverFallsBackToLegacyGroupWithoutSft)
{
	struct wcall *call = nullptr;
	auto *instance = wuser2inst(user);
	EXPECT_EQ(ENOTSUP, wcall_add(instance, &call, "test-no-sft",
		WCALL_CONV_TYPE_CONFERENCE_MLS, false, true, NULL));
	EXPECT_EQ(nullptr, wcall_lookup(instance, "test-no-sft"));
	EXPECT_EQ(nullptr, call);
}

TEST_F(ConnectionTestStart, canEndWhileWaitingForConfigurationAndRestart)
{
	configure();
	const auto id = test_convid();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	wcall_end(user, id.c_str());
	wcall_end(user, id.c_str());
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	configure();
	EXPECT_EQ(0u, sft_messages); // Cancelled join must not consume the update.
	EXPECT_EQ(0, wcall_start_test(user, 1));
	EXPECT_TRUE(wcall_is_test(wcall_lookup(wuser2inst(user), id.c_str())));
}

TEST_F(ConnectionTestStart, configurationRequestFailureUsesNormalEndPath)
{
	configure();
	const auto id = test_convid();
	config_error = EIO;
	// Submission succeeds; processing detects the failed configuration request.
	ASSERT_EQ(0, wcall_start_test(user, 0));
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	config_error = 0;
	EXPECT_EQ(0, wcall_start_test(user, 0));
}

TEST_F(ConnectionTestStart, testStartDoesNotConsumeMeetingDuration)
{
	configure();
	wcall_set_duration(user, "another-conversation", 60);
	ASSERT_EQ(0, wcall_start_test(user, 0));
	EXPECT_TRUE(wcall_is_test(wcall_lookup(wuser2inst(user), test_convid().c_str())));
	auto *duration = wcall_duration_lookup(wuser2inst(user), "another-conversation");
	EXPECT_NE(nullptr, duration);
	mem_deref(duration);
}

TEST_F(ConnectionTestStart, lateSftReplyCannotReachRestartedTest)
{
	configure();
	const auto id = test_convid();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	configure();
	ASSERT_EQ(1u, sft_messages);
	void *old_context = sft_context;
	wcall_end(user, id.c_str());
	wcall_poll();
	ASSERT_EQ(1u, closed_calls);
	ASSERT_EQ(0, wcall_start_test(user, 0));
	wcall_i_sft_resp(wuser2inst(user), 1000, nullptr, old_context);
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_TRUE(wcall_is_test(wcall_lookup(wuser2inst(user), id.c_str())));
}

TEST_F(ConnectionTestStart, testEpochSurvivesStartWhileNormalMlsStillResetsKeys)
{
	for (const bool is_test : {false, true}) {
		struct ccall *call = nullptr;
		struct config *cfg = nullptr;
		struct ecall_conf conf = {};
		ASSERT_EQ(0, config_alloc(&cfg, nullptr, nullptr, nullptr));
		ASSERT_EQ(0, ccall_alloc(&call, &conf, "key-test", "test-user",
			"test-client", true, false));
		ccall_set_test_mode(call, is_test);
		ASSERT_EQ(0, ccall_set_config(call, cfg));
		auto *keys = ccall_get_keystore(call);
		uint8_t key[E2EE_SESSIONKEY_SIZE];
		memset(key, 0x5a, sizeof(key));
		ASSERT_EQ(0, keystore_set_session_key(keys, 1, key, sizeof(key)));
		ASSERT_EQ(0, ccall_start(ccall_get_icall(call),
			ICALL_CALL_TYPE_NORMAL, false, false));
		uint32_t epoch = 0;
		uint64_t updated = 0;
		EXPECT_EQ(is_test ? 0 : ENOENT,
			keystore_get_current(keys, &epoch, &updated));
		if (is_test)
			EXPECT_EQ(1u, epoch);
		mem_deref(call);
		mem_deref(cfg);
	}
}

TEST_F(ConnectionTestStart, endsTwentySecondsAfterFirstConnectionNotification)
{
	configure();
	const auto id = test_convid();
	auto *instance = wuser2inst(user);
	struct wcall *test_call = nullptr;
	struct wcall *normal_call = nullptr;
	ASSERT_EQ(0, wcall_add(instance, &test_call, id.c_str(),
		WCALL_CONV_TYPE_CONFERENCE_MLS, false, true, NULL));
	ASSERT_EQ(0, wcall_add(instance, &normal_call, "normal-call",
		WCALL_CONV_TYPE_CONFERENCE_MLS, false, false, NULL));

	// Simulate established-media notifications without starting a transport.
	const uint64_t connected_at = tmr_jiffies();
	wcall_i_test_connected(test_call);
	wcall_i_test_connected(normal_call);
	struct tmr repeated_connection;
	tmr_init(&repeated_connection);
	tmr_start(&repeated_connection, 10000, [](void *arg) {
		wcall_i_test_connected(static_cast<struct wcall *>(arg));
	}, test_call);
	stop_on_close = true;
	const int err = re_main_wait(22000);
	tmr_cancel(&repeated_connection);
	EXPECT_EQ(0, err);
	EXPECT_GE(tmr_jiffies() - connected_at, 20000u);
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(nullptr, wcall_lookup(instance, id.c_str()));
	EXPECT_EQ(normal_call, wcall_lookup(instance, "normal-call"));
}

TEST_F(ConnectionTestStart, manualEndCancelsConnectedTestTimer)
{
	configure();
	const auto id = test_convid();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	auto *call = wcall_lookup(wuser2inst(user), id.c_str());
	ASSERT_NE(nullptr, call);
	wcall_i_test_connected(call);
	wcall_end(user, id.c_str());
	// A late connection notification must not rearm a terminating test.
	wcall_i_test_connected(call);
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	EXPECT_EQ(0, wcall_start_test(user, 0));
}

TEST_F(ConnectionTestStart, configurationTimeoutClosesOnceAndAllowsRestart)
{
	configure();
	const auto id = test_convid();
	const uint64_t started_at = tmr_jiffies();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	// Leave the ccall configuration request unanswered until its real timeout.
	stop_on_close = true;
	EXPECT_EQ(0, re_main_wait(17000));
	EXPECT_GE(tmr_jiffies() - started_at, 15000u);
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(WCALL_REASON_TIMEOUT, close_reason);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	configure();
	EXPECT_EQ(0u, sft_messages); // The expired join must have been unregistered.
	EXPECT_EQ(0, wcall_start_test(user, 0));
	configure();
	EXPECT_EQ(1u, sft_messages);
	EXPECT_EQ(1u, closed_calls);
}

TEST_F(ConnectionTestStart, unansweredSftOfferTimesOutAndAllowsRestart)
{
	configure();
	const auto id = test_convid();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	configure();
	ASSERT_EQ(1u, sft_messages);
	void *old_context = sft_context;
	// No SFT SETUP is delivered, so CONNSENT must time out after 15 seconds.
	stop_on_close = true;
	EXPECT_EQ(0, re_main_wait(17000));
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(WCALL_REASON_TIMEOUT, close_reason);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	ASSERT_EQ(0, wcall_start_test(user, 0));
	wcall_i_sft_resp(wuser2inst(user), 1000, nullptr, old_context);
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_TRUE(wcall_is_test(wcall_lookup(wuser2inst(user), id.c_str())));
}

TEST_F(ConnectionTestStart, lateConnectionNotificationCannotDelayFailureEnd)
{
	configure();
	const auto id = test_convid();
	ASSERT_EQ(0, wcall_start_test(user, 0));
	configure();
	auto *call = wcall_lookup(wuser2inst(user), id.c_str());
	ASSERT_NE(nullptr, call);
	// Report a transport failure, then a racing established notification.
	wcall_i_sft_resp(wuser2inst(user), 1000, nullptr, sft_context);
	wcall_i_test_connected(call);
	stop_on_close = true;
	EXPECT_EQ(0, re_main_wait(1000));
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(WCALL_REASON_ERROR, close_reason);
	EXPECT_EQ(nullptr, wcall_lookup(wuser2inst(user), id.c_str()));
	EXPECT_EQ(0, wcall_start_test(user, 0));
}

TEST_F(ConnectionTestStart, normalConferenceStillKeepsCallAfterEarlyLeave)
{
	configure();
	ASSERT_EQ(0, wcall_start(user, "normal-conference", WCALL_CALL_TYPE_NORMAL,
		WCALL_CONV_TYPE_CONFERENCE_MLS, 0, 0));
	configure();
	auto *call = wcall_lookup(wuser2inst(user), "normal-conference");
	ASSERT_NE(nullptr, call);
	ASSERT_EQ(1u, sft_messages);
	wcall_i_sft_resp(wuser2inst(user), 1000, nullptr, sft_context);
	wcall_poll();
	EXPECT_EQ(1u, closed_calls);
	EXPECT_EQ(call, wcall_lookup(wuser2inst(user), "normal-conference"));
}
} // namespace
