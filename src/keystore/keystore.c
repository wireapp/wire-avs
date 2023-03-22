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

#define NUM_KEYS 16

const uint8_t SKEY_INFO[] = "session_key";
const size_t  SKEY_INFO_LEN = 11;
const uint8_t MKEY_INFO[] = "media_key";
const size_t  MKEY_INFO_LEN = 9;
const uint8_t CS_INFO[] = "cs";
const size_t  CS_INFO_LEN = 2;

struct listener
{
	struct le le;
	ks_cchangedh *changedh;
	void *arg;
};

struct keyinfo
{
	struct le le;
	uint8_t skey[E2EE_SESSIONKEY_SIZE];
	uint8_t mkey[E2EE_SESSIONKEY_SIZE];
	uint32_t index;
	uint64_t update_ts;
};

struct keystore
{
	struct list keyl;
	struct keyinfo *current;
	bool init;
	uint8_t *salt;
	size_t slen;

	bool hash_forward;
	bool has_keys;
	bool decrypt_successful;
	bool decrypt_attempted;
	const EVP_MD *hash_md;

	struct list listeners;

	uint64_t update_ts;
	struct lock *lock;	
};

static int keystore_hash_to_key(struct keystore *ks, uint32_t index);
static int keystore_derive_session_key(struct keystore *ks,
				       struct keyinfo *prev,
				       struct keyinfo **pnext);
static int keystore_derive_media_key(struct keystore *ks,
				     struct keyinfo *kinfo);
static int keystore_append_key(struct keystore *ks,
			       struct keyinfo *kinfo);
static int keystore_organise(struct keystore *ks);


static bool is_empty(const uint8_t *buf, size_t sz)
{
	if (sz < 1)
		return true;

	return (buf[0] == 0) && (memcmp(buf, buf + 1, sz - 1) == 0);
}

static void keyinfo_destructor(void *data)
{
	struct keyinfo *info = data;

	list_unlink(&info->le);
	sodium_memzero(info, sizeof(*info));
}

static void keystore_destructor(void *data)
{
	struct keystore *ks = data;

	ks->salt = mem_deref(ks->salt);
	ks->lock = mem_deref(ks->lock);
	list_flush(&ks->keyl);
	sodium_memzero(ks, sizeof(*ks));
}

static struct keyinfo *keystore_get_latest(struct keystore *ks)
{
	struct le *le;

	le = ks->keyl.head;
	while (le && le->next)
		le = le->next;

	return le ? le->data : NULL;
}

int keystore_alloc(struct keystore **pks, bool hash_forward)
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

	ks->update_ts = tmr_jiffies();
	ks->hash_md = EVP_sha512();
	ks->hash_forward = hash_forward;
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
	list_flush(&ks->keyl);
	ks->current = NULL;

	ks->init = false;
	ks->has_keys = false;
	ks->decrypt_attempted = false;
	ks->decrypt_successful = false;

	lock_rel(ks->lock);

	return 0;
}

int keystore_reset(struct keystore *ks)
{
	if (!ks)
		return EINVAL;

	info("keystore(%p): reset\n", ks);
	lock_write_get(ks->lock);

	list_flush(&ks->keyl);
	ks->current = NULL;
	ks->init = false;
	ks->slen = 0;
	ks->salt = mem_deref(ks->salt);
	ks->has_keys = false;
	ks->decrypt_attempted = false;
	ks->decrypt_successful = false;

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

	info("keystore(%p): set_salt %zu bytes\n", ks, saltlen);
	tsalt = mem_zalloc(saltlen, NULL);
	if (!tsalt) {
		return ENOMEM;
	}

	memcpy(tsalt, salt, saltlen);

	lock_write_get(ks->lock);
	ks->salt = mem_deref(ks->salt);
	ks->salt = tsalt;
	ks->slen = saltlen;
	ks->update_ts = tmr_jiffies();
	lock_rel(ks->lock);

	return 0;
}

