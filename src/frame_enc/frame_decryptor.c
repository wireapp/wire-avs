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

#include <re.h>
#include <avs.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <sodium.h>
#include <assert.h>

static const size_t TAG_SIZE   = 16;
static const size_t IV_SIZE    = 12;

struct frame_decryptor
{
	struct peerflow *pf;
	uint32_t kidx;
	EVP_CIPHER_CTX *ctx;
	struct keystore *keystore;
	uint8_t iv[IV_SIZE];
	enum frame_media_type mtype;

	char *userid_hash;
	uint32_t csrc;
	bool frame_recv;
	bool frame_dec;
};

static void destructor(void *arg)
{
	struct frame_decryptor *dec = arg;
	dec->keystore = (struct keystore*)mem_deref(dec->keystore);
	dec->userid_hash = mem_deref(dec->userid_hash);
	if (dec->ctx) {
		EVP_CIPHER_CTX_free(dec->ctx);
		dec->ctx = NULL;
	}
}

int frame_decryptor_alloc(struct frame_decryptor **pdec,
			  enum frame_media_type mtype)
{
	struct frame_decryptor *dec;
	int err = 0;

	if (!pdec) {
		return EINVAL;
	}

	dec = mem_zalloc(sizeof(*dec), destructor);
	if (!dec)
		return ENOMEM;

	dec->mtype = mtype;
	*pdec = dec;

	info("frame_dec(%p): alloc type: %s\n",
	     dec,
	     frame_type_name(mtype));

	if (err) {
		mem_deref(dec);
	}
	return err;
}

int frame_decryptor_set_uid(struct frame_decryptor *dec,
			    const char *userid_hash)
{
	int err = 0;

	if (!dec || !userid_hash)
		return EINVAL;

	info("frame_dec(%p): set_uid: %s\n",
	     dec,
	     userid_hash);
	dec->userid_hash = mem_deref(dec->userid_hash);

	err = str_dup(&dec->userid_hash, userid_hash);
	if (err)
		goto out;

out:
	return err;
}

int frame_decryptor_set_peerflow(struct frame_decryptor *dec,
				 struct peerflow *pf)
{
	if (!dec || !pf)
		return EINVAL;

	mem_deref(dec->pf);
	dec->pf = pf;

	return 0;
}

int frame_decryptor_set_keystore(struct frame_decryptor *dec,
				 struct keystore *keystore)
{
	int err = 0;

	if (!dec || !keystore)
		return EINVAL;

	mem_deref(dec->keystore);
	dec->keystore = (struct keystore*)mem_ref(keystore);

	return err;
}

