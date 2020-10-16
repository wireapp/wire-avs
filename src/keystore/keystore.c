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

#include <openssl/evp.h>

#if HAVE_WEBRTC
#include <openssl/hkdf.h>
#else
#include "hkdf.h"
#endif

#include "avs_keystore.h"

#include <sodium.h>

#define NUM_KEYS 4

const uint8_t SKEY_INFO[] = "session_key";
const size_t  SKEY_INFO_LEN = 11;
const uint8_t MKEY_INFO[] = "media_key";
const size_t  MKEY_INFO_LEN = 9;
const uint8_t CS_INFO[] = "cs";
const size_t  CS_INFO_LEN = 2;

struct keyinfo
{
	uint8_t skey[E2EE_SESSIONKEY_SIZE];
	uint8_t mkey[E2EE_SESSIONKEY_SIZE];
	uint32_t index;
	bool isset;
};

struct keystore
{
	struct keyinfo keys[NUM_KEYS];
	size_t current;
	size_t head;
	bool init;
	uint8_t *salt;
	size_t slen;

	const EVP_MD *hash_md;

	struct lock *lock;	
};

static int keystore_hash_to_key(struct keystore *ks, uint32_t index);
static int keystore_derive_session_key(struct keystore *ks,
				       struct keyinfo* prev,
				       struct keyinfo* next);
static int keystore_derive_media_key(struct keystore *ks, uint32_t index);

static void keystore_destructor(void *data)
{
	struct keystore *ks = data;

	ks->salt = mem_deref(ks->salt);
	ks->lock = mem_deref(ks->lock);
	sodium_memzero(ks, sizeof(*ks));
}

int keystore_alloc(struct keystore **pks)
{
	struct keystore *ks = NULL;
	int err;

	if (!pks)
		return EINVAL;

	ks = mem_zalloc(sizeof(*ks), keystore_destructor);
	if (!ks)
		return ENOMEM;

	err = lock_alloc(&ks->lock);
	if (err)
		goto out;

	ks->hash_md = EVP_sha512();
	*pks = ks;

out:
	if (err)
		mem_deref(ks);

	return err;
}

int keystore_reset_keys(struct keystore *ks)
{
	if (!ks)
		return EINVAL;

	info("keystore(%p): reset_keys\n", ks);
	lock_write_get(ks->lock);
	sodium_memzero(ks->keys, sizeof(*ks->keys));

	ks->current = 0;
	ks->head = 0;
	ks->init = false;
	lock_rel(ks->lock);

	return 0;
}

int keystore_reset(struct keystore *ks)
{
	if (!ks)
		return EINVAL;

	info("keystore(%p): reset\n", ks);
	lock_write_get(ks->lock);
	sodium_memzero(ks->keys, sizeof(*ks->keys));

	ks->current = 0;
	ks->head = 0;
	ks->init = false;
	ks->slen = 0;
	ks->salt = mem_deref(ks->salt);
	lock_rel(ks->lock);

	return 0;
}

int keystore_set_salt(struct keystore *ks,
		      const uint8_t *salt,
		      size_t saltlen)
{
	uint8_t *tsalt = NULL;

	if (!ks || !salt) {
		warning("keystore(%p): set_salt invalid param\n", ks);
		return EINVAL;
	}

	tsalt = mem_zalloc(saltlen, NULL);
	if (!tsalt) {
		return ENOMEM;
	}

	memcpy(tsalt, salt, saltlen);

	lock_write_get(ks->lock);
	ks->salt = mem_deref(ks->salt);
	ks->salt = tsalt;
	ks->slen = saltlen;
	lock_rel(ks->lock);

	return 0;
}

int keystore_set_session_key(struct keystore *ks,
			     uint32_t index,
			     uint8_t *key,
			     size_t ksz)
{
	uint32_t sz;
	size_t h;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	info("keystore(%p): set_session_key 0x%08x\n", ks, index);
	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);

	lock_write_get(ks->lock);

	if (ks->keys[ks->head].isset && 
	    index <= ks->keys[ks->head].index) {
		info("keystore(%p): set_session_key ignoring old key 0x%08x head %08x\n",
		     ks, index, ks->keys[ks->head].index);
		err = EALREADY;
		goto out;
	}

	h = (ks->head + 1) % NUM_KEYS;

	memset(ks->keys[h].skey, 0, E2EE_SESSIONKEY_SIZE);
	memcpy(ks->keys[h].skey, key, sz);
	ks->keys[h].index = index;
	if (!ks->init) {
		ks->current = h;
		ks->init = true;
	}
	ks->head = h;
	err = keystore_derive_media_key(ks, h);
	if (err)
		goto out;

	ks->keys[h].isset = true;

	info("keystore(%p): set_session_key 0x%08x at index %zu\n",
	     ks, ks->keys[h].index, h);
out:
	lock_rel(ks->lock);

	return err;
}

int keystore_set_fresh_session_key(struct keystore *ks,
				   uint32_t index,
				   uint8_t *key,
				   size_t ksz,
				   uint8_t *salt,
				   size_t saltsz)
{
	int err = 0;
	int s;
	uint8_t hashed_key[E2EE_SESSIONKEY_SIZE];

	if (!ks || !key || !salt) {
		return EINVAL;
	}

	s = HKDF(hashed_key, sizeof(hashed_key), ks->hash_md,
		 key, ksz,
		 salt, saltsz,
		 CS_INFO, CS_INFO_LEN);


	if (s) {
		err = keystore_set_session_key(ks,
					       index,
					       hashed_key,
					       sizeof(hashed_key));
	}
	else {
		err = EINVAL;
	}

	sodium_memzero(hashed_key, sizeof(hashed_key));

	return err;
}

