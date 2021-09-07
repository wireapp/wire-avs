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

#include <openssl/evp.h>
#include <sodium.h>
#include <assert.h>

static const size_t BLOCK_SIZE = 32;
static const size_t TAG_SIZE   = 16;
static const size_t IV_SIZE    = 12;

struct frame_encryptor
{
	char *userid_hash;
	uint64_t kidx;
	uint64_t frameid;
	EVP_CIPHER_CTX *ctx;
	struct keystore *keystore;
	uint8_t iv[IV_SIZE];
	enum frame_media_type mtype;

	bool frame_recv;
	bool frame_enc;
	uint64_t updated_ts;
};

static void destructor(void *arg)
{
	struct frame_encryptor *enc = arg;
	enc->keystore = (struct keystore*)mem_deref(enc->keystore);
	enc->userid_hash = mem_deref(enc->userid_hash);
	if (enc->ctx) {
		EVP_CIPHER_CTX_free(enc->ctx);
		enc->ctx = NULL;
	}
}

const char *frame_type_name(enum frame_media_type mtype)
{
	switch(mtype) {
	case FRAME_MEDIA_AUDIO:
		return "audio";

	case FRAME_MEDIA_VIDEO:
		return "video";
	}

	return "unknown";
}

int frame_encryptor_xor_iv(const uint8_t *srciv,
			   uint32_t frameid,
			   uint32_t keyid,
			   uint8_t *dstiv,
			   size_t ivsz)
{
	uint8_t fbytes[8];
	size_t npos;

	if (!srciv || !dstiv) {
		return EINVAL;
	}

	assert(ivsz > 7);

	fbytes[0] = (frameid >> 24) & 0xFF;
	fbytes[1] = (frameid >> 16) & 0xFF;
	fbytes[2] = (frameid >>  8) & 0xFF;
	fbytes[3] = (frameid      ) & 0xFF;
	fbytes[4] = (keyid   >> 24) & 0xFF;
	fbytes[5] = (keyid   >> 16) & 0xFF;
	fbytes[6] = (keyid   >>  8) & 0xFF;
	fbytes[7] = (keyid        ) & 0xFF;

	memcpy(dstiv, srciv, ivsz);
	for (npos = 0; npos < 8; npos++) {
		dstiv[npos] ^= fbytes[npos];
	}

	return 0;
}

int frame_encryptor_alloc(struct frame_encryptor **penc,
			  const char *userid_hash,
			  enum frame_media_type mtype)
{
	struct frame_encryptor *enc;
	uint32_t fstart;
	int err = 0;

	if (!penc || !userid_hash) {
		return EINVAL;
	}

	enc = mem_zalloc(sizeof(*enc), destructor);
	if (!enc) {
		return ENOMEM;
	}

	err = str_dup(&enc->userid_hash, userid_hash);
	if (err) {
		goto out;
	}

	randombytes_buf(&fstart, sizeof(fstart));
	enc->frameid = fstart;
	enc->mtype = mtype;
	*penc = enc;

	info("frame_enc(%p): alloc type: %s uid: %s\n",
	     enc,
	     frame_type_name(mtype),
	     userid_hash);
out:
	if (err) {
		mem_deref(enc);
	}
	return err;
}

int frame_encryptor_set_keystore(struct frame_encryptor *enc,
				 struct keystore *keystore)
{
	int err = 0;

	mem_deref(enc->keystore);
	enc->keystore = (struct keystore*)mem_ref(keystore);

	err = keystore_generate_iv(keystore,
				   enc->userid_hash,
				   enc->mtype == FRAME_MEDIA_VIDEO ? "video_iv" : "audio_iv",
				   enc->iv,
				   IV_SIZE);

	return err;
}

