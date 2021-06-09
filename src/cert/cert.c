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
#include "avs_log.h"
#include "avs_cert.h"


#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bn.h>


/* note: shadow struct */
struct tls {
	SSL_CTX *ctx;
	X509 *cert;
	char *pass;  /* password for private key */
};


int cert_tls_set_selfsigned_ecdsa(struct tls *tls, const char *curve_name)
{
	X509_NAME *subj = NULL;
	EVP_PKEY *key = NULL;
	EC_KEY *ec_key = NULL;
	X509 *cert = NULL;
	int r, err = ENOMEM;
	const char *cn = "ztest@wire.com";
	int eccgrp;

	if (!tls || !cn)
		return EINVAL;

	key = EVP_PKEY_new();
	if (!key)
		goto out;

	eccgrp = OBJ_txt2nid(curve_name);
	if (eccgrp == NID_undef) {
		warning("curve not supported: %s\n");
		return ENOTSUP;
	}

	/* ECDSA */
	ec_key = EC_KEY_new_by_curve_name(eccgrp);
	if (!ec_key) {
		warning("EC_KEY_new_by_curve_name error\n");
		goto out;
	}

	/* NOTE: important */
	EC_KEY_set_asn1_flag(ec_key, OPENSSL_EC_NAMED_CURVE);

	if (!EC_KEY_generate_key(ec_key)) {
		warning("EC_KEY_generate_key error\n");
		goto out;
	}

	if (!EVP_PKEY_assign_EC_KEY(key, ec_key)) {
		warning("EVP_PKEY_assign_EC_KEY error\n");
		goto out;
	}

	/* ownership of ec_key struct was assigned, don't free it. */

	cert = X509_new();
	if (!cert)
		goto out;

	if (!X509_set_version(cert, 2))
		goto out;

	if (!ASN1_INTEGER_set(X509_get_serialNumber(cert), rand_u32()))
		goto out;

	subj = X509_NAME_new();
	if (!subj)
		goto out;

	if (!X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
					(unsigned char *)cn,
					(int)str_len(cn), -1, 0))
		goto out;

	if (!X509_set_issuer_name(cert, subj) ||
	    !X509_set_subject_name(cert, subj))
		goto out;

	if (!X509_gmtime_adj(X509_get_notBefore(cert), -3600*24*365) ||
	    !X509_gmtime_adj(X509_get_notAfter(cert),   3600*24*365*10))
		goto out;

	if (!X509_set_pubkey(cert, key))
		goto out;

	if (!X509_sign(cert, key, EVP_sha1()))
		goto out;

	r = SSL_CTX_use_certificate(tls->ctx, cert);
	if (r != 1)
		goto out;

	r = SSL_CTX_use_PrivateKey(tls->ctx, key);
	if (r != 1) {
		warning("SSL_CTX_use_PrivateKey error\n");
		ERR_print_errors_fp(stderr);
		goto out;
	}

	if (tls->cert)
		X509_free(tls->cert);

	tls->cert = cert;
	cert = NULL;

#if 0
	X509_print_fp(stderr, tls->cert);
#endif

	err = 0;

 out:
	if (subj)
		X509_NAME_free(subj);

	if (cert)
		X509_free(cert);

	if (key)
		EVP_PKEY_free(key);

	if (err)
		ERR_clear_error();

	return err;
}


/**
 * Enable ECDH (Elliptic Curve Diffie-Hellmann) on the TLS context
 *
 * This is valid for a TLS/DTLS server only.
 */
int cert_enable_ecdh(struct tls *tls)
{
	SSL_CTX *ctx;

	if (!tls)
		return EINVAL;

	ctx = tls_openssl_context(tls);
	if (!ctx) {
		warning("cert: no openssl context\n");
		return ENOENT;
	}

	if (!SSL_CTX_set_ecdh_auto(ctx, 1)) {
		warning("cert: failed to enable ECDH auto\n");
		ERR_clear_error();
		return EPROTO;
	}

	return 0;
}
