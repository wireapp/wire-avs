/*
* Wire
* Copyright (C) 2019 Wire Swiss GmbH
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

enum frame_media_type {
	FRAME_MEDIA_AUDIO = 0,
	FRAME_MEDIA_VIDEO = 1,
};

const char *frame_type_name(enum frame_media_type mtype);

struct frame_encryptor;

int frame_encryptor_alloc(struct frame_encryptor **penc,
			  const char *userid_hash,
			  enum frame_media_type mtype);

int frame_encryptor_set_keystore(struct frame_encryptor *enc,
				 struct keystore *keystore);

int frame_encryptor_encrypt(struct frame_encryptor *enc,
			    const uint8_t *src,
			    size_t srcsz,
			    uint8_t *dst,
			    size_t *dstsz);

size_t frame_encryptor_max_size(struct frame_encryptor *enc,
				size_t srcsz);


int frame_encryptor_xor_iv(const uint8_t *srciv,
			   uint32_t frameid,
			   uint32_t keyid,
			   uint8_t *dstiv,
			   size_t ivsz);

