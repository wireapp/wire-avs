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


#ifdef HAVE_CRYPTOBOX

/* cryptobox */

struct cryptobox;

int  cryptobox_alloc(struct cryptobox **cbp, const char *storedir);
void cryptobox_dump(const struct cryptobox *cb);

int  cryptobox_generate_prekey(struct cryptobox *cb,
			       uint8_t *key, size_t *sz, uint16_t id);

int  cryptobox_session_add_send(struct cryptobox *cb,
				const char *remote_userid,
				const char *remote_clientid,
				const char *local_clientid,
				const uint8_t *peer_key, size_t peer_key_len);
int  cryptobox_session_add_recv(struct cryptobox *cb,
				const char *remote_userid,
				const char *remote_clientid,
				const char *local_clientid,
				uint8_t *plain, size_t *plain_len,
				const uint8_t *cipher, size_t cipher_len);
struct session *cryptobox_session_find(struct cryptobox *cb,
				       const char *remote_userid,
				       const char *remote_clientid,
				       const char *local_clientid);
int cryptobox_session_encrypt(struct cryptobox *cb, struct session *sess,
			      uint8_t *cipher, size_t *cipher_len,
			      const uint8_t *plain, size_t plain_len);
int cryptobox_session_decrypt(struct cryptobox *cb, struct session *sess,
			      uint8_t *plain, size_t *plain_len,
			      const uint8_t *cipher, size_t cipher_len);

#endif
