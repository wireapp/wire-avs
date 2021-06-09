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


#define KASE_SESSIONKEY_SIZE 32
#define KASE_CHANBIND_SIZE    8


struct kase;


int kase_alloc(struct kase **kasep);
const uint8_t *kase_public_key(const struct kase *kase);
int kase_print_publickey(struct re_printf *pf, const struct kase *kase);
int kase_get_sessionkeys(uint8_t *session_tx, uint8_t *session_rx,
			 struct kase *kase,
			 const uint8_t *publickey_remote,
			 bool is_client,
			 const char *clientid_local,
			 const char *clientid_remote);


int kase_channel_binding(uint8_t hash[KASE_CHANBIND_SIZE],
			 const char *clientid_local,
			 const char *clientid_remote);
