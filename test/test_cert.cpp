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


class cert_test : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		int err;

		log_set_min_level(LOG_LEVEL_INFO);
		log_enable_stderr(true);

		err = tls_alloc(&tls, TLS_METHOD_SSLV23, 0, 0);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		mem_deref(tls);
	}


protected:
	struct tls *tls = nullptr;
};


TEST_F(cert_test, generate_selfsigned_rsa_1024)
{
	int err;

	err = tls_set_selfsigned(tls, "ztest@wire.com");
	ASSERT_EQ(0, err);
}


TEST_F(cert_test, generate_selfsigned_ecdsa_prime256v1)
{
	int err;
	err = cert_tls_set_selfsigned_ecdsa(tls, "prime256v1");
	ASSERT_EQ(0, err);
}


TEST_F(cert_test, generate_selfsigned_ecdsa_secp521r1)
{
	int err;
	err = cert_tls_set_selfsigned_ecdsa(tls, "secp521r1");
	ASSERT_EQ(0, err);
}
