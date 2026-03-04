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

struct avs_stats;
	
enum stats_proto {
	STATS_PROTO_UNKNOWN   = 0,
	STATS_PROTO_UDP       = 1,
	STATS_PROTO_TCP       = 2,
};

enum stats_cand {
	STATS_CAND_UNKNOWN   = 0,
	STATS_CAND_HOST      = 1,
	STATS_CAND_SRFLX     = 2,
	STATS_CAND_PRFLX     = 3,
	STATS_CAND_RELAY     = 4,
};

	
struct stats_jitter {
	float audio_rx;
	float audio_tx;
	float video_rx;
	float video_tx;
};

struct stats_packet_counts {
	uint32_t audio_rx;
	uint32_t audio_tx;
	uint32_t video_rx;
	uint32_t video_tx;
	uint32_t lost_rx;
	uint32_t lost_tx;
};

struct stats_report {
	enum stats_proto proto;
	enum stats_cand cand;
	struct stats_jitter jitter;
	struct stats_packet_counts packets;
	int audio_level;
	int audio_level_smooth;
	float rtt;
};

int stats_alloc(struct avs_stats **statsp, void *arg);
int stats_update(struct avs_stats *stats, const char *report_json);
int stats_get_report(struct avs_stats *stats, struct stats_report *report);
char *stats_proto_name(enum stats_proto proto);	
char *stats_cand_name(enum stats_cand cand);	
	
#ifdef __cplusplus
}
#endif
