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


/*
 * KASE Key Agreement Signaling Extension
 *
 * Ref: https://download.libsodium.org/doc/key_exchange/
 */


kase_assert(crypto_kx_SESSIONKEYBYTES == KASE_SESSIONKEY_SIZE);


struct kase {
	uint8_t publickey[crypto_kx_PUBLICKEYBYTES];
	uint8_t secretkey[crypto_kx_SECRETKEYBYTES];
};


static void kase_destructor(void *data)
{
	struct kase *kase = data;

	sodium_memzero(kase->secretkey, sizeof(kase->secretkey));
}


int kase_alloc(struct kase **kasep)
{
	struct kase *kase;

	if (!kasep)
		return EINVAL;

	kase = mem_zalloc(sizeof(*kase), kase_destructor);
	if (!kase)
		return ENOMEM;

	/* Generate the key pair */
	crypto_kx_keypair(kase->publickey, kase->secretkey);

	info("kase: generated keypair (%zu,%zu bytes)\n",
	     sizeof(kase->publickey), sizeof(kase->secretkey));

	*kasep = kase;

	return 0;
}


const uint8_t *kase_public_key(const struct kase *kase)
{
	return kase ? kase->publickey : NULL;
}


int kase_get_sessionkeys(uint8_t *session_tx, uint8_t *session_rx,
			 struct kase *kase,
			 const uint8_t *publickey_remote,
			 bool is_client,
			 const char *clientid_local,
			 const char *clientid_remote)
{
	uint8_t tmp_tx[KASE_SESSIONKEY_SIZE];
	uint8_t tmp_rx[KASE_SESSIONKEY_SIZE];
	uint8_t chanbind_key[KASE_CHANBIND_SIZE];
	int err = 0;

	if (!session_tx || !session_rx || !kase || !publickey_remote)
		return EINVAL;

	if (!str_isset(clientid_local)) {
		warning("kase: local client id is not set\n");
		return EINVAL;
	}
	if (!str_isset(clientid_remote)) {
		warning("kase: remote client id is not set\n");
		return EINVAL;
	}

	if (is_client) {
		if (crypto_kx_client_session_keys(tmp_rx, tmp_tx,
						  kase->publickey,
						  kase->secretkey,
						  publickey_remote) != 0) {

			warning("kase: Suspicious server public key\n");
			return EPROTO;
		}
	}
	else {
		if (crypto_kx_server_session_keys(tmp_rx, tmp_tx,
						  kase->publickey,
						  kase->secretkey,
						  publickey_remote) != 0) {

			warning("kase: Suspicious client public key\n");
			return EPROTO;
		}

	}

	err = kase_channel_binding(chanbind_key,
				   clientid_local,
				   clientid_remote);
	if (err) {
		warning("kase: channel_binding failed (%m)\n", err);
		return err;
	}

	crypto_generichash(session_tx, KASE_SESSIONKEY_SIZE,
			   tmp_tx, sizeof(tmp_tx),
			   chanbind_key, sizeof(chanbind_key));

	crypto_generichash(session_rx, KASE_SESSIONKEY_SIZE,
			   tmp_rx, sizeof(tmp_rx),
			   chanbind_key, sizeof(chanbind_key));

	/* The secret key is no longer needed */
	sodium_memzero(kase->secretkey, sizeof(kase->secretkey));

	return 0;
}


int kase_print_publickey(struct re_printf *pf, const struct kase *kase)
{
       if (!kase)
	       return 0;

       return base64_print(pf, kase->publickey, sizeof(kase->publickey));
}
