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
#include "gtest/gtest.h"
#include <re.h>


TEST(libre, socket_address_v4)
{
	struct sa sa;

	sa_set_str(&sa, "127.0.0.1", 1234);

	ASSERT_EQ(AF_INET, sa_af(&sa));
	ASSERT_EQ(0x7f000001U, sa_in(&sa));
	ASSERT_EQ(1234, sa_port(&sa));
}


TEST(libre, socket_address_v6)
{
	struct sa sa;
	const uint8_t addr_ref[16+1] =
		"\x2a\x02\x0f\xe0\xc1\x11\x21\x50"
		"\x7a\x31\xc1\xff\xfe\xba\x04\x38";
	uint8_t addr[16];

	sa_set_str(&sa, "2a02:fe0:c111:2150:7a31:c1ff:feba:438", 6666);

	ASSERT_EQ(AF_INET6, sa_af(&sa));
	sa_in6(&sa, addr);
	EXPECT_EQ(0, memcmp(addr, addr_ref, 16));
	ASSERT_EQ(6666, sa_port(&sa));
}


TEST(libre, verify_tls_support)
{
	struct tls *tls;
	int err;
	err = tls_alloc(&tls, TLS_METHOD_SSLV23, NULL, NULL);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(tls != NULL);
	mem_deref(tls);
}


TEST(libre, verify_dtls_support)
{
	struct tls *tls;
	int err;
	err = tls_alloc(&tls, TLS_METHOD_DTLSV1, NULL, NULL);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(tls != NULL);
	mem_deref(tls);
}
