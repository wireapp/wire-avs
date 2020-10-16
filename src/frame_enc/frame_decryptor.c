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

static const size_t TAG_SIZE   = 16;
static const size_t IV_SIZE    = 12;

struct frame_decryptor
{
	char *userid_hash;
	uint32_t kidx;
	EVP_CIPHER_CTX *ctx;
	struct keystore *keystore;
	uint8_t iv[IV_SIZE];
	enum frame_media_type mtype;
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
			  const char *userid_hash,
			  enum frame_media_type mtype)
{
	struct frame_decryptor *dec;
	int err = 0;

	dec = mem_zalloc(sizeof(*dec), destructor);
	if (!dec) {
		return ENOMEM;
	}

	err = str_dup(&dec->userid_hash, userid_hash);
	if (err) {
		goto out;
	}

	dec->mtype = mtype;

	*pdec = dec;

out:
	if (err) {
		mem_deref(dec);
	}
	return err;
}

int frame_decryptor_set_keystore(struct frame_decryptor *dec,
				 struct keystore *keystore)
{
	int err = 0;

	mem_deref(dec->keystore);
	dec->keystore = (struct keystore*)mem_ref(keystore);

	err = keystore_generate_iv(dec->keystore,
				   dec->userid_hash,
				   dec->mtype == FRAME_MEDIA_VIDEO ? "video_iv" : "audio_iv",
				   dec->iv,
				   IV_SIZE);

	return err;
}

int frame_decryptor_decrypt(struct frame_decryptor *dec,
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
	size_t hsize = 0;
	int err = 0;

	if (!dec || !src || !dst) {
		return EINVAL;
	}

	if (srcsz < 1) {
		err = EAGAIN;
		goto out;
	}

	hsize = frame_hdr_read(src, &frameid, &kid);

	fid32 = (uint32_t)frameid;
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
			//warning("frame_decryptor_decrypt(%p): cant find key %u\n", dec, kid);
			err = EAGAIN;
			goto out;
		}

		dec->ctx = EVP_CIPHER_CTX_new();
		if (!EVP_DecryptInit_ex(dec->ctx, EVP_aes_256_gcm(), NULL, key, NULL))
			warning("frame_decryptor_decrypt(%p): init 256_gcm failed\n", dec);
			
		dec->kidx = kid;
	}

	if (!EVP_DecryptInit_ex(dec->ctx, NULL, NULL, NULL, iv)) {
		warning("frame_decryptor_decrypt(%p): init failed\n", dec);
		err = EIO;
		goto out;
	}

	if (!EVP_CIPHER_CTX_ctrl(dec->ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (uint8_t*)tag)) {
		warning("frame_decryptor_decrypt(%p): set tag failed\n", dec);
		err = EIO;
		goto out;
	}
	
	if (!EVP_DecryptUpdate(dec->ctx, NULL, &dec_len, src, (int)hsize)) {
		warning("frame_decryptor_decrypt(%p): add header failed\n", dec);
		err = EIO;
		goto out;
	}
	if (!EVP_DecryptUpdate(dec->ctx, dst, &dec_len, enc, enc_size)) {
		warning("frame_decryptor_decrypt(%p): update failed\n", dec);
		err = EIO;
		goto out;
	}
	
	if (!EVP_DecryptFinal_ex(dec->ctx, dst + dec_len, &blk_len)) {
		warning("frame_decryptor_decrypt(%p): final failed\n", dec);
		err = EIO;
		goto out;
	}

	*dstsz = dec_len + blk_len;
#if 0
	info("decrypt %s frame %u %u to %u bytes!\n", mt, frameid, encrypted_frame.size(),
					     dec_len);
	for (int s = 0; s < dec_len; s += 8) {
		warning("D %08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
			dst[s + 0], dst[s + 1], dst[s + 2], dst[s + 3], 
			dst[s + 4], dst[s + 5], dst[s + 6], dst[s + 7]);
	}
#endif

out:
	sodium_memzero(key, E2EE_SESSIONKEY_SIZE);
	if (err != 0 && dec->ctx) {
		EVP_CIPHER_CTX_free(dec->ctx);
		dec->ctx = NULL;
	}
	return err;
}

size_t frame_decryptor_max_size(struct frame_decryptor *dec,
				size_t srcsz)
{
	return srcsz;
}

