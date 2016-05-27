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

#ifndef NETEQTEST_DUMMYRTPPACKET_H
#define NETEQTEST_DUMMYRTPPACKET_H

#include "NETEQTEST_RTPpacket.h"

class NETEQTEST_DummyRTPpacket : public NETEQTEST_RTPpacket {
 public:
  virtual int readFromFile(FILE* fp) OVERRIDE;
  virtual int writeToFile(FILE* fp) OVERRIDE;
};

#endif  // NETEQTEST_DUMMYRTPPACKET_H