int keystore_set_session_key(struct keystore *ks,
			     uint32_t index,
			     const uint8_t *key,
			     size_t ksz)
{
	uint32_t sz = 0;
	struct le *le = NULL;
	struct keyinfo *kinfo = NULL;
	int err = 0;

	if (!ks || ksz == 0) {
		return EINVAL;
	}

	info("keystore(%p): set_session_key 0x%08x %zu bytes\n", ks, index, ksz);
	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);

	if (is_empty(key, ksz)) {
		warning("keystore(%p): set_session_key key 0x%08x is all zeros\n", ks, index);
	}

	lock_write_get(ks->lock);

	if (ks->current && 
	    index < ks->current->index) {
		info("keystore(%p): set_session_key ignoring old key 0x%08x current %08x\n",
		     ks, index, ks->current->index);
		err = EALREADY;
		goto out;
	}

	LIST_FOREACH(&ks->keyl, le) {
		kinfo = le->data;
		if (index == kinfo->index) {
			if (memcmp(kinfo->skey, key, sz) == 0) {
				//info("keystore(%p): set_session_key key 0x%08x already set, "
				//     "ignoring\n", ks, index);
				err = EALREADY;
				goto out;
			}
			else {
				warning("keystore(%p): set_session_key key 0x%08x changed, "
					"overwriting\n", ks, index);
				memset(kinfo->skey, 0, E2EE_SESSIONKEY_SIZE);
				memcpy(kinfo->skey, key, sz);
				ks->update_ts = tmr_jiffies();
				err = keystore_derive_media_key(ks, kinfo);
				goto out;
			}
		}
	}

	kinfo = mem_zalloc(sizeof(*kinfo), keyinfo_destructor);
	if (!kinfo) {
		err = ENOMEM;
		goto out;
	}

	memset(kinfo->skey, 0, E2EE_SESSIONKEY_SIZE);
	memcpy(kinfo->skey, key, sz);
	kinfo->index = index;

	kinfo->update_ts = tmr_jiffies();
	ks->update_ts = kinfo->update_ts;
	err = keystore_derive_media_key(ks, kinfo);
	if (err)
		goto out;

	err = keystore_append_key(ks, kinfo);
	if (err)
		goto out;

	ks->has_keys = true;
	if (!ks->init) {
		ks->current = kinfo;
		err = keystore_organise(ks);
		if (err)
			goto out;

		ks->init = true;

		LIST_FOREACH(&ks->listeners, le) {
			struct listener *l = le->data;
			l->changedh(ks, l->arg);
		}
	}

	info("keystore(%p): set_session_key 0x%08x set\n",
	     ks, kinfo->index);
out:
	lock_rel(ks->lock);

	return err;
}

