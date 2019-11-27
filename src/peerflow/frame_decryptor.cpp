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

namespace wire {

const size_t BLOCK_SIZE = 32;

FrameDecryptor::FrameDecryptor()
	: _ready(false)
{
	_ctx = EVP_CIPHER_CTX_new();
#if 0
	uint8_t key[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
	EVP_DecryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
	_ready = true;
#endif
}

FrameDecryptor::~FrameDecryptor()
{
}

void FrameDecryptor::SetKey(const uint8_t *key)
{
#if 0
	warning("XXXXYYYY decrypt set_key\n");
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
	EVP_DecryptInit_ex(_ctx, EVP_aes_256_cbc(), NULL, key, NULL);
	_ready = true;
}

int FrameDecryptor::Decrypt(cricket::MediaType media_type,
			    const std::vector<uint32_t>& csrcs,
			    rtc::ArrayView<const uint8_t> additional_data,
			    rtc::ArrayView<const uint8_t> encrypted_frame,
			    rtc::ArrayView<uint8_t> frame,
			    size_t* bytes_written)
{
	const char *mt = "unknown";
	int32_t dec_len = 0, blk_len = 0;
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
		return EINVAL;
	}

	const uint8_t *src = encrypted_frame.data();
	uint8_t *dst = frame.data();

	uint32_t data_len = ntohl(*(uint32_t*)src);
	src += sizeof(uint32_t);
	uint32_t payload_size = frame.size() - sizeof(uint32_t) - BLOCK_SIZE;

	if (!EVP_DecryptInit_ex(_ctx, NULL, NULL, NULL, src)) {
		warning("FrameDecryptor::Decrypt: init failed\n");
		err = 1;
		goto out;
	}

	src += BLOCK_SIZE;
	
	if (!EVP_DecryptUpdate(_ctx, dst, &dec_len, src, payload_size)) {
		warning("FrameDecryptor::Decrypt: update failed\n");
		err = 1;
		goto out;
	}

	if (!EVP_DecryptFinal_ex(_ctx, dst + dec_len, &blk_len)) {
		warning("FrameDecryptor::Decrypt: final failed\n");
		err = 1;
		goto out;
	}

	*bytes_written = data_len;
#if 0
	warning("XXXXYYYY decrypt %s %u to %u bytes!\n", mt, encrypted_frame.size(), *bytes_written);
	for (int s = 0; s < data_len; s += 8) {
		warning("%08x %02x %02x %02x %02x %02x %02x %02x %02x\n", s,
			dst[s + 0], dst[s + 1], dst[s + 2], dst[s + 3], 
			dst[s + 4], dst[s + 5], dst[s + 6], dst[s + 7]);
	}
#endif

out:
	return 0;
}

size_t FrameDecryptor::GetMaxPlaintextByteSize(cricket::MediaType media_type,
					       size_t encrypted_frame_size)
{
	return encrypted_frame_size;
}

}  // namespace wire

