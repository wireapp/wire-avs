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

#include "frame_encryptor.h"
#include "frame_hdr.h"

namespace wire {

const size_t BLOCK_SIZE = 32;

FrameEncryptor::FrameEncryptor() :
	_keystore(NULL),
	_ctx(NULL),
	_kidx(0)
{
}

FrameEncryptor::~FrameEncryptor()
{
	_keystore = (struct keystore*)mem_deref(_keystore);
	EVP_CIPHER_CTX_free(_ctx);
	_ctx = NULL;
}

void FrameEncryptor::SetKeystore(struct keystore *keystore)
{
	info("FrameEncryptor::SetKeystore %p\n", keystore);
	_keystore = (struct keystore*)mem_ref(keystore);
}

#if 0
void FrameEncryptor::SetKey(const uint8_t *key)
{
#if 0
	info("FrameEncryptor::SetKey\n");
	for (int s = 0; s < 32; s += 8) {
		warning("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
			key[s + 0], key[s + 1], key[s + 2], key[s + 3], 
			key[s + 4], key[s + 5], key[s + 6], key[s + 7]);
	}
#endif
	if (_ctx) {
		EVP_CIPHER_CTX_free(_ctx);
		_ctx = EVP_CIPHER_CTX_new();
	}
	EVP_EncryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
	_ready = true;
}
#endif

int FrameEncryptor::Encrypt(cricket::MediaType media_type,
			    uint32_t ssrc,
			    rtc::ArrayView<const uint8_t> additional_data,
			    rtc::ArrayView<const uint8_t> frame,
			    rtc::ArrayView<uint8_t> encrypted_frame,
			    size_t* bytes_written)
{
	const char *mt = "unknown";
	int32_t enc_len = 0, blk_len = 0;
	uint8_t iv[BLOCK_SIZE];
	struct frame_hdr *hdr;
	const uint8_t *src;
	uint8_t *dst;
	uint32_t kid = 0;
	const uint8_t *key;
	int err = 0;

	switch (media_type) {
	case cricket::MEDIA_TYPE_AUDIO:
		mt = "audio";
		break;
	case cricket::MEDIA_TYPE_VIDEO:
		mt = "video";
		break;
	case cricket::MEDIA_TYPE_DATA:
		mt = "data";
		break;
	}

	if (!_keystore) {
		return EINVAL;
	}

	err = keystore_get_current_key(_keystore, &kid, &key);
	if (err) {
		warning("FrameEncryptor::Encrypt: not ready\n");
		goto out;
	}

	if (kid != _kidx && _ctx) {
		EVP_CIPHER_CTX_free(_ctx);
		_ctx = NULL;
	}

	if (!_ctx) {
		info("FrameEncryptor(%p) encrypting with key %u\n", this, kid);
		_ctx = EVP_CIPHER_CTX_new();
		EVP_EncryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
		_kidx = kid;
	}

	src = frame.data();
	dst = encrypted_frame.data();

	// TODO: randomize iv
	memset(iv, 0, BLOCK_SIZE);

	if (!EVP_EncryptInit_ex(_ctx, NULL, NULL, NULL, iv)) {
		warning("FrameEncryptor::Encrypt: init failed\n");
		err = 1;
		goto out;
	}

	hdr = (struct frame_hdr*)dst;
	frame_hdr_init(hdr, kid, frame.size());
	dst += sizeof(*hdr);

	memcpy(dst, iv, BLOCK_SIZE);
	dst += BLOCK_SIZE;

	if (!EVP_EncryptUpdate(_ctx, dst, &enc_len, frame.data(), frame.size())) {
		warning("FrameEncryptor::Encrypt: update failed\n");
		err = 1;
		goto out;
	}

	
	if (!EVP_EncryptFinal_ex(_ctx, dst + enc_len, &blk_len)) {
		warning("FrameEncryptor::Encrypt: final failed\n");
		err = 1;
		goto out;
	}

	enc_len += blk_len;

	*bytes_written = enc_len + sizeof(*hdr) + BLOCK_SIZE;


#if 0
	info("frameEncryptor::Encrypt %s %u to %u bytes!\n", mt, frame.size(), *bytes_written);

	for (int s = 0; s < frame.size(); s += 8) {
		info("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
		     src[s + 0], src[s + 1], src[s + 2], src[s + 3], 
		     src[s + 4], src[s + 5], src[s + 6], src[s + 7]);
	}
#endif
out:
	return err;
}


size_t FrameEncryptor::GetMaxCiphertextByteSize(cricket::MediaType media_type,
						size_t frame_size)
{
	  return frame_size + BLOCK_SIZE * 2 + sizeof(struct frame_hdr);
}

}  // namespace wire

