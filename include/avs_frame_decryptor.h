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

struct frame_decryptor;
struct peerflow;

int frame_decryptor_alloc(struct frame_decryptor **pdec,
			  enum frame_media_type mtype);

int frame_decryptor_set_peerflow(struct frame_decryptor *dec,
				 struct peerflow *pf);

int frame_decryptor_set_keystore(struct frame_decryptor *dec,
				 struct keystore *keystore);

int frame_decryptor_set_uid(struct frame_decryptor *dec,
			    const char *userid_hash);

int frame_decryptor_decrypt(struct frame_decryptor *dec,
			    uint32_t csrc,
			    const uint8_t *src,
			    size_t srcsz,
			    uint8_t *dst,
			    size_t *dstsz);

size_t frame_decryptor_max_size(struct frame_decryptor *dec,
				size_t srcsz);

