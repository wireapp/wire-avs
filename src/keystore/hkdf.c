
#include <re.h>
#include <avs.h>

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include "hkdf.h"


int HKDF(uint8_t *out_key, size_t out_len, const EVP_MD *digest,
	 const uint8_t *secret, size_t secret_len,
	 const uint8_t *salt, size_t salt_len,
	 const uint8_t *info, size_t info_len)
{
	EVP_PKEY_CTX *pctx;
	int err = 0;
	
	pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
	if (!pctx) {
		warning("HKDF: PKEY_CTX failed\n");
		return ENOSYS;
	}
	if (EVP_PKEY_derive_init(pctx) <= 0) {
		warning("HKDF: init failed\n");
		err = EIO;
		goto out;
	}
	if (EVP_PKEY_CTX_set_hkdf_md(pctx, digest) <= 0) {
		warning("HKDF: md failed\n");
		err = EIO;
		goto out;
	}
	if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, salt_len) <= 0) {
		warning("HKDF: salt failed\n");
		err = EIO;
		goto out;
	}
	if (EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, secret_len) <= 0) {
		warning("HKDF: secret failed\n");
		err = EIO;
		goto out;
	}
	if (EVP_PKEY_CTX_add1_hkdf_info(pctx, info, info_len) <= 0) {
		warning("HKDF: secret failed\n");
		err = EIO;
		goto out;
	}
	if (EVP_PKEY_derive(pctx, out_key, &out_len) <= 0) {
		warning("HKDF: derive failed\n");
		err = EIO;
		goto out;
	}

 out:
	if (pctx)
		EVP_PKEY_CTX_free(pctx);

	return err ? 0 : 1;
}