int keystore_set_fresh_session_key(struct keystore *ks,
				   uint32_t index,
				   const uint8_t *key,
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

	if (ks->current) {
		memcpy(pkey, ks->current->skey, sz);
		*pindex = ks->current->index;
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

	if (!ks->current || !ks->current->le.next) {
		err = ENOENT;
	}
	else {
		struct keyinfo *next = ks->current->le.next->data;
		memcpy(pkey, next->skey, sz);
		*pindex = next->index;
	}

	lock_rel(ks->lock);

	return err;
}


int keystore_rotate(struct keystore *ks)
{
	struct le *le;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	info("keystore(%p): rotate keys: %u current: %u\n",
	     ks, list_count(&ks->keyl),
	     ks->current ? ks->current->index : 0);

	lock_write_get(ks->lock);

	if (!ks->current) {
		err = ENOENT;
		goto out;
	}

	if (!ks->current->le.next) {
		if (ks->hash_forward) {
			err = keystore_hash_to_key(ks, ks->current->index + 1);
			if (err) {
				goto out;
			}
		}
		else {
			err = ENOENT;
			goto out;
		}
	}
	ks->current = ks->current->le.next->data;
	err = keystore_organise(ks);
	if (err)
		goto out;

	LIST_FOREACH(&ks->listeners, le) {
		struct listener *l = le->data;
		l->changedh(ks, l->arg);
	}
	info("keystore(%p): rotate new key %08x\n",
	     ks, ks->current->index);


out:
	lock_rel(ks->lock);

	return err;
}

int keystore_get_current(struct keystore *ks,
			 uint32_t *pindex,
			 uint64_t *updated_ts)
{
	if (!ks) {
		return EINVAL;
	}

	if (ks->current) {
		*pindex = ks->current->index;
		*updated_ts = ks->update_ts;
		return 0;
	}

	return ENOENT;
}

int keystore_set_current(struct keystore *ks,
			 uint32_t index)
{
	struct le *le;
	bool set = false;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	info("keystore(%p): set_current to: %zu c: %zu\n",
	     ks, index, ks->current);

	lock_write_get(ks->lock);

	LIST_FOREACH(&ks->keyl, le) {
		struct keyinfo *kinfo = le->data;

		if (kinfo->index == index) {
			ks->current = kinfo;
			set = true;
			break;
		}
	}

	if (!set) {
		err = ENOENT;
		goto out;
	}

	err = keystore_organise(ks);
	if (err)
		goto out;

	LIST_FOREACH(&ks->listeners, le) {
		struct listener *l = le->data;
		l->changedh(ks, l->arg);
	}


	info("keystore(%p): set_current key %08x\n",
	     ks, ks->current->index);

out:
	lock_rel(ks->lock);
	return err;
}

int keystore_get_media_key(struct keystore *ks,
			   uint32_t index,
			   uint8_t *pkey,
			   size_t ksz)
{
	uint32_t sz;
	bool found = false;
	struct le *le = NULL;
	struct keyinfo *kinfo = NULL;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	sz = MIN(ksz, E2EE_SESSIONKEY_SIZE);
	memset(pkey, 0, ksz);

	lock_read_get(ks->lock);

	LIST_FOREACH(&ks->keyl, le) {
		kinfo = le->data;
		if (kinfo->index == index) {
			memcpy(pkey, kinfo->mkey, sz);
			found = true;
			if (!ks->current || index > ks->current->index) {
				ks->current = kinfo;
				err = keystore_organise(ks);
				if (err)
					goto out;
			}
			break;
		}
	}

	if (!found && ks->hash_forward) {
		if (!kinfo) {
			warning("keystore(%p): get_media_key nothing to hash forward from\n", ks);
			err = ENOENT;
			goto out;
		}

		if (index > kinfo->index &&
		    index < kinfo->index + NUM_KEYS) {
			err = keystore_hash_to_key(ks, index);
			if (err) {
				goto out;
			}

			kinfo = keystore_get_latest(ks);
			if (!kinfo) {
				goto out;
			}

			memcpy(pkey, kinfo->mkey, sz);
			found = true;
			if (kinfo->index > ks->current->index) {
				ks->current = kinfo;
				err = keystore_organise(ks);
				if (err)
					goto out;
			}
		}
	}

out:
	lock_rel(ks->lock);

	return found ? err : ENOENT;
}

static int keystore_hash_to_key(struct keystore *ks, uint32_t index)
{
	struct keyinfo *kinfo = NULL;
	struct keyinfo *knext = NULL;
	int err = 0;

	if (!ks) {
		return EINVAL;
	}

	kinfo = keystore_get_latest(ks);
	if (!kinfo) {
		err = ENOENT;
		goto out;
	}

	info("keystore(%p): hash_to_key 0x%08x from 0x%08x\n",
	     ks, index, kinfo->index);
	while(kinfo->index < index) {

		err = keystore_derive_session_key(ks,
						  kinfo,
						  &knext);
		if (err) {
			goto out;
		}
		if (!knext) {
			err = ENOMEM;
			goto out;
		}
		err = keystore_derive_media_key(ks, knext);
		if (err) {
			goto out;
		}
		err = keystore_append_key(ks, knext);
		if (err)
			goto out;

		kinfo = knext;
	}

	info("keystore(%p): hash_to_key new key 0x%08x\n",
	     ks, kinfo->index);

out:
	return err;
}

static int keystore_derive_session_key(struct keystore *ks,
				       struct keyinfo* prev,
				       struct keyinfo** pnext)

{
	struct keyinfo *kinfo = NULL;
	int s;

	if (!ks) {
		return EINVAL;
	}

	kinfo = mem_zalloc(sizeof(*kinfo), keyinfo_destructor);
	if (!kinfo) {
		return ENOMEM;
	}

	kinfo->index = prev->index + 1;
	s = HKDF(kinfo->skey, sizeof(kinfo->skey), ks->hash_md,
		 prev->skey, sizeof(prev->skey),
		 ks->salt, ks->slen,
		 SKEY_INFO, SKEY_INFO_LEN);

	kinfo->update_ts = tmr_jiffies();
	ks->update_ts = kinfo->update_ts;

	*pnext = kinfo;
	return s ? 0 : EINVAL;
}

static int keystore_derive_media_key(struct keystore *ks,
				     struct keyinfo* kinfo)
{
	int s = 0;

	if (!ks || !kinfo) {
		return EINVAL;
	}

	s = HKDF(kinfo->mkey, sizeof(kinfo->mkey), ks->hash_md,
		 kinfo->skey, sizeof(kinfo->skey),
		 ks->salt, ks->slen,
		 MKEY_INFO, MKEY_INFO_LEN);

	return s ? 0 : EINVAL;
}

static bool sort_handler(struct le *le1, struct le *le2, void *arg)
{
	struct keyinfo *ki1 = le1 ? le1->data : NULL;
	struct keyinfo *ki2 = le2 ? le2->data : NULL;

	(void)arg;

	if (!ki1 || !ki2) {
		return true;
	}

	return ki1->index <= ki2->index;
}

static int keystore_organise(struct keystore *ks)
{
	struct le *le = NULL;
	if (!ks) {
		return EINVAL;
	}

	list_sort(&ks->keyl, sort_handler, NULL);

	if (!ks->current) {
		return 0;
	}

	le = ks->keyl.head;
	while (le && le->data != ks->current &&
	       le->next && le->next->data != ks->current) {
		mem_deref(le->data);
		le = ks->keyl.head;
	}

	return 0;
}

static int keystore_append_key(struct keystore *ks,
			       struct keyinfo *kinfo)
{

	if (!ks || !kinfo) {
		return EINVAL;
	}

	list_append(&ks->keyl, &kinfo->le, kinfo);
	return keystore_organise(ks);
}

uint32_t keystore_get_max_key(struct keystore *ks)
{
	struct keyinfo *kinfo = NULL;

	if (!ks) {
		return 0;
	}
	
	kinfo = keystore_get_latest(ks);
	if (!kinfo) {
		return 0;
	}

	return kinfo->index;
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

int keystore_set_decrypt_attempted(struct keystore *ks)
{
	if (!ks)
		return EINVAL;

	info("keystore(%p): decrypt_attempted\n", ks);
	lock_write_get(ks->lock);
	ks->decrypt_attempted = true;
	lock_rel(ks->lock);

	return 0;
}

int keystore_set_decrypt_successful(struct keystore *ks)
{
	if (!ks)
		return EINVAL;

	info("keystore(%p): decrypt_successful\n", ks);
	lock_write_get(ks->lock);
	ks->decrypt_successful = true;
	lock_rel(ks->lock);

	return 0;
}

bool keystore_has_keys(struct keystore *ks)
{
	bool keys = false;
	if (!ks)
		return false;

	lock_write_get(ks->lock);
	keys = ks->has_keys;
	lock_rel(ks->lock);

	return keys;
}

void keystore_get_decrypt_states(struct keystore *ks,
				 bool *attempted,
				 bool *successful)
{
	if (!ks || !attempted || !successful)
		return;

	lock_write_get(ks->lock);
	*attempted = ks->decrypt_attempted;
	*successful = ks->decrypt_successful;
	lock_rel(ks->lock);
}

int  keystore_add_listener(struct keystore *ks,
			   ks_cchangedh *changedh,
			   void *arg)
{
	struct listener *listener;

	if (!ks || !changedh)
		return EINVAL;

	listener = mem_zalloc(sizeof(*listener), NULL);
	if (!listener)
		return ENOMEM;

	listener->changedh = changedh;
	listener->arg = arg;

	lock_write_get(ks->lock);
	list_append(&ks->listeners, &listener->le, listener);
	lock_rel(ks->lock);

	return 0;
}

int  keystore_remove_listener(struct keystore *ks,
			      void *arg)
{
	struct le *le;

	if (!ks)
		return EINVAL;

	lock_write_get(ks->lock);
	for(le = ks->listeners.head; le; le = le->next) {
		struct listener *l = le->data;

		if (l->arg == arg) {
			list_unlink(&l->le);
			break;
		}
	}
	lock_rel(ks->lock);

	return 0;
}

bool keystore_rotate_by_time(struct keystore *ks,
			     uint64_t min_ts)
{
	struct le *le = NULL;
	struct keyinfo *latest = NULL;
	int err = 0;

	if (!ks)
		return EINVAL;

	lock_write_get(ks->lock);

	if (ks->current) {
		le = &ks->current->le;
	}
	else {
		le = ks->keyl.head;
	}

	info("keystore(%p): rotate_by_time ts: %llu\n", ks, min_ts);
	while (le) {
		struct keyinfo *kinfo = le->data;
		if (kinfo->update_ts <= min_ts) {
			latest = kinfo;
		}
		le = le->next;
	}

	if (latest && latest != ks->current) {
		ks->current = latest;
		err = keystore_organise(ks);
		if (err)
			goto out;

		LIST_FOREACH(&ks->listeners, le) {
			struct listener *l = le->data;
			l->changedh(ks, l->arg);
		}
		info("keystore(%p): rotate_by_time new key %08x ts: %llu min: %llu\n",
		     ks,
		     ks->current->index,
		     ks->update_ts,
		     min_ts);
	}

out:
	lock_rel(ks->lock);

	return (err || !latest || latest->le.next);
}

