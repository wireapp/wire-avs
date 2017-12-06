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


/*
 * KASE = Key Agreement Signalling Extension
 */

static void test()
{
	struct kase *cli = NULL, *srv = NULL;
	uint8_t sharedkey_client_tx[KASE_SESSIONKEY_SIZE];
	uint8_t sharedkey_client_rx[KASE_SESSIONKEY_SIZE];
	uint8_t sharedkey_server_tx[KASE_SESSIONKEY_SIZE];
	uint8_t sharedkey_server_rx[KASE_SESSIONKEY_SIZE];
	int err;

	err = kase_alloc(&cli);
	ASSERT_EQ(0, err);
	err = kase_alloc(&srv);
	ASSERT_EQ(0, err);

	err = kase_get_sessionkeys(sharedkey_client_tx, sharedkey_client_rx,
				   cli, kase_public_key(srv), true,
				   "1", "2");
	ASSERT_EQ(0, err);

	err = kase_get_sessionkeys(sharedkey_server_tx, sharedkey_server_rx,
				   srv, kase_public_key(cli), false,
				   "2", "1");
	ASSERT_EQ(0, err);

	/* verify that shared keys are matching */
	ASSERT_EQ(0, memcmp(sharedkey_client_tx, sharedkey_server_rx,
			    sizeof(sharedkey_client_tx)));
	ASSERT_EQ(0, memcmp(sharedkey_client_rx, sharedkey_server_tx,
			    sizeof(sharedkey_client_rx)));

	mem_deref(srv);
	mem_deref(cli);
}


TEST(kase, basic_exchange)
{
	test();
}


TEST(kase, non_repeating_keys)
{
	struct kase *cli = NULL, *srv = NULL;
	int err;

	err = kase_alloc(&cli);
	ASSERT_EQ(0, err);
	err = kase_alloc(&srv);
	ASSERT_EQ(0, err);

	/* verify that public keys are not the same */
	ASSERT_TRUE(0 != memcmp(kase_public_key(cli),
				kase_public_key(srv),
				KASE_SESSIONKEY_SIZE));

	mem_deref(srv);
	mem_deref(cli);
}


TEST(kase, channel_binding)
{
	static const char *hash_expect = "\x22\x62\x19\x6e\x74\x6c\x90\xa6";
	uint8_t hash_a[KASE_CHANBIND_SIZE];
	uint8_t hash_b[KASE_CHANBIND_SIZE];
	int err;

	err = kase_channel_binding(hash_a, "111", "222");
	ASSERT_EQ(0, err);
	err = kase_channel_binding(hash_b, "222", "111");
	ASSERT_EQ(0, err);

	debug("kase: hash A:  %zu bytes [%w]\n", sizeof(hash_a),
	      hash_a, sizeof(hash_a));
	debug("kase: hash B:  %zu bytes [%w]\n", sizeof(hash_b),
	      hash_b, sizeof(hash_b));

	ASSERT_EQ(0, memcmp(hash_expect, hash_a, sizeof(hash_a)));
	ASSERT_EQ(0, memcmp(hash_expect, hash_b, sizeof(hash_b)));

}


#if 1
TEST(kase, hundreds_of_exchanges)
{
	int i=0;

	for (i=0; i<100; i++) {
		test();
	}

	debug("kase: repeated %d times\n", i);

}
#endif