int frame_encryptor_encrypt(struct frame_encryptor *enc,
			    uint32_t ssrc,
			    const uint8_t *src,
			    size_t srcsz,
			    uint8_t *dst,
			    size_t *dstsz)
{
	int32_t enc_len = 0, blk_len = 0;
	uint8_t iv[IV_SIZE];
	uint8_t *tag;
	uint64_t kid = 0;
	uint64_t updated_ts = 0;
	uint32_t kid32 = 0;
	size_t hlen = 0;
	uint8_t key[E2EE_SESSIONKEY_SIZE];
	int err = 0;

	enc->frameid = (enc->frameid + 1) & 0xFFFFFFFF;

	if (!enc->keystore) {
		return EINVAL;
	}

	if (!enc->frame_recv) {
		info("frame_enc(%p): encrypt: first frame received "
		     "type: %s uid: %s fid: %u ssrc: %u\n",
		     enc,
		     frame_type_name(enc->mtype),
		     enc->userid_hash,
		     enc->frameid,
		     ssrc);
		enc->frame_recv = true;
	}

	err = keystore_get_current(enc->keystore, &kid32, &updated_ts);
	if (err) {
		err = EAGAIN;
		//warning("frame_enc(%p): encrypt: not ready ks: %p err: %u\n",
		//	enc, enc->keystore, err);
		goto out;
	}
	kid = (uint64_t)kid32;

	if (enc->ctx && (kid != enc->kidx || updated_ts != enc->updated_ts)) {
		EVP_CIPHER_CTX_free(enc->ctx);
		enc->ctx = NULL;
	}

	if (!enc->ctx) {
		err = keystore_get_media_key(enc->keystore, kid, key, sizeof(key));
		if (err) {
			err = EAGAIN;
			//warning("frame_enc(%p): encrypt: not ready ks: %p err: %m\n",
			//	enc, enc->keystore, err);
			goto out;
		}
		//info("frame_encryptor(%p): encrypting with kid=%llu\n", enc, kid);
		enc->ctx = EVP_CIPHER_CTX_new();
		EVP_EncryptInit_ex(enc->ctx, EVP_aes_256_gcm(), NULL, key, NULL);
		enc->kidx = kid;
		enc->updated_ts = updated_ts;
	}

	hlen = frame_hdr_write(dst,
			       frame_encryptor_max_size(enc, srcsz),
			       enc->frameid,
			       kid,
			       ssrc);

	err = frame_encryptor_xor_iv(enc->iv, enc->frameid, kid, iv, IV_SIZE);
	if (err) {
		goto out;
	}

	if (!EVP_EncryptInit_ex(enc->ctx, NULL, NULL, NULL, iv)) {
		warning("frame_enc(%p): encrypt: init failed\n", enc);
		err = ENOSYS;
		goto out;
	}

	if (!EVP_EncryptUpdate(enc->ctx, NULL, &enc_len, dst, hlen)) {
		warning("frame_enc(%p): encrypt: add header failed\n", enc);
		err = EIO;
		goto out;
	}

	dst = dst + hlen;

	if (!EVP_EncryptUpdate(enc->ctx, dst, &enc_len, src, srcsz)) {
		warning("frame_enc(%p): encrypt: update failed\n", enc);
		err = EIO;
		goto out;
	}

	if (!EVP_EncryptFinal_ex(enc->ctx, dst + enc_len, &blk_len)) {
		warning("frame_enc(%p): encrypt: final failed\n", enc);
		err = EBADF;
		goto out;
	}

	enc_len += blk_len;
	tag = dst + enc_len;

	if (!EVP_CIPHER_CTX_ctrl(enc->ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag)) {
		warning("frame_enc(%p): encrypt: set tag failed\n", enc);
		err = EIO;
		goto out;
	}

	*dstsz = hlen + enc_len + TAG_SIZE;

	if (!err && !enc->frame_enc) {
		info("frame_enc(%p): encrypt: first frame encrypted "
		     "type: %s uid: %s fid: %u ssrc: %u\n",
		     enc,
		     frame_type_name(enc->mtype),
		     enc->userid_hash,
		     enc->frameid,
		     ssrc);
		enc->frame_enc = true;
	}
out:
	sodium_memzero(key, E2EE_SESSIONKEY_SIZE);
	return err;
}

size_t frame_encryptor_max_size(struct frame_encryptor *enc,
				size_t srcsz)
{
	return srcsz + BLOCK_SIZE + TAG_SIZE + frame_hdr_max_size();
}

