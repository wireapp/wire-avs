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


struct keystore;

int keystore_alloc(struct keystore **pks, bool hash_forward);
int keystore_reset(struct keystore *ks);
int keystore_reset_keys(struct keystore *ks);

int keystore_set_salt(struct keystore *ks,
		      const uint8_t *salt,
		      size_t saltlen);

int keystore_set_session_key(struct keystore *ks,
			     uint32_t index,
			     const uint8_t *key,
			     size_t ksz);

int keystore_set_fresh_session_key(struct keystore *ks,
				   uint32_t index,
				   const uint8_t *key,
				   size_t ksz,
				   uint8_t *salt,
				   size_t saltsz);

int keystore_get_current_session_key(struct keystore *ks,
				     uint32_t *pindex,
				     uint8_t *pkey,
				     size_t ksz);

int keystore_get_next_session_key(struct keystore *ks,
				  uint32_t *pindex,
				  uint8_t *pkey,
				  size_t ksz);

int keystore_rotate(struct keystore *ks);

int keystore_get_current(struct keystore *ks,
			 uint32_t *pindex,
			 uint64_t *updated_ts);

int keystore_set_current(struct keystore *ks,
			 uint32_t index);

int keystore_get_current_media_key(struct keystore *ks,
				   uint32_t *pindex,
				   uint8_t *pkey,
				   size_t ksz);

int keystore_get_media_key(struct keystore *ks,
			   uint32_t index,
			   uint8_t *pkey,
			   size_t ksz);

uint32_t keystore_get_max_key(struct keystore *ks);

bool keystore_rotate_by_time(struct keystore *ks,
			     uint64_t min_ts);

int keystore_generate_iv(struct keystore *ks,
			 const char *clientid,
			 const char *stream_name,
			 uint8_t *iv,
			 size_t ivsz);

bool keystore_has_keys(struct keystore *ks);

int  keystore_set_decrypt_successful(struct keystore *ks);
int  keystore_set_decrypt_attempted(struct keystore *ks);

void keystore_get_decrypt_states(struct keystore *ks,
				 bool *attempted,
				 bool *successful);

typedef void (ks_cchangedh)(struct keystore *ks,
			    void *arg);

int  keystore_add_listener(struct keystore *ks,
			   ks_cchangedh *changedh,
			   void *arg);

int  keystore_remove_listener(struct keystore *ks,
			      void *arg);

