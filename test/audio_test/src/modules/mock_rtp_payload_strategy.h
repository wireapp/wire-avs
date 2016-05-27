/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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
/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_MOCK_MOCK_RTP_PAYLOAD_REGISTRY_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_MOCK_MOCK_RTP_PAYLOAD_REGISTRY_H_

#include "gmock/gmock.h"
#include "webrtc/modules/rtp_rtcp/interface/rtp_payload_registry.h"

namespace webrtc {

class MockRTPPayloadStrategy : public RTPPayloadStrategy {
 public:
  MOCK_CONST_METHOD0(CodecsMustBeUnique,
      bool());
  MOCK_CONST_METHOD4(PayloadIsCompatible,
      bool(const ModuleRTPUtility::Payload& payload,
           const uint32_t frequency,
           const uint8_t channels,
           const uint32_t rate));
  MOCK_CONST_METHOD2(UpdatePayloadRate,
      void(ModuleRTPUtility::Payload* payload, const uint32_t rate));
  MOCK_CONST_METHOD1(GetPayloadTypeFrequency, int(
      const ModuleRTPUtility::Payload& payload));
  MOCK_CONST_METHOD5(CreatePayloadType,
      ModuleRTPUtility::Payload*(
          const char payloadName[RTP_PAYLOAD_NAME_SIZE],
          const int8_t payloadType,
          const uint32_t frequency,
          const uint8_t channels,
          const uint32_t rate));
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_RTP_RTCP_SOURCE_MOCK_MOCK_RTP_PAYLOAD_REGISTRY_H_
