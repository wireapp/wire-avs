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
#include <gtest/gtest.h>
#include <avs.h>


TEST(srtp, srtcp_packet)
{
	static const char *key_str =
		"1b982e85e74ca325c2c7e4ef09791d135f326d01b969d25a8799e0f01724";
	static const char *srtcp_pkt =
		"80c9000100000001"               /* RTCP packet */
		"80000001469347f1b3521ba5b99d";  /* SRTCP trailer */
	struct srtp *srtp;
	struct mbuf *mb = mbuf_alloc(256);
	uint8_t key[30], pkt[22];
	int err = 0;

	err |= str_hex(key, sizeof(key), key_str);
	err |= str_hex(pkt, sizeof(pkt), srtcp_pkt);
	ASSERT_EQ(0, err);

	err = srtp_alloc(&srtp, SRTP_AES_CM_128_HMAC_SHA1_80,
			 key, sizeof(key), 0);
	ASSERT_EQ(0, err);

	mbuf_write_mem(mb, pkt, sizeof(pkt));
	mb->pos = 0;

	err = srtcp_decrypt(srtp, mb);
	if (err) {
		warning("srtcp_decrypt failed (%m)\n", err);
	}
	ASSERT_EQ(0, err);
	ASSERT_EQ(0, mb->pos);
	ASSERT_EQ(8, mb->end);

	mem_deref(srtp);
	mem_deref(mb);
}
