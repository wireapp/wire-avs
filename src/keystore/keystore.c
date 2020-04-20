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
#include <openssl/cipher.h>
#include "avs_keystore.h"

#ifndef __EMSCRIPTEN__
#include <sodium.h>
#endif

#define NUM_KEYS 8

struct keyinfo
{
	uint8_t key[E2EE_SESSIONKEY_SIZE];
	bool set;
};

struct keystore
{
	struct keyinfo keys[NUM_KEYS];
	uint32_t current;
	uint32_t next;
};

static void keystore_destructor(void *data)
{
#ifndef __EMSCRIPTEN__
	struct keystore *ks = data;

	sodium_memzero(ks, sizeof(*ks));
#endif
}

int keystore_alloc(struct keystore **pks)
{
	struct keystore *ks;

	if (!pks)
		return EINVAL;

	ks = mem_zalloc(sizeof(*ks), keystore_destructor);
	if (!ks)
		return ENOMEM;

	*pks = ks;

	return 0;
}

int keystore_set_key(struct keystore *ks, uint32_t index, uint8_t *key, uint32_t ksz)
{
	uint32_t sz;
	if (!ks) {
		return EINVAL;
	}

	index = index % NUM_KEYS;

	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);
	memset(ks->keys[index].key, 0, E2EE_SESSIONKEY_SIZE);
	memcpy(ks->keys[index].key, key, ksz);
	ks->keys[index].set = true;

	return 0;
}

int keystore_get_key(struct keystore *ks, uint32_t index, const uint8_t **pkey)
{
	if (!ks) {
		return EINVAL;
	}

	index = index % NUM_KEYS;

	if (ks->keys[index].set) {
		*pkey = ks->keys[index].key;
		return 0;
	}

	return EINVAL;
}

int keystore_set_current(struct keystore *ks, uint32_t index)
{
	if (!ks) {
		return EINVAL;
	}

	ks->current = index;
	return 0;
}

int keystore_get_current(struct keystore *ks, uint32_t *pindex)
{
	if (!ks) {
		return EINVAL;
	}

	*pindex = ks->current;
	return 0;
}
	
int keystore_get_current_key(struct keystore *ks, uint32_t *pindex, const uint8_t **pkey)
{
	uint32_t p;

	if (!ks) {
		return EINVAL;
	}

	p = ks->current;

	if (pindex) {
		*pindex = p;
	}

	return keystore_get_key(ks, p, pkey);
}

