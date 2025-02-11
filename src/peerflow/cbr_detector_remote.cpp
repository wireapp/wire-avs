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

#include "cbr_detector_remote.h"

#define MAX_MISSMATCH 10
#define MIN_MATCH 100

namespace wire {

CbrDetectorRemote::CbrDetectorRemote(void)
{
}

CbrDetectorRemote::~CbrDetectorRemote()
{
}

CbrDetectorRemote::Result CbrDetectorRemote::Decrypt(cricket::MediaType media_type,
						const std::vector<uint32_t>& csrcs,
						rtc::ArrayView<const uint8_t> additional_data,
						rtc::ArrayView<const uint8_t> encrypted_frame,
						rtc::ArrayView<uint8_t> frame)
{
	const uint8_t *src = encrypted_frame.data();
	uint8_t *dst = frame.data();
	uint32_t data_len = encrypted_frame.size();

	if (media_type == cricket::MEDIA_TYPE_AUDIO) {
		if (data_len == frame_size && frame_size >= 40) {
			missmatch_count = 0;
			frame_count++;
			if (frame_count > MIN_MATCH && !detected) {
				info("CBR detector: remote cbr detected\n");
				detected = true;
			}
		}
		else {
			missmatch_count++;
			if (!detected
			    || (detected && missmatch_count > MAX_MISSMATCH)) {
				frame_count = 0;
				frame_size = data_len;
				missmatch_count = 0;
				if (detected) {
					info("CBR detector: remote cbr detected disabled\n");
					detected = false;
				}
			}
		}
	}

	memcpy(dst, src, data_len);

out:
	return CbrDetectorRemote::Result(CbrDetectorRemote::Status::kOk, data_len);
}

size_t CbrDetectorRemote::GetMaxPlaintextByteSize(cricket::MediaType media_type,
					       size_t encrypted_frame_size)
{
	return encrypted_frame_size;
}

bool CbrDetectorRemote::Detected(void)
{
	return detected;
}
}  // namespace wire

