/*
* Wire
* Copyright (C) 2020 Wire Swiss GmbH
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

class KeystoreTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		const uint8_t callid[] = "CALL_ID";
		const size_t clen = 7;

		keystore_alloc(&ks, true);
		keystore_set_salt(ks, callid, clen);
		keystore_alloc(&ks2, true);
		keystore_set_salt(ks2, callid, clen);
		keystore_alloc(&ks3, false);
		keystore_set_salt(ks3, callid, clen);
	}

	virtual void TearDown() override
	{
		mem_deref(ks);
		mem_deref(ks2);
		mem_deref(ks3);
	}

protected:
	struct keystore *ks;
	struct keystore *ks2;
	struct keystore *ks3;
};

TEST_F(KeystoreTest, set_single_key)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint32_t idx;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_EQ(keystore_get_media_key(ks, idx, b3, KEYSZ), 0);

	ASSERT_TRUE(memcmp(b1, b2, KEYSZ) == 0);
}

TEST_F(KeystoreTest, test_reset)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint32_t idx;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 0);

	ASSERT_EQ(keystore_set_session_key(ks, 1, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 1);

	ASSERT_EQ(keystore_reset(ks), 0);
	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b2, KEYSZ) == 0);
}

TEST_F(KeystoreTest, rotate_one)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint32_t idx;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 1);
}

TEST_F(KeystoreTest, change_era)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint32_t idx;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 1);

	ASSERT_EQ(keystore_set_session_key(ks, 1000, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 1000);
}

TEST_F(KeystoreTest, simple_sync)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t m2[KEYSZ];
	uint8_t m3[KEYSZ];
	uint32_t idx1, idx2;
	size_t i;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks2, 0, b1, KEYSZ), 0);

	for (i = 1; i < 10; i++) {
		ASSERT_EQ(keystore_rotate(ks), 0);
		ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
		ASSERT_EQ(idx1, i);

		ASSERT_EQ(keystore_rotate(ks2), 0);
		ASSERT_EQ(keystore_get_current_session_key(ks2, &idx2, b3, KEYSZ), 0);
		ASSERT_EQ(idx2, i);

		ASSERT_EQ(keystore_get_media_key(ks, idx1, m2, KEYSZ), 0);
		ASSERT_EQ(keystore_get_media_key(ks, idx2, m3, KEYSZ), 0);

		ASSERT_TRUE(memcmp(b2, b3, KEYSZ) == 0);
		ASSERT_TRUE(memcmp(m2, m3, KEYSZ) == 0);
	}
}

TEST_F(KeystoreTest, offset_sync)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint32_t idx1, idx2;
	size_t i, j;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);

	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);

	ASSERT_EQ(keystore_set_session_key(ks2, idx1, b2, KEYSZ), 0);

	for (i = 1; i < 10; i++) {
		ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
		ASSERT_EQ(keystore_get_current_session_key(ks2, &idx2, b3, KEYSZ), 0);
		ASSERT_EQ(idx1, idx2);

		ASSERT_TRUE(memcmp(b2, b3, KEYSZ) == 0);

		ASSERT_EQ(keystore_rotate(ks), 0);
		ASSERT_EQ(keystore_rotate(ks2), 0);
	}
}

TEST_F(KeystoreTest, new_era)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint32_t idx1, idx2;
	size_t i, j;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
	ASSERT_EQ(idx1, 0);

	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
	ASSERT_EQ(idx1, 1);

	ASSERT_EQ(keystore_set_session_key(ks, 1000, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
	ASSERT_EQ(idx1, 1);
	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx1, b2, KEYSZ), 0);
	ASSERT_EQ(idx1, 1000);
}

TEST_F(KeystoreTest, overwrite_current_key)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b3, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	keystore_get_current(ks, &idx, &ts1);
	ASSERT_EQ(idx, 0);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b3, KEYSZ), 0);
	ASSERT_TRUE(memcmp(b1, b3, KEYSZ) == 0);

	/* Overwrite key 0, check that it is updated and the ts also */
	usleep(100000);
	ASSERT_EQ(keystore_set_session_key(ks, 0, b2, KEYSZ), 0);
	keystore_get_current(ks, &idx, &ts2);
	ASSERT_EQ(idx, 0);
	ASSERT_FALSE(ts1 == ts2);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b3, KEYSZ), 0);
	ASSERT_TRUE(memcmp(b2, b3, KEYSZ) == 0);
}

TEST_F(KeystoreTest, set_same_key)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0x00, KEYSZ);
	memset(b3, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b2, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b2, KEYSZ) == 0);

	/* Set key 0 again - should fail */
	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), EALREADY);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b3, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b3, KEYSZ) == 0);
}

