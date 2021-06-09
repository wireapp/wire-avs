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

#include "cbr_detector_local.h"

namespace wire {

CbrDetectorLocal::CbrDetectorLocal()
{
}

CbrDetectorLocal::~CbrDetectorLocal()
{
}

int CbrDetectorLocal::Encrypt(cricket::MediaType media_type,
			    uint32_t ssrc,
			    rtc::ArrayView<const uint8_t> additional_data,
			    rtc::ArrayView<const uint8_t> frame,
			    rtc::ArrayView<uint8_t> encrypted_frame,
			    size_t* bytes_written)
{
	const uint8_t *src = frame.data();
	uint8_t *dst = encrypted_frame.data();
	uint32_t data_len = frame.size();

	if (media_type == cricket::MEDIA_TYPE_AUDIO) {
		if (data_len == frame_size && frame_size >= 40) {
			frame_count++;
			if (frame_count > 200 && !detected) {
				info("CBR detector: local cbr detected\n");
				detected = true;
			}
		}
		else {
			frame_count = 0;
			frame_size = data_len;
			if (detected) {
				info("CBR detector: local cbr detected disabled\n");
				detected = false;
			}
		}

	}

	memcpy(dst, src, data_len);
	*bytes_written = data_len;

out:
	return 0;
}


size_t CbrDetectorLocal::GetMaxCiphertextByteSize(cricket::MediaType media_type,
						size_t frame_size)
{
	  return frame_size;
}

bool CbrDetectorLocal::Detected()
{
	return detected;
}

}  // namespace wire