int frame_decryptor_decrypt(struct frame_decryptor *dec,
			    uint32_t csrc,
			    const uint8_t *src,
			    size_t srcsz,
			    uint8_t *dst,
			    size_t *dstsz)
{
	uint8_t key[E2EE_SESSIONKEY_SIZE];
	uint8_t iv[IV_SIZE];
	int dec_len = 0, blk_len = 0;
	const uint8_t *enc, *tag;
	int enc_size;
	uint64_t frameid = 0;
	uint32_t fid32 = 0;
	uint64_t kid = 0;
	uint32_t fcsrc = 0;
	size_t hsize = 0;
	bool new_user = false;
	int err = 0;

	if (!dec || !src || !dst) {
		return EINVAL;
	}

	if (srcsz < 1) {
		err = EAGAIN;
		goto out;
	}

	hsize = frame_hdr_read(src, srcsz, &frameid, &kid, &fcsrc);
	fid32 = (uint32_t)frameid;

	if (fcsrc)
		csrc = fcsrc;
	if (csrc != 0 && csrc != dec->csrc) {
		dec->userid_hash = mem_deref(dec->userid_hash);

		err = peerflow_get_userid_for_ssrc(dec->pf,
						   csrc,
						   dec->mtype == FRAME_MEDIA_VIDEO,
						   NULL,
						   NULL,
						   &dec->userid_hash);
		if (err) 
			goto out;

		new_user = true;
		dec->csrc = csrc;
		dec->frame_recv = true;
	}
	else if (!dec->frame_recv) {
		new_user = true;
		dec->frame_recv = true;
		dec->frame_dec = false;
	}

	if(new_user) {
		const char *typename;
		typename = dec->mtype == FRAME_MEDIA_VIDEO ? "video_iv" : "audio_iv";

		if (!dec->userid_hash) {
			err = EAGAIN;
			goto out;
		}
		err = keystore_generate_iv(dec->keystore,
					   dec->userid_hash,
					   typename,
					   dec->iv,
					   IV_SIZE);
		if (err) 
			goto out;

		info("frame_dec(%p): decrypt: first frame received "
		     "type: %s uid: %s fid: %u csrc: %u\n",
		     dec,
		     frame_type_name(dec->mtype),
		     dec->userid_hash,
		     fid32,
		     csrc);
		dec->frame_dec = false;
		keystore_set_decrypt_attempted(dec->keystore);
	}

	err = frame_encryptor_xor_iv(dec->iv, fid32, kid, iv, IV_SIZE);
	if (err) {
		goto out;
	}

	enc = src + hsize;
	enc_size = srcsz - hsize - TAG_SIZE;
	tag = enc + enc_size;

	if (kid != dec->kidx && dec->ctx) {
		EVP_CIPHER_CTX_free(dec->ctx);
		dec->ctx = NULL;
	}

	if (!dec->ctx) {
		if (keystore_get_media_key(dec->keystore, kid, key, sizeof(key)) != 0) {
			//warning("frame_dec(%p): decrypt: cant find key %u\n", dec, kid);
			err = EAGAIN;
			goto out;
		}

		dec->ctx = EVP_CIPHER_CTX_new();
		if (!EVP_DecryptInit_ex(dec->ctx, EVP_aes_256_gcm(), NULL, key, NULL))
			warning("frame_dec(%p): decrypt: init 256_gcm failed\n", dec);
			
		dec->kidx = kid;
	}

	if (!EVP_DecryptInit_ex(dec->ctx, NULL, NULL, NULL, iv)) {
		warning("frame_dec(%p): decrypt: init failed\n", dec);
		err = EIO;
		goto out;
	}

	if (!EVP_CIPHER_CTX_ctrl(dec->ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (uint8_t*)tag)) {
		warning("frame_dec(%p): decrypt: set tag failed\n", dec);
		err = EIO;
		goto out;
	}
	
	if (!EVP_DecryptUpdate(dec->ctx, NULL, &dec_len, src, (int)hsize)) {
		warning("frame_dec(%p): decrypt: add header failed\n", dec);
		err = EIO;
		goto out;
	}
	if (!EVP_DecryptUpdate(dec->ctx, dst, &dec_len, enc, enc_size)) {
		warning("frame_dec(%p): decrypt: update failed\n", dec);
		err = EIO;
		goto out;
	}
	
	if (!EVP_DecryptFinal_ex(dec->ctx, dst + dec_len, &blk_len)) {
		warning("frame_dec(%p): decrypt: final failed\n", dec);
		err = EIO;
		goto out;
	}

	*dstsz = dec_len + blk_len;

out:
	sodium_memzero(key, E2EE_SESSIONKEY_SIZE);
	if (err != 0 && dec->ctx) {
		EVP_CIPHER_CTX_free(dec->ctx);
		dec->ctx = NULL;
	}

	if (!err && !dec->frame_dec) {
		info("frame_dec(%p): decrypt: first frame decrypted "
		     "type: %s uid: %s fid: %u csrc: %u\n",
		     dec,
		     frame_type_name(dec->mtype),
		     dec->userid_hash,
		     fid32,
		     dec->csrc);
		dec->frame_dec = true;
		keystore_set_decrypt_successful(dec->keystore);
	}

	return err;
}

size_t frame_decryptor_max_size(struct frame_decryptor *dec,
				size_t srcsz)
{
	return srcsz;
}

