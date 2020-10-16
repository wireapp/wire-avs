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

int keystore_alloc(struct keystore **pks);
int keystore_reset(struct keystore *ks);
int keystore_reset_keys(struct keystore *ks);

int keystore_set_salt(struct keystore *ks,
		      const uint8_t *salt,
		      size_t saltlen);

int keystore_set_session_key(struct keystore *ks,
			     uint32_t index,
			     uint8_t *key,
			     size_t ksz);

int keystore_set_fresh_session_key(struct keystore *ks,
				   uint32_t index,
				   uint8_t *key,
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

int keystore_get_current(struct keystore *ks, uint32_t *pindex);

int keystore_get_current_media_key(struct keystore *ks,
				   uint32_t *pindex,
				   uint8_t *pkey,
				   size_t ksz);

int keystore_get_media_key(struct keystore *ks,
			   uint32_t index,
			   uint8_t *pkey,
			   size_t ksz);

uint32_t keystore_get_max_key(struct keystore *ks);

int keystore_generate_iv(struct keystore *ks,
			 const char *clientid,
			 const char *stream_name,
			 uint8_t *iv,
			 size_t ivsz);

