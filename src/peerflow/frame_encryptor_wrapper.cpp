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

#include "frame_encryptor_wrapper.h"

namespace wire {

FrameEncryptor::FrameEncryptor(const char *userid_hash,
			       enum frame_media_type mtype)
{
	info("FrameEncryptor::ctor: %p\n", this);
	frame_encryptor_alloc(&_enc,
			      userid_hash,
			      mtype);
}

FrameEncryptor::~FrameEncryptor()
{
	info("FrameEncryptor::dtor: %p\n", this);
	_enc = (struct frame_encryptor*)mem_deref(_enc);
}

int FrameEncryptor::SetKeystore(struct keystore *keystore)
{
	return frame_encryptor_set_keystore(_enc, keystore);
}

int FrameEncryptor::Encrypt(cricket::MediaType media_type,
			    uint32_t ssrc,
			    rtc::ArrayView<const uint8_t> additional_data,
			    rtc::ArrayView<const uint8_t> frame,
			    rtc::ArrayView<uint8_t> encrypted_frame,
			    size_t* bytes_written)
{
	return frame_encryptor_encrypt(_enc,
				       frame.data(),
				       frame.size(),
				       encrypted_frame.data(),
				       bytes_written);;
}


size_t FrameEncryptor::GetMaxCiphertextByteSize(cricket::MediaType media_type,
						size_t frame_size)
{
	return frame_encryptor_max_size(_enc, frame_size);
}

}  // namespace wire

