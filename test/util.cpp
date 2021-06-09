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

#define _GNU_SOURCE 1
#include <sys/time.h>
#include <sys/resource.h>

#include <re.h>
#include <avs.h>
#include "ztest.h"
#include "fakes.hpp"


int create_http_resp(struct http_msg **msgp, const char *str)
{
	struct mbuf *mb;
	int err;

	if (!msgp || !str)
		return EINVAL;

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	mbuf_write_str(mb, str);
	mb->pos = 0;

	err = http_msg_decode(msgp, mb, false);

	mem_deref(mb);

	return err;
}


static void watchdog_timeout(void *arg)
{
	int *err = (int *)arg;

	*err = ETIMEDOUT;

	re_cancel();
}


static void signal_handler(int sig)
{
	re_fprintf(stderr, "test interrupted by signal %d\n", sig);
	//re_cancel();
	exit(2);
}


int re_main_wait(uint32_t timeout_ms)
{
	struct tmr tmr;
	int err = 0;

	tmr_init(&tmr);

	tmr_start(&tmr, timeout_ms, watchdog_timeout, &err);
	(void)re_main(signal_handler);

	tmr_cancel(&tmr);
	return err;
}


int dns_init(struct dnsc **dnscp)
{
        struct sa nsv[16];
        uint32_t nsn;
        int err;

        nsn = ARRAY_SIZE(nsv);

        err = dns_srv_get((char *)NULL, 0, nsv, &nsn);
        if (err) {
                error("dns srv get: %m\n", err);
                goto out;
        }

        err = dnsc_alloc(dnscp, 0, nsv, nsn);
        if (err) {
                error("dnsc alloc: %m\n", err);
                goto out;
        }

 out:
        return err;
}


int create_dtls_srtp_context(struct tls **dtlsp, enum tls_keytype cert_type)
{
	struct tls *dtls = NULL;
	int err;

	if (!dtlsp)
		return EINVAL;

	err = tls_alloc(&dtls, TLS_METHOD_DTLS, NULL, NULL);
	if (err)
		goto out;

	err = cert_enable_ecdh(dtls);
	if (err)
		goto out;

	switch (cert_type) {

	case TLS_KEYTYPE_EC:
		err = cert_tls_set_selfsigned_ecdsa(dtls, "prime256v1");
		break;

	default:
		warning("create_dtls_srtp_context: certificate type %d"
			" not supported\n", cert_type);
		err = ENOTSUP;
		goto out;
	}

	if (err)
		goto out;

	tls_set_verify_client(dtls);

	err = tls_set_srtp(dtls,
			   "SRTP_AEAD_AES_256_GCM:"
			   "SRTP_AEAD_AES_128_GCM:"
			   "SRTP_AES128_CM_SHA1_80:"
			   "SRTP_AES128_CM_SHA1_32");
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(dtls);
	else
		*dtlsp = dtls;

	return err;
}


int ztest_set_ulimit(unsigned num)
{
	struct rlimit limit;
	int err;

	getrlimit(RLIMIT_NOFILE, &limit);

	limit.rlim_cur = num;  /* Soft limit */

	if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
		err = errno;
		warning("setrlimit() failed with errno %m\n", err);
		return err;
	}

	return 0;
}
