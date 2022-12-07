/*
* Wire
* Copyright (C) 2022 Wire Swiss GmbH
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

#include <assert.h>
#include <sodium.h>

#include <re.h>
#include <avs.h>



int hash_conv(const uint8_t *secret,
	      uint32_t secretlen,
	      const char *convid, 
	      char **destid_hash)
{
	crypto_hash_sha256_state ctx;
	const char hexstr[] = "0123456789abcdef";
	const size_t hlen = 16;
	unsigned char hash[crypto_hash_sha256_BYTES];
	const size_t blen = min(hlen, crypto_hash_sha256_BYTES);
	char *dest = NULL;
	size_t i;
	int err = 0;

	err = crypto_hash_sha256_init(&ctx);
	if (err) {
		warning("ccall_hash_id: hash init failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, secret, secretlen);
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)convid, strlen(convid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_final(&ctx, hash);
	if (err) {
		warning("ccall_hash_id: hash final failed\n");
		goto out;
	}

	dest = mem_zalloc(blen * 2 + 1, NULL);
	if (!dest) {
		err = ENOMEM;
		goto out;
	}

	for (i = 0; i < hlen; i++) {
		dest[i * 2]     = hexstr[hash[i] >> 4];
		dest[i * 2 + 1] = hexstr[hash[i] & 0xf];
	}

	*destid_hash = dest;

out:
	if (err) {
		mem_deref(dest);
	}
	return err;
}

int hash_user(const uint8_t *secret,
	      uint32_t secretlen,
	      const char *userid, 
	      const char *clientid,
	      char **destid_hash)
{
	crypto_hash_sha256_state ctx;
	const char hexstr[] = "0123456789abcdef";
	const size_t hlen = 16;
	unsigned char hash[crypto_hash_sha256_BYTES];
	const size_t blen = min(hlen, crypto_hash_sha256_BYTES);
	char *dest = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	size_t i;
	int err = 0;

	if (!userid || !clientid)
		return EINVAL;

	err = crypto_hash_sha256_init(&ctx);
	if (err) {
		warning("ccall_hash_id: hash init failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, secret, secretlen);
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)userid, strlen(userid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)clientid, strlen(clientid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_final(&ctx, hash);
	if (err) {
		warning("ccall_hash_id: hash final failed\n");
		goto out;
	}

	dest = mem_zalloc(blen * 2 + 1, NULL);
	if (!dest) {
		err = ENOMEM;
		goto out;
	}

	for (i = 0; i < hlen; i++) {
		dest[i * 2]     = hexstr[hash[i] >> 4];
		dest[i * 2 + 1] = hexstr[hash[i] & 0xf];
	}

	info("ccall_hash_user %s.%s hash %s\n",
		anon_id(userid_anon, userid),
		anon_client(clientid_anon, clientid),
		dest);
	*destid_hash = dest;

out:
	if (err) {
		mem_deref(dest);
	}
	return err;
}

