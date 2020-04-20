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

#include "frame_decryptor.h"
#include "frame_hdr.h"

namespace wire {

const size_t BLOCK_SIZE = 32;

FrameDecryptor::FrameDecryptor() :
	_kidx(0),
	_ctx(NULL),
	_keystore(NULL)
{
}

FrameDecryptor::~FrameDecryptor()
{
	_keystore = (struct keystore*)mem_deref(_keystore);
	EVP_CIPHER_CTX_free(_ctx);
	_ctx = NULL;
}

void FrameDecryptor::SetKeystore(struct keystore *keystore)
{
	_keystore = (struct keystore*)mem_ref(keystore);
}

#if 0
void FrameDecryptor::SetKey(const uint8_t *key)
{
#if 0
	info("FrameDecryptor::SetKey\n");
	for (int s = 0; s < 32; s += 8) {
		info("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
		     key[s + 0], key[s + 1], key[s + 2], key[s + 3], 
		     key[s + 4], key[s + 5], key[s + 6], key[s + 7]);
	}
#endif
	if (_ctx) {
		EVP_CIPHER_CTX_free(_ctx);
		_ctx = EVP_CIPHER_CTX_new();
	}
	EVP_DecryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
	_ready = true;
}
#endif

FrameDecryptor::Result FrameDecryptor::Decrypt(cricket::MediaType media_type,
				     const std::vector<uint32_t>& csrcs,
				     rtc::ArrayView<const uint8_t> additional_data,
				     rtc::ArrayView<const uint8_t> encrypted_frame,
				     rtc::ArrayView<uint8_t> frame)
{
	uint32_t kid = 0;
	const uint8_t *key = NULL;
	const char *mt = "unknown";
	int32_t dec_len = 0, blk_len = 0;
	FrameDecryptor::Status err = FrameDecryptor::Status::kOk;
	const uint8_t *src, *iv, *enc;
	uint8_t *dst;
	struct frame_hdr *hdr;
	uint32_t enc_size;

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

	src = encrypted_frame.data();
	dst = frame.data();

	hdr = (struct frame_hdr*)src;

	if (!frame_hdr_check_magic(hdr)) {
		err = FrameDecryptor::Status::kFailedToDecrypt;
		goto out;
	}

	iv = src + sizeof(*hdr);
	enc = iv + BLOCK_SIZE;
	enc_size = frame.size() - sizeof(*hdr) - BLOCK_SIZE;
	kid = frame_hdr_get_key(hdr);

	if (keystore_get_key(_keystore, kid, &key) != 0) {
		warning("FrameDecryptor::Encrypt: cant find key %u\n", kid);
		err = FrameDecryptor::Status::kRecoverable;
		goto out;
	}

	if (kid != _kidx && _ctx) {
		EVP_CIPHER_CTX_free(_ctx);
		_ctx = NULL;
	}

	if (!_ctx) {
		info("FrameDecryptor(%p) decrypting with key %u\n", this, kid);
		_ctx = EVP_CIPHER_CTX_new();
		EVP_DecryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
		_kidx = kid;
	}

	if (!EVP_DecryptInit_ex(_ctx, NULL, NULL, NULL, iv)) {
		warning("FrameDecryptor::Decrypt: init failed\n");
		err = FrameDecryptor::Status::kFailedToDecrypt;
		goto out;
	}

	
	if (!EVP_DecryptUpdate(_ctx, dst, &dec_len, enc, enc_size)) {
		warning("FrameDecryptor::Decrypt: update failed\n");
		err = FrameDecryptor::Status::kFailedToDecrypt;
		goto out;
	}

	if (!EVP_DecryptFinal_ex(_ctx, dst + dec_len, &blk_len)) {
		warning("FrameDecryptor::Decrypt: final failed\n");
		err = FrameDecryptor::Status::kFailedToDecrypt;
		goto out;
	}

#if 0
	info("decrypt %s %u to %u bytes!\n", mt, encrypted_frame.size(),
					     frame_hdr_get_psize(hdr));
	for (int s = 0; s < data_len; s += 8) {
		warning("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
			dst[s + 0], dst[s + 1], dst[s + 2], dst[s + 3], 
			dst[s + 4], dst[s + 5], dst[s + 6], dst[s + 7]);
	}
#endif

out:
	return FrameDecryptor::Result(err, err == FrameDecryptor::Status::kOk ?
					   frame_hdr_get_psize(hdr) : 0);
}

size_t FrameDecryptor::GetMaxPlaintextByteSize(cricket::MediaType media_type,
					       size_t encrypted_frame_size)
{
	return encrypted_frame_size;
}

}  // namespace wire

