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
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_PACING_INCLUDE_MOCK_MOCK_PACED_SENDER_H_
#define WEBRTC_MODULES_PACING_INCLUDE_MOCK_MOCK_PACED_SENDER_H_

#include "gmock/gmock.h"

#include <vector>

#include "webrtc/modules/pacing/include/paced_sender.h"

namespace webrtc {

class MockPacedSender : public PacedSender {
 public:
  MockPacedSender() : PacedSender(NULL, 0, 0) {}
  MOCK_METHOD6(SendPacket, bool(Priority priority,
                                uint32_t ssrc,
                                uint16_t sequence_number,
                                int64_t capture_time_ms,
                                int bytes,
                                bool retransmission));
  MOCK_CONST_METHOD0(QueueInMs, int());
  MOCK_CONST_METHOD0(QueueInPackets, int());
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_PACING_INCLUDE_MOCK_MOCK_PACED_SENDER_H_
