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

#define KEYSZ (32)

/* A call config with ICE servers but no SFT: a conference call
 * started with it ends right away (ENOTSUP) and the ccall goes IDLE,
 * as after any call that ended in it.
 */
static const char cfg_no_sft[] =
	"{\"ice_servers\":[{\"urls\":[\"turn:127.0.0.1:3478\"],"
	"\"username\":\"user\",\"credential\":\"cred\"}],"
	"\"ttl\":3600}";

/* The test answers the config requests itself, with config_update */
static int cfg_req_handler(void *arg)
{
	(void)arg;
	return 0;
}

class CcallTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		ASSERT_EQ(0, config_alloc(&cfg, cfg_req_handler, NULL, NULL));
		ASSERT_EQ(0, ccall_alloc(&ccall, NULL,
					 "conv@domain", "user@domain", "client",
					 true, false));
		ASSERT_EQ(0, ccall_set_config(ccall, cfg));
		icall = ccall_get_icall(ccall);
		ks = ccall_get_keystore(ccall);
		ASSERT_TRUE(ks != NULL);

		memset(epoch_key, 0x5a, sizeof(epoch_key));
	}

	virtual void TearDown() override
	{
		mem_deref(ccall);
		mem_deref(cfg);
	}

	/* Media key the ccall's keystore derives from epoch_key */
	void media_key(uint8_t *mkey)
	{
		ASSERT_EQ(0, keystore_set_session_key(ks, 1, epoch_key,
						      KEYSZ));
		ASSERT_EQ(0, keystore_get_media_key(ks, 1, mkey, KEYSZ));
	}

	/* Media key a keystore without salt derives from epoch_key */
	void unsalted_media_key(uint8_t *mkey)
	{
		struct keystore *ref = NULL;

		/* MLS calls do not hash forward (see ccall_alloc) */
		ASSERT_EQ(0, keystore_alloc(&ref, false));
		ASSERT_EQ(0, keystore_set_session_key(ref, 1, epoch_key,
						      KEYSZ));
		ASSERT_EQ(0, keystore_get_media_key(ref, 1, mkey, KEYSZ));
		mem_deref(ref);
	}

protected:
	struct config *cfg = NULL;
	struct ccall *ccall = NULL;
	struct icall *icall = NULL;
	struct keystore *ks = NULL;
	uint8_t epoch_key[KEYSZ];
};


/* A ccall that a call ended in: the keystore reset going IDLE clears the
 * salt, and the next outgoing call must set it again. Without the salt
 * the caller's media keys don't match those of the members joining with
 * its CONFSTART secret, and no frame decrypts either way.
 */
TEST_F(CcallTest, restart_after_ended_call_salts_media_keys)
{
	uint8_t unsalted[KEYSZ];
	uint8_t first[KEYSZ];
	uint8_t second[KEYSZ];

	unsalted_media_key(unsalted);

	/* First call: starts, then ends for want of an SFT */
	ASSERT_EQ(0, ccall_start(icall, ICALL_CALL_TYPE_NORMAL, false, false));
	media_key(first);
	ASSERT_NE(0, memcmp(first, unsalted, KEYSZ));

	ASSERT_EQ(0, config_update(cfg, 0, cfg_no_sft, sizeof(cfg_no_sft) - 1));

	/* Second call on the same ccall */
	ASSERT_EQ(0, ccall_start(icall, ICALL_CALL_TYPE_NORMAL, false, false));
	media_key(second);

	ASSERT_NE(0, memcmp(second, unsalted, KEYSZ))
		<< "media keys derived without salt";
	ASSERT_NE(0, memcmp(second, first, KEYSZ))
		<< "the second call reused the first call's secret";
}
