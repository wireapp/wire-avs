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
#ifndef AVS_STATS_H
#define AVS_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

struct avs_stats;

enum stats_proto {
	STATS_PROTO_UNKNOWN   = 0,
	STATS_PROTO_UDP       = 1,
	STATS_PROTO_TCP       = 2,
	STATS_PROTO_TLS       = 3,
};

enum stats_cand {
	STATS_CAND_UNKNOWN   = 0,
	STATS_CAND_HOST      = 1,
	STATS_CAND_SRFLX     = 2,
	STATS_CAND_PRFLX     = 3,
	STATS_CAND_RELAY     = 4,
};

enum stats_quality_limitation {
	STATS_QUALITY_LIMITATION_UNKNOWN    = 0,
	STATS_QUALITY_LIMITATION_NONE       = 1,
	STATS_QUALITY_LIMITATION_BANDWIDTH  = 2,
	STATS_QUALITY_LIMITATION_CPU        = 3,
	STATS_QUALITY_LIMITATION_OTHER      = 4,
};

struct stats_rx_tx {
	uint32_t rx;
	uint32_t tx;
};

// jitter in ms
struct stats_jitter {
	struct stats_rx_tx audio;
	struct stats_rx_tx video;
};

struct stats_channel {
	uint32_t audio;
	uint32_t video;
};

struct stats_rtt {
	struct stats_channel remote_inbound;
	uint32_t candidate_pair;
	bool candidate_pair_valid;
};

struct stats_packet_counts {
	struct stats_rx_tx audio;
	struct stats_rx_tx video;
	struct stats_rx_tx audio_lost;
	struct stats_rx_tx video_lost;
};

struct stats_loss_percentages {
	struct stats_rx_tx direction;
	struct stats_channel channel;
};

/* Negative values mean unavailable; dimensions use zero. */
struct stats_tx_media {
	double bitrate;
	double jitter;
	double loss;
	int width;
	int height;
	double fps;
};

struct stats_rx_track {
	struct le le;
	char *id;
	char *track_id;
	char *user_id;
	char *client_id;
	bool video;
	struct stats_tx_media media;
};

struct stats_report {
	/* Owned snapshot from stats_get_report_snapshot; release with stats_report_release.
	 * Callback arguments borrow it only for the duration of the callback. */
	const struct list *rx_tracks;
	struct stats_tx_media tx_audio;
	struct stats_tx_media tx_video;
	/* Resolved endpoints of the selected, nominated, succeeded ICE pair. */
	bool ice_pair_selected;
	enum stats_proto local_proto;
	/* Original candidate protocol for the legacy connection.protocol key. */
	enum stats_proto legacy_local_proto;
	enum stats_cand local_cand;
	enum stats_proto remote_proto;
	enum stats_cand remote_cand;
	enum stats_quality_limitation quality_limitation;
	struct stats_jitter jitter;
	struct stats_channel jitter_buffer_delay;
	struct stats_packet_counts packets;
	struct stats_loss_percentages loss_percentages;
	struct stats_packet_counts packets_per_sec;
	double mos_estimate;
	int audio_level;
	int audio_level_smooth;
	int quality_index;
	struct stats_rtt rtt;
};

int stats_alloc(struct avs_stats **statsp, enum icall_conv_type conv_type, void *arg);
int stats_update(struct avs_stats *stats, const char *report_json);
/* Scalar report only, rx_tracks is NULL; no release required. */
int stats_get_report(struct avs_stats *stats, struct stats_report *report);
/* Includes owned RX tracks. Release before reusing the output report. */
int stats_get_report_snapshot(struct avs_stats *stats, struct stats_report *report);
void stats_report_release(struct stats_report *report);
char *stats_proto_name(enum stats_proto proto);
char *stats_cand_name(enum stats_cand cand);
char *stats_quality_limitation_name(enum stats_quality_limitation qality_limitation);

// Exponential Moving Average
struct avs_ema;
int ema_alloc(struct avs_ema **emap, void *arg);
int ema_get_val(const struct avs_ema *ema, int *val);
int ema_update(struct avs_ema *ema, float data);

// Mos Calculation
double g107_2_estimate(double rtt, double packet_lost, double jitter_buffer_delay);


#ifdef __cplusplus
}
#endif

#endif
