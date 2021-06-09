/*
 * Wire
 * Copyright (C) 2016 Wire Swiss GmbH
 *
 * The Wire Software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * The Wire Software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Wire Software. If not, see <http://www.gnu.org/licenses/>.
 *
 * This module of the Wire Software uses software code from
 * WebRTC (https://chromium.googlesource.com/external/webrtc)
 *
 * *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 * *
 * *  Use of the WebRTC source code on a stand-alone basis is governed by a
 * *  BSD-style license that can be found in the LICENSE file in the root of
 * *  the source tree.
 * *  An additional intellectual property rights grant can be found
 * *  in the file PATENTS.  All contributing project authors to Web RTC may
 * *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTP_DUMP_H
#define RTP_DUMP_H

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

namespace wire_avs {
class RtpDump
{
public:
    RtpDump();
    ~RtpDump();

    int32_t Start(const char* fileNameUTF8);
    int32_t Stop();
    bool IsActive() const;
    int32_t DumpPacket(const uint8_t* packet,
                               size_t packetLength);
private:
    // Return the system time in ms.
    inline uint32_t GetTimeInMS() const;
    // Return x in network byte order (big endian).
    inline uint32_t RtpDumpHtonl(uint32_t x) const;
    // Return x in network byte order (big endian).
    inline uint16_t RtpDumpHtons(uint16_t x) const;

    // Return true if the packet starts with a valid RTCP header.
    // Note: See RtpUtility::RtpHeaderParser::RTCP() for details on how
    //       to determine if the packet is an RTCP packet.
    bool RTCP(const uint8_t* packet) const;

private:
    pthread_mutex_t _mutex;
    FILE* _file;
    uint32_t _startTime;
};
}  // namespace wwire_avs
#endif // RTP_DUMP_H
