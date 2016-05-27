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

#ifndef WEBRTC_TEST_MOCK_TRANSPORT_H_
#define WEBRTC_TEST_MOCK_TRANSPORT_H_

#include "gmock/gmock.h"
#include "webrtc/transport.h"

namespace webrtc {

class MockTransport : public webrtc::Transport {
 public:
  MOCK_METHOD3(SendPacket,
      int(int channel, const void* data, int len));
  MOCK_METHOD3(SendRTCPPacket,
      int(int channel, const void* data, int len));
};
}  // namespace webrtc
#endif  // WEBRTC_TEST_MOCK_TRANSPORT_H_
