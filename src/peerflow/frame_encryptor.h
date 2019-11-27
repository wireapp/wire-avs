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


#ifndef FRAME_ENCRYPTOR_H_
#define FRAME_ENCRYPTOR_H_

#include "api/crypto/frameencryptorinterface.h"
#include "rtc_base/refcountedobject.h"
#include <openssl/evp.h>
#include <openssl/cipher.h>

namespace wire {

class FrameEncryptor : public rtc::RefCountedObject<webrtc::FrameEncryptorInterface>
{
public:
	FrameEncryptor();
	~FrameEncryptor();

	void SetKey(const uint8_t *key);

	int Encrypt(cricket::MediaType media_type,
		    uint32_t ssrc,
		    rtc::ArrayView<const uint8_t> additional_data,
		    rtc::ArrayView<const uint8_t> frame,
		    rtc::ArrayView<uint8_t> encrypted_frame,
		    size_t* bytes_written);

	size_t GetMaxCiphertextByteSize(cricket::MediaType media_type,
					size_t frame_size);

private:
	EVP_CIPHER_CTX *_ctx;
	bool _ready;
};

}  // namespace wire

#endif  // FRAME_ENCRYPTOR_H_
