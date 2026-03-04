/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#ifdef __cplusplus
extern "C" {
#endif

enum stats_protocol {
	PROTOCOL_UNKNOWN   = 0,
	PROTOCOL_UDP       = 1,
	PROTOCOL_TCP       = 2,
};

enum stats_candidate {
	CANDIDATE_UNKNOWN   = 0,
	CANDIDATE_HOST      = 1,
	CANDIDATE_SRFLX     = 2,
	CANDIDATE_PRFLX     = 3,
	CANDIDATE_RELAY     = 4,
};

static const char* PROTOCOL_UDP_STR = "UDP";
static const char* PROTOCOL_TCP_STR = "TCP";
static const char* PROTOCOL_UNKNOWN_STR = "Unknown";

static const char* CANDIDATE_HOST_STR = "Host";
static const char* CANDIDATE_SRFLX_STR = "Srflx";
static const char* CANDIDATE_PRFLX_STR = "Prflx";
static const char* CANDIDATE_RELAY_STR = "Relay";
static const char* CANDIDATE_UNKNOWN_STR = "Unknown";

struct stats_jitter {
	float audio;
	float video;
};
struct stats_packet_counts {
	uint32_t audio;
	uint32_t video;
};

#ifdef __cplusplus
}
#endif