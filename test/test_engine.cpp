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
#include "fakes.hpp"
#include "ztest.h"


#define MAGIC 0x3b9daac0

#define ENG_MSYS "audummy"


/*
 * A fixture with shared stuff for Engine testing
 */
class EngineTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
#if 1
		/* you can enable this to see what is going on .. */
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(false);
#endif

		err = engine_init(ENG_MSYS);
		ASSERT_EQ(0, err);

		backend = new FakeBackend;

		backend->addUser("user@domain.com", "secret");

		err = engine_alloc(&eng, backend->uri, backend->uri,
				   "user@domain.com", "secret",
				   NULL, false, false, "ztest 1.0",
				   engine_ready_handler,
				   engine_error_handler,
				   engine_shutdown_handler, this);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(eng != NULL);
	}

	virtual void TearDown() override
	{
		mem_deref(eng);
		engine_close();

		delete backend;
	}

	static void engine_ready_handler(void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		ASSERT_EQ(MAGIC, et->magic);

		et->n_ready++;

		/* stop the re_main() loop now */
		re_cancel();
	}

	static void engine_error_handler(int err, void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		warning("engine error (%m)\n", err);

		ASSERT_EQ(MAGIC, et->magic);

		et->n_error++;
		et->err = err;
	}

	static void engine_shutdown_handler(void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		warning(">> Engine shutted down.\n");

		ASSERT_EQ(MAGIC, et->magic);

		et->n_shutdown++;

		/* stop the re_main() loop now */
		re_cancel();
	}

	void shutdown()
	{
		engine_shutdown(eng);

		/* wait for engine to shut down */
		re_main(NULL);
	}

	static void reg_client_handler(int err, const char *clientid, void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		ASSERT_EQ(MAGIC, et->magic);

		if (err) {
			warning("Register client failed: %m\n", err);
			et->err = err;
		}

		++et->n_reg_client;

		re_cancel();
	}

	static void get_client_handler(int err, void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		ASSERT_EQ(MAGIC, et->magic);

		if (err) {
			warning("Get client failed: %m\n", err);
			et->err = err;
		}

		++et->n_get_client;

		re_cancel();
	}

	static void client_handler(const char *clientid, const char *model,
				   void *arg)
	{
		EngineTest *et = static_cast<EngineTest *>(arg);

		ASSERT_EQ(MAGIC, et->magic);

		++et->n_clients;
	}


protected:
	FakeBackend *backend = nullptr;
	struct engine *eng = nullptr;
	int err = 0;
	unsigned n_ready = 0;
	unsigned n_error = 0;
	unsigned n_shutdown = 0;
	unsigned n_get_client = 0;
	unsigned n_clients = 0;
	unsigned n_reg_client = 0;

	uint32_t magic = MAGIC;
};


TEST_F(EngineTest, alloc_and_free)
{
	ASSERT_EQ(0, err);
	ASSERT_EQ(0, n_ready);
	ASSERT_EQ(0, n_error);
	ASSERT_EQ(0, n_shutdown);
}


TEST_F(EngineTest, login_success)
{
	/* wait for network-traffic to complete */
	re_main(NULL);

	ASSERT_EQ(0, err);
	ASSERT_EQ(1, n_ready);
	ASSERT_EQ(0, n_error);
	ASSERT_EQ(0, n_shutdown);
}


TEST(engine, init_and_close)
{
	int err;

	err = engine_init(ENG_MSYS);

	ASSERT_EQ(0, err);
	engine_close();
}


TEST(engine, init_and_close_twice)
{
	int err;

	err = engine_init(ENG_MSYS);
	ASSERT_EQ(0, err);
	engine_close();

	err = engine_init(ENG_MSYS);
	ASSERT_EQ(0, err);
	engine_close();
}


TEST_F(EngineTest, register_client)
{
	struct client_handler clih = {
		.clientregh = reg_client_handler,
		.clienth = client_handler,
		.getclih = get_client_handler,
		.arg     = this
	};
	static const struct zapi_prekey dummy_prekey = {
		.key     = {0},
		.key_len = 32,
		.id      = 65535
	};

#if 0
	/* you can enable this to see what is going on .. */
	log_set_min_level(LOG_LEVEL_WARN);
	log_enable_stderr(true);
#endif

	/* wait for engine to login .. */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);
	ASSERT_EQ(1, n_ready);
	ASSERT_EQ(0, n_error);

	/* 1. Get clients, should be empty */
	err = engine_get_clients(eng, &clih);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);
	ASSERT_EQ(1, n_get_client);
	ASSERT_EQ(0, n_clients);

	/* 2. Register one client and wait for complete */
	err = engine_register_client(eng,
				     &dummy_prekey,
				     &dummy_prekey, 1,
				     &clih);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);
	ASSERT_EQ(1, n_reg_client);

	/* 3. Get clients, should be one */
	err = engine_get_clients(eng, &clih);
	ASSERT_EQ(0, err);

	err = re_main_wait(5000);
	ASSERT_EQ(0, err);
	ASSERT_EQ(2, n_get_client);
	ASSERT_EQ(1, n_clients);

	shutdown();
}