int keystore_get_current_session_key(struct keystore *ks,
				     uint32_t *pindex,
				     uint8_t *pkey,
				     size_t ksz)
{
	uint32_t sz;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);
	memset(pkey, 0, ksz);

	lock_read_get(ks->lock);

	if (ks->keys[ks->current].isset) {
		memcpy(pkey, ks->keys[ks->current].skey, sz);
		*pindex = ks->keys[ks->current].index;
	}
	else {
		err = ENOENT;
	}

	lock_rel(ks->lock);

	return err;
}

int keystore_get_next_session_key(struct keystore *ks,
				  uint32_t *pindex,
				  uint8_t *pkey,
				  size_t ksz)
{
	uint32_t sz;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);
	memset(pkey, 0, ksz);

	lock_read_get(ks->lock);

	if (ks->head != ks->current && 
	    ks->keys[ks->head].isset) {
		memcpy(pkey, ks->keys[ks->head].skey, sz);
		*pindex = ks->keys[ks->head].index;
	}
	else {
		err = ENOENT;
	}

	lock_rel(ks->lock);

	return err;
}


int keystore_rotate(struct keystore *ks)
{
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	info("keystore(%p): rotate h: %zu c: %zu "
	       " i: 0x%08x\n", ks, ks->head, ks->current, ks->keys[ks->head].index);
	lock_write_get(ks->lock);
	if (ks->current == ks->head) {
		err = keystore_hash_to_key(ks, ks->keys[ks->head].index + 1);
		if (err) {
			goto out;
		}
	}
	ks->current = ks->head;

	lock_rel(ks->lock);

	info("keystore(%p): rotate new key %08x at index %zu\n",
	     ks, ks->keys[ks->current].index, ks->current);

out:
	return err;
}

int keystore_get_current(struct keystore *ks, uint32_t *pindex)
{
	if (!ks) {
		return EINVAL;
	}

	if (ks->keys[ks->current].isset) {
		*pindex = ks->keys[ks->current].index;
		return 0;
	}

	return ENOENT;
}

int keystore_get_media_key(struct keystore *ks,
			   uint32_t index,
			   uint8_t *pkey,
			   size_t ksz)
{
	uint32_t sz;
	size_t kid;
	int err = 0;
	bool found = false;

	if (!ks) {
		return EINVAL;
	}

	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);
	memset(pkey, 0, ksz);

	lock_read_get(ks->lock);

	for (kid = 0; kid < NUM_KEYS; kid++) {
		if (ks->keys[kid].isset &&
		    ks->keys[kid].index == index) {
			memcpy(pkey, ks->keys[kid].mkey, sz);
			found = true;
			if (index > ks->keys[ks->current].index) {
				ks->current = kid;
			}
			break;
		}
	}

	if (!found && ks->keys[ks->head].isset &&
	    index > ks->keys[ks->head].index &&
	    index < ks->keys[ks->head].index + NUM_KEYS) {
		err = keystore_hash_to_key(ks, index);
		if (err) {
			goto out;
		}
		memcpy(pkey, ks->keys[ks->head].mkey, sz);
		found = true;
		if (index > ks->keys[ks->current].index) {
			ks->current = ks->head;
		}
	}

out:
	lock_rel(ks->lock);

	return found ? err : ENOENT;
}

static int keystore_hash_to_key(struct keystore *ks, uint32_t index)
{
	uint32_t h, n;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	h = ks->head;

	info("keystore(%p): hash_to_key 0x%08x from 0x%08x\n",
	     ks, index, ks->keys[h].index);
	while(ks->keys[h].index < index) {
		n = (h + 1) % NUM_KEYS;
		err = keystore_derive_session_key(ks, &ks->keys[h], &ks->keys[n]);
		if (err) {
			goto out;
		}
		err = keystore_derive_media_key(ks, n);
		if (err) {
			goto out;
		}
		ks->keys[n].index = ks->keys[h].index + 1;
		ks->keys[n].isset = true;
		h = n;
	}

	ks->head = h;

	info("keystore(%p): hash_to_key new key 0x%08x at index %zu\n",
	     ks, ks->keys[h].index, h);

out:
	return err;
}

static int keystore_derive_session_key(struct keystore *ks,
				       struct keyinfo* prev,
				       struct keyinfo* next)

{
	int s;

	if (!ks) {
		return EINVAL;
	}

	s = HKDF(next->skey, sizeof(next->skey), ks->hash_md,
		 prev->skey, sizeof(prev->skey),
		 ks->salt, ks->slen,
		 SKEY_INFO, SKEY_INFO_LEN);

	return s ? 0 : EINVAL;
}

static int keystore_derive_media_key(struct keystore *ks, uint32_t index)
{
	struct keyinfo *info = &ks->keys[index];
	int s;

	if (!ks) {
		return EINVAL;
	}

	s = HKDF(info->mkey, sizeof(info->mkey), ks->hash_md,
		 info->skey, sizeof(info->skey),
		 ks->salt, ks->slen,
		 MKEY_INFO, MKEY_INFO_LEN);

	return s ? 0 : EINVAL;
}

uint32_t keystore_get_max_key(struct keystore *ks)
{
	if (!ks) {
		return 0;
	}
	return ks->keys[ks->head].index;
}

int keystore_generate_iv(struct keystore *ks,
			 const char *clientid,
			 const char *stream_name,
			 uint8_t *iv,
			 size_t ivsz)
{
	int s;

	if (!ks || !clientid || !stream_name || !iv) {
		return EINVAL;
	}

	memset(iv, 0, ivsz);
	s = HKDF(iv, ivsz, ks->hash_md,
		 (const uint8_t*)clientid, strlen(clientid),
		 (const uint8_t*)stream_name, strlen(stream_name),
		 NULL, 0);

	return s ? 0 : EINVAL;
}

