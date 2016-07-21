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

#include <re.h>

#include "avs_rtpdump.h"

#include <assert.h>
#include <stdio.h>
#include <limits>

#if defined(_WIN32)
#include <Windows.h>
#include <mmsystem.h>
#else
#include <string.h>
#include <sys/time.h>
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

namespace wire_avs {
const char RTPFILE_VERSION[] = "1.0";
const uint32_t MAX_UWORD32 = 0xffffffff;

// This stucture is specified in the rtpdump documentation.
// This struct corresponds to RD_packet_t in
// http://www.cs.columbia.edu/irt/software/rtptools/
typedef struct
{
	// Length of packet, including this header (may be smaller than plen if not
	// whole packet recorded).
	uint16_t length;
	// Actual header+payload length for RTP, 0 for RTCP.
	uint16_t plen;
	// Milliseconds since the start of recording.
	uint32_t offset;
} RtpDumpPacketHeader;

RtpDump::RtpDump()
{
	pthread_mutex_init(&_mutex,NULL);
	_file = NULL;
	_startTime = 0;
}

RtpDump::~RtpDump()
{
	pthread_mutex_destroy(&_mutex);
	if(_file){
		fclose(_file);
	}
}

int32_t RtpDump::Start(const char* fileNameUTF8)
{
	if (fileNameUTF8 == NULL){
		return -1;
	}

	pthread_mutex_lock(&_mutex);
	if(_file){
		fclose(_file);
	}
	_file = fopen(fileNameUTF8, "wb");
	if (!_file) {
		error("rtpdump: Failed to open file.\n");
		pthread_mutex_unlock(&_mutex);
		return -1;
	}

	// Store start of RTP dump (to be used for offset calculation later).
	_startTime = GetTimeInMS();

	// All rtp dump files start with #!rtpplay.
	char magic[14+1] = "";
	snprintf(magic, sizeof(magic), "#!rtpplay%s \n", RTPFILE_VERSION);
	if (fwrite(magic, sizeof(magic)-1, 1, _file) != 1){
		error("rtpdump: Error writing to file. \n");
		pthread_mutex_unlock(&_mutex);
		return -1;
	}

	// The header according to the rtpdump documentation is sizeof(RD_hdr_t)
	// which is 8 + 4 + 2 = 14 bytes for 32-bit architecture (and 22 bytes on
	// 64-bit architecture). However, Wireshark use 16 bytes for the header
	// regardless of if the binary is 32-bit or 64-bit. Go by the same approach
	// as Wireshark since it makes more sense.
	// http://wiki.wireshark.org/rtpdump explains that an additional 2 bytes
	// of padding should be added to the header.
	char dummyHdr[16];
	memset(dummyHdr, 0, sizeof(dummyHdr));
	if (fwrite(dummyHdr, sizeof(dummyHdr), 1, _file) != 1){
		error("rtpdump: Error writing to file. \n");
		pthread_mutex_unlock(&_mutex);
		return -1;
	}
	pthread_mutex_unlock(&_mutex);
	return 0;
}

int32_t RtpDump::Stop()
{
	pthread_mutex_lock(&_mutex);
	if(_file){
		fclose(_file);
		_file = NULL;
	}
	pthread_mutex_unlock(&_mutex);
	return 0;
}

bool RtpDump::IsActive() const
{
	bool file_opened = false;
	if(_file){
		file_opened = true;
	}
	return file_opened;
}

int32_t RtpDump::DumpPacket(const uint8_t* packet, size_t packetLength)
{
	pthread_mutex_lock(&_mutex);
	if (!IsActive()){
		pthread_mutex_unlock(&_mutex);
		return 0;
	}

	if (packet == NULL){
		pthread_mutex_unlock(&_mutex);
		return -1;
	}

	RtpDumpPacketHeader hdr;
	size_t total_size = packetLength + sizeof hdr;
	if (packetLength < 1 || total_size > std::numeric_limits<uint16_t>::max()){
		pthread_mutex_unlock(&_mutex);
		return -1;
	}

	// If the packet doesn't contain a valid RTCP header the packet will be
	// considered RTP (without further verification).
	bool isRTCP = RTCP(packet);

	// Offset is relative to when recording was started.
	uint32_t offset = GetTimeInMS();
	if (offset < _startTime){
		// Compensate for wraparound.
		offset += MAX_UWORD32 - _startTime + 1;
	} else {
		offset -= _startTime;
	}
	hdr.offset = RtpDumpHtonl(offset);

	hdr.length = RtpDumpHtons((uint16_t)(total_size));
	if (isRTCP){
		hdr.plen = 0;
	} else{
		hdr.plen = RtpDumpHtons((uint16_t)packetLength);
	}

	if (fwrite(&hdr, 1, sizeof(hdr), _file) == -1){
		error("rtpdump: Error writing to file.\n");
		pthread_mutex_unlock(&_mutex);
		return -1;
	}
	if (fwrite(packet, sizeof(uint8_t), packetLength, _file) == -1){
		error("rtpdump: Error writing to file. \n");
		pthread_mutex_unlock(&_mutex);
		return -1;
	}

	pthread_mutex_unlock(&_mutex);
	return 0;
}

bool RtpDump::RTCP(const uint8_t* packet) const
{
	const uint8_t payloadType = packet[1];
	bool is_rtcp = false;

	switch(payloadType)
	{
	case 192:
		is_rtcp = true;
		break;
	case 193: case 195:
		break;
	case 200: case 201: case 202: case 203:
	case 204: case 205: case 206: case 207:
		is_rtcp = true;
		break;
	}
	return is_rtcp;
}

inline uint32_t RtpDump::GetTimeInMS() const
{
#if defined(_WIN32)
    return timeGetTime();
#else
    struct timeval tv;
    struct timezone tz;
    unsigned long val;

    gettimeofday(&tv, &tz);
    val = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return val;
#endif
}

inline uint32_t RtpDump::RtpDumpHtonl(uint32_t x) const
{
    return (x >> 24) + ((((x >> 16) & 0xFF) << 8) + ((((x >> 8) & 0xFF) << 16) +
                                                     ((x & 0xFF) << 24)));
}

inline uint16_t RtpDump::RtpDumpHtons(uint16_t x) const
{
    return (x >> 8) + ((x & 0xFF) << 8);
}
}  // namespace wire_avs
