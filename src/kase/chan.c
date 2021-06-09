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
#include <sodium.h>
#include <avs_log.h>
#include <avs_kase.h>
#include "priv_kase.h"


int kase_channel_binding(uint8_t hash[KASE_CHANBIND_SIZE],
			 const char *clientid_local,
			 const char *clientid_remote)
{
	static const uint8_t null_key[crypto_shorthash_KEYBYTES] = {0};
	uint8_t h1[crypto_shorthash_BYTES];
	uint8_t h2[crypto_shorthash_BYTES];
	size_t i;
	int r;
    char clientid_anon1[ANON_CLIENT_LEN];
    char clientid_anon2[ANON_CLIENT_LEN];

	kase_assert(sizeof(h1) == KASE_CHANBIND_SIZE);
	kase_assert(sizeof(h2) == KASE_CHANBIND_SIZE);

	if (!hash || !clientid_local || !clientid_remote)
		return EINVAL;

	if (!str_isset(clientid_local)) {
		warning("kase: channel_bind: local client id is not set\n");
		return EINVAL;
	}
	if (!str_isset(clientid_remote)) {
		warning("kase: channel_bind: remote client id is not set\n");
		return EINVAL;
	}

	r = crypto_shorthash(h1, (uint8_t *)clientid_local,
			     str_len(clientid_local), null_key);
	if (r != 0)
		return EPROTO;
	r = crypto_shorthash(h2, (uint8_t *)clientid_remote,
			     str_len(clientid_remote), null_key);
	if (r != 0)
		return EPROTO;

	/* XOR the hash values as the client IDs can be swapped */
	for (i=0; i<KASE_CHANBIND_SIZE; i++) {
		hash[i] = h1[i] ^ h2[i];
	}

	debug("kase: channel binding:  \"%s\" \"%s\"\n",
	      anon_client(clientid_anon1, clientid_local),
          anon_client(clientid_anon2, clientid_remote));

	return 0;
}
