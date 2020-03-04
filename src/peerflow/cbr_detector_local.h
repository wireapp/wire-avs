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


#ifndef CBR_DETECTOR_LOCAL_H_
#define CBR_DETECTOR_LOCAL_H_

#include "api/crypto/frame_encryptor_interface.h"
#include "rtc_base/ref_counted_object.h"

namespace wire {

class CbrDetectorLocal : public rtc::RefCountedObject<webrtc::FrameEncryptorInterface>
{
public:
	CbrDetectorLocal();
	~CbrDetectorLocal();

	int Encrypt(cricket::MediaType media_type,
		    uint32_t ssrc,
		    rtc::ArrayView<const uint8_t> additional_data,
		    rtc::ArrayView<const uint8_t> frame,
		    rtc::ArrayView<uint8_t> encrypted_frame,
		    size_t* bytes_written);

	size_t GetMaxCiphertextByteSize(cricket::MediaType media_type,
					size_t frame_size);

	bool Detected();

private:
	bool detected;
	uint32_t frame_count;
	uint32_t frame_size;
};

}  // namespace wire

#endif  // CBR_DETECTOR_LOCAL_H_
