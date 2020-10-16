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

#include "frame_decryptor_wrapper.h"

namespace wire {

FrameDecryptor::FrameDecryptor(const char *userid_hash,
			       enum frame_media_type mtype)
{
	frame_decryptor_alloc(&_dec, userid_hash, mtype);
	info("FrameDecryptor::ctor: %p\n", this);
}

FrameDecryptor::~FrameDecryptor()
{
	info("FrameDecryptor::dtor: %p\n", this);

	_dec = (struct frame_decryptor*)mem_deref(_dec);
}

int FrameDecryptor::SetKeystore(struct keystore *keystore)
{
	return frame_decryptor_set_keystore(_dec, keystore);
}

FrameDecryptor::Result FrameDecryptor::Decrypt(cricket::MediaType media_type,
				     const std::vector<uint32_t>& csrcs,
				     rtc::ArrayView<const uint8_t> additional_data,
				     rtc::ArrayView<const uint8_t> encrypted_frame,
				     rtc::ArrayView<uint8_t> frame)
{
	size_t decsz = 0;
	int err;

	err = frame_decryptor_decrypt(_dec,
				      encrypted_frame.data(),
				      encrypted_frame.size(),
				      frame.data(),
				      &decsz);

	switch (err) {
	case 0:
		return FrameDecryptor::Result(FrameDecryptor::Status::kOk, decsz);

	case EAGAIN:
		return FrameDecryptor::Result(FrameDecryptor::Status::kRecoverable, 0);

	default:
		return FrameDecryptor::Result(FrameDecryptor::Status::kFailedToDecrypt, 0);
	}
}

size_t FrameDecryptor::GetMaxPlaintextByteSize(cricket::MediaType media_type,
					       size_t encrypted_frame_size)
{
	return frame_decryptor_max_size(_dec, encrypted_frame_size);;
}

}  // namespace wire