TEST_F(KeystoreTest, ignore_old_key)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t b4[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b3, 0xCC, KEYSZ);
	memset(b4, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);

	ASSERT_EQ(keystore_set_session_key(ks, 1, b2, KEYSZ), 0);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);

	ASSERT_EQ(keystore_rotate(ks), 0);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 1);
	ASSERT_TRUE(memcmp(b2, b4, KEYSZ) == 0);

	/* Set key 0 to b3 - should fail */
	ASSERT_EQ(keystore_set_session_key(ks, 0, b3, KEYSZ), EALREADY);
	keystore_get_current(ks, &idx, &ts2);
	ASSERT_EQ(idx, 1);
}

TEST_F(KeystoreTest, delete_old_keys)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t b4[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b3, 0xCC, KEYSZ);
	memset(b4, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks, 1, b2, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks, 2, b3, KEYSZ), 0);
	ASSERT_EQ(keystore_get_max_key(ks), 2);

	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);
	/* Check key 0 is still available */
	ASSERT_EQ(keystore_get_media_key(ks, 0, b4, KEYSZ), 0);

	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 1);
	ASSERT_TRUE(memcmp(b2, b4, KEYSZ) == 0);
	/* Check key 0 is still available */
	ASSERT_EQ(keystore_get_media_key(ks, 0, b4, KEYSZ), 0);

	ASSERT_EQ(keystore_rotate(ks), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 2);
	ASSERT_TRUE(memcmp(b3, b4, KEYSZ) == 0);
	/* Check key 0 is deleted */
	ASSERT_EQ(keystore_get_media_key(ks, 0, b4, KEYSZ), ENOENT);
	ASSERT_EQ(keystore_get_media_key(ks, 1, b4, KEYSZ), 0);
	ASSERT_EQ(keystore_get_media_key(ks, 2, b4, KEYSZ), 0);
}

TEST_F(KeystoreTest, no_hash_mode)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t b4[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b2, 0xCC, KEYSZ);
	memset(b4, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks3, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks3, 1, b2, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks3, 2, b3, KEYSZ), 0);
	ASSERT_EQ(keystore_get_max_key(ks3), 2);

	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);

	ASSERT_EQ(keystore_rotate(ks3), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 1);

	ASSERT_EQ(keystore_rotate(ks3), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 2);

	/* Should not rotate beyond the latest */
	ASSERT_EQ(keystore_rotate(ks3), ENOENT);
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 2);

}

TEST_F(KeystoreTest, out_of_order_add)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t b4[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b2, 0xCC, KEYSZ);
	memset(b4, 0x00, KEYSZ);

	ASSERT_EQ(keystore_set_session_key(ks3, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks3, 2, b3, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks3, 1, b2, KEYSZ), 0);
	ASSERT_EQ(keystore_get_max_key(ks3), 2);

	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);

	ASSERT_EQ(keystore_rotate(ks3), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 1);
	ASSERT_TRUE(memcmp(b2, b4, KEYSZ) == 0);

	ASSERT_EQ(keystore_rotate(ks3), 0);
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 2);
	ASSERT_TRUE(memcmp(b3, b4, KEYSZ) == 0);
}

TEST_F(KeystoreTest, rotate_by_time)
{
	uint8_t b1[KEYSZ];
	uint8_t b2[KEYSZ];
	uint8_t b3[KEYSZ];
	uint8_t b4[KEYSZ];
	uint32_t idx;
	uint64_t ts1, ts2;

	memset(b1, 0xAA, KEYSZ);
	memset(b2, 0xBB, KEYSZ);
	memset(b2, 0xCC, KEYSZ);
	memset(b4, 0x00, KEYSZ);

	ts1 = tmr_jiffies();

	/* No keys set, return true */
	ASSERT_TRUE(keystore_rotate_by_time(ks3, ts1-1));
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), ENOENT);

	ASSERT_EQ(keystore_set_session_key(ks3, 0, b1, KEYSZ), 0);
	ASSERT_EQ(keystore_set_session_key(ks3, 1, b2, KEYSZ), 0);
	usleep(10000);
	ts2 = tmr_jiffies();
	ASSERT_EQ(keystore_set_session_key(ks3, 2, b3, KEYSZ), 0);
	ASSERT_EQ(keystore_get_max_key(ks3), 2);

	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);

	/* No rotation as time is less than t1 */
	ASSERT_TRUE(keystore_rotate_by_time(ks3, ts1-1));
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 0);
	ASSERT_TRUE(memcmp(b1, b4, KEYSZ) == 0);

	/* Rotate to key 1 as time is more than t1  but less than t2*/
	ASSERT_TRUE(keystore_rotate_by_time(ks3, ts2-1));
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 1);
	ASSERT_TRUE(memcmp(b2, b4, KEYSZ) == 0);

	/* Rotate to key 2 as time is more than t2, return false as 2 is latest */
	ASSERT_TRUE(!keystore_rotate_by_time(ks3, ts2+10));
	ASSERT_EQ(keystore_get_current_session_key(ks3, &idx, b4, KEYSZ), 0);
	ASSERT_EQ(idx, 2);
	ASSERT_TRUE(memcmp(b3, b4, KEYSZ) == 0);
}

