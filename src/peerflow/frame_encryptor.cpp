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

namespace wire {

const size_t BLOCK_SIZE = 32;

FrameEncryptor::FrameEncryptor() :
	_ready(false)
{
	_ctx = EVP_CIPHER_CTX_new();
#if 0
	uint8_t key[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
	EVP_EncryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
	_ready = true;
#endif
}

FrameEncryptor::~FrameEncryptor()
{
}

void FrameEncryptor::SetKey(const uint8_t *key)
{
#if 0
	warning("XXXXYYYY encrypt set_key\n");
	for (int s = 0; s < 32; s += 8) {
		warning("XXXXYYYY %08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
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

	if (!_ready) {
		warning("FrameEncryptor::Encrypt: not ready\n");
		return EINVAL;
	}
	const uint8_t *src = frame.data();
	uint8_t *dst = encrypted_frame.data();

	// TODO: randomize iv
	memset(iv, 0, BLOCK_SIZE);

	if (!EVP_EncryptInit_ex(_ctx, NULL, NULL, NULL, iv)) {
		warning("FrameEncryptor::Encrypt: init failed\n");
		err = 1;
		goto out;
	}

	*((uint32_t*)dst) = htonl(frame.size());
	//warning("XXXXYYYY encrypt %02x%02x%02x%02x\n", dst[0], dst[1], dst[2], dst[3]);
	dst += sizeof(uint32_t);

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

	*bytes_written = enc_len + sizeof(uint32_t) + BLOCK_SIZE;

	//warning("XXXXYYYY encrypt %s %u to %u bytes!\n", mt, frame.size(), *bytes_written);

#if 0
	for (int s = 0; s < frame.size(); s += 8) {
		warning("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
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
	  return frame_size + BLOCK_SIZE * 2 + sizeof(uint32_t);
}

}  // namespace wire

