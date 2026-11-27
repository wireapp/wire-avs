
#include <re.h>
#include "avs.h"

#include <math.h>

// A reference timestamp (9.9.2001) in microseconds
const double REF_TS_US = 1000000000000000;

static enum stats_proto stats_parse_poto(const char *type)
{
	if (type == NULL) {
		return STATS_PROTO_UNKNOWN;
	}

	if (streq(type, "udp")) {
		return STATS_PROTO_UDP;
	}
	else if (streq(type, "tcp")) {
		return STATS_PROTO_TCP;
	}
	else { 
		return STATS_PROTO_UNKNOWN;
	}
}

static enum stats_cand stats_parse_cand(const char *cand)
{
	if (cand == NULL) {
		return STATS_CAND_UNKNOWN;
	}

	if (streq(cand, "host")) {
		return STATS_CAND_HOST;
	}
	else if (streq(cand, "srflx")) {
		return STATS_CAND_SRFLX;
	}
	else if (streq(cand, "prflx")) {
		return STATS_CAND_PRFLX;
	}
	else if (streq(cand, "relay")) {
		return STATS_CAND_RELAY;
	}
	else { 
		return STATS_CAND_UNKNOWN;
	}
}

static enum stats_quality_limitation stats_parse_quality_limitation(const char *qualty_limitation)
{
	if (qualty_limitation == NULL) {
		return STATS_QUALITY_LIMITATION_UNKNOWN;
	}

	if (streq(qualty_limitation, "none")) {
		return STATS_QUALITY_LIMITATION_NONE;
	}
	else if (streq(qualty_limitation, "bandwidth")) {
		return STATS_QUALITY_LIMITATION_BANDWIDTH;
	}
	else if (streq(qualty_limitation, "cpu")) {
		return STATS_QUALITY_LIMITATION_CPU;
	}
	else if (streq(qualty_limitation, "other")) {
		return STATS_QUALITY_LIMITATION_OTHER;
	}
	else { 
		return STATS_QUALITY_LIMITATION_UNKNOWN;
	}
}

enum stats_type {
	STATS_TYPE_UNKNOWN,
	STATS_TYPE_INBOUND_RTP,
	STATS_TYPE_OUTBOUND_RTP,
	STATS_TYPE_REMOTE_INBOUND_RTP,
	STATS_TYPE_MEDIA_SOURCE,
	STATS_TYPE_CANDIDATE_PAIR,
	STATS_TYPE_LOCAL_CANDIDATE,
	STATS_TYPE_REMOTE_CANDIDATE,
	STATS_TYPE_TRANSPORT,
};

static enum stats_type stats_parse_type(const char *type)
{
	if (type == NULL) {
		return STATS_TYPE_UNKNOWN;
	}

	if (streq(type, "inbound-rtp")) {
		return STATS_TYPE_INBOUND_RTP;
	}
	else if (streq(type, "outbound-rtp")) {
		return STATS_TYPE_OUTBOUND_RTP;
	}
	else if (streq(type, "remote-inbound-rtp")) {
		return STATS_TYPE_REMOTE_INBOUND_RTP;
	}
	else if (streq(type, "media-source")) {
		return STATS_TYPE_MEDIA_SOURCE;
	}
	else if (streq(type, "candidate-pair")) {
		return STATS_TYPE_CANDIDATE_PAIR;
	}
	else if (streq(type, "local-candidate")) {
		return STATS_TYPE_LOCAL_CANDIDATE;
	}
	else if (streq(type, "remote-candidate")) {
		return STATS_TYPE_REMOTE_CANDIDATE;
	}
	else if (streq(type, "transport")) {
		return STATS_TYPE_TRANSPORT;
	}
	else { 
		return STATS_TYPE_UNKNOWN;
	}
}

enum stats_kind {
	STATS_KIND_UNKNOWN,
	STATS_KIND_AUDIO,
	STATS_KIND_VIDEO,
};

static enum stats_kind stats_parse_kind(const char *kind)
{
	if (kind == NULL) {
		return STATS_KIND_UNKNOWN;
	}

	if (streq(kind, "audio")) {
		return STATS_KIND_AUDIO;
	}
	else if (streq(kind, "video")) {
		return STATS_KIND_VIDEO;
	}
	else { 
		return STATS_KIND_UNKNOWN;
	}
}

enum stats_state {
	STATS_STATE_UNKNOWN,
	STATS_STATE_SUCCEEDED,
};

static enum stats_state stats_parse_state(const char *state)
{
	if (state == NULL) {
		return STATS_STATE_UNKNOWN;
	}

	if (streq(state, "succeeded")) {
		return STATS_STATE_SUCCEEDED;
	}
	else {
		return STATS_STATE_UNKNOWN;
	}
}

struct stats_inbound_rtp {
	char *id, *track_id, *user_id, *client_id;
	uint64_t bytes_received;
	bool has_bytes, has_packets, has_lost, has_jitter;
	int width, height;
	double fps;
	enum stats_kind kind;
	int packets_received;
	int packets_lost;
	double jitter;
	double jitter_buffer_delay;
	int jitter_buffer_emitted_count;
	double timestamp;
	struct le le;
};

struct stats_outbound_rtp {
	char *id;
	uint64_t bytes_sent;
	bool has_bytes_sent;
	int width;
	int height;
	double fps;
	enum stats_kind kind;
	int packets_sent;
	double timestamp;
	enum stats_quality_limitation quality_limitation;
	struct le le;
};

struct stats_remote_inbound_rtp {
	char *local_id;
	double fraction_lost;
	bool has_jitter;
	enum stats_kind kind;
	int packets_lost;
	double jitter;
	double timestamp;
	double rtt;
	struct le le;
};

struct stats_audio_source {
	double level;
	struct le le;
};

struct stats_candidate_pair {
	enum stats_state state;
	bool nominated;
	bool selected;
	bool has_rtt;
	double current_rtt;
	char *id;
	char *local_candidate_id;
	char *remote_candidate_id;
	struct le le;
};

static void candidate_pair_destructor(void *arg)
{
	struct stats_candidate_pair *item = arg;
	mem_deref(item->id);
	mem_deref(item->local_candidate_id);
	mem_deref(item->remote_candidate_id);
}

struct stats_local_candidate {
	char *id;
	enum stats_proto proto;
	enum stats_cand cand;
	bool relay_tls;
	struct le le;
};

static void local_candidate_destructor(void *arg)
{
	struct stats_local_candidate *item = arg;
	mem_deref(item->id);
}

struct stats_transport {
	char *selected_pair_id;
	struct le le;
};

static void transport_destructor(void *arg)
{
	struct stats_transport *item = arg;
	mem_deref(item->selected_pair_id);
}

struct stats_obj {
	struct list audio_source;
	struct list inbound_rtp;
	struct list outbound_rtp;
	struct list remote_inbound_rtp;
	struct list candidate_pair;
	struct list local_candidate;
	struct list remote_candidate;
	struct list transport;
};

struct stats_last_bytes {
	struct le le;
	char *id;
	uint64_t bytes_sent;
	double timestamp;
	char *user_id, *client_id;
	int packets_received, packets_lost;
	bool has_bytes, has_lost;
};

static void last_bytes_destructor(void *arg)
{
	struct stats_last_bytes *item = arg;
	mem_deref(item->id);
	mem_deref(item->user_id);
	mem_deref(item->client_id);
}

struct avs_stats {
	struct lock *lock;
	struct list last_bytes;
	struct list last_rx;
	struct list rx_tracks;
	enum icall_conv_type conv_type;
	struct stats_report report;
	struct stats_packet_counts last_packets;
	uint64_t last_timestamp_in_ms;
	struct avs_ema *ema;

	void *arg;
};

static void reset_tx(struct stats_report *report)
{
	report->tx_audio.bitrate = report->tx_audio.jitter =
		report->tx_audio.loss = report->tx_audio.fps = -1;
	report->tx_video.bitrate = report->tx_video.jitter =
		report->tx_video.loss = report->tx_video.fps = -1;
}

static uint32_t calculate_loss_percentage(uint32_t lost_packets, uint32_t total_packets) {
	if (total_packets)
		return (lost_packets / (float)total_packets) * 100;
	else
		return 0;
}

// Three known timestamp formats
// Native provides timestamp in microseconds	"timestamp": 1771376014893170
// Chrome provides timestamp in milliseconds	"timestamp": 1772181428273.284
// Firefox provides timestamp in mlliseconds	"timestamp": 1772181428273
static uint64_t normalize_timestamp_to_ms(double timestamp) {
	// try to detect if timestamp is in microseconds with
	// comparing to an anchor point
	if (timestamp > REF_TS_US) {
		return (uint64_t)(timestamp / 1000);
	}

	return (uint64_t)timestamp;
}

static double safe_normalize(double divident, double divisor) {
	return divisor ? 1000 * (divident / divisor) : 0;
}


// Helper fnc to update per sec statistics
static void update_per_sec_stats(struct stats_packet_counts *packets_per_sec, const struct stats_packet_counts *diff, uint64_t time_difference_in_ms) {
	if (!packets_per_sec || !diff || !time_difference_in_ms) {
		return;
	}

	packets_per_sec->audio.tx = 1000 * diff->audio.tx / time_difference_in_ms;
	packets_per_sec->audio.rx = 1000 * diff->audio.rx / time_difference_in_ms;
	packets_per_sec->video.tx = 1000 * diff->video.tx / time_difference_in_ms;
	packets_per_sec->video.rx = 1000 * diff->video.rx / time_difference_in_ms;
	packets_per_sec->audio_lost.tx = 1000 * diff->audio_lost.tx / time_difference_in_ms;
	packets_per_sec->audio_lost.rx = 1000 * diff->audio_lost.rx / time_difference_in_ms;
	packets_per_sec->video_lost.tx = 1000 * diff->video_lost.tx / time_difference_in_ms;
	packets_per_sec->video_lost.rx = 1000 * diff->video_lost.rx / time_difference_in_ms;
}


// Helper fnc to calculate loss percentages and update report section accordingly
static void update_loss_percentages(struct stats_loss_percentages *loss_percentages, const struct stats_packet_counts *diff) {
	if (!loss_percentages || !diff) {
		return;
	}

	// Notice that rx and tx parts contain different information wrt loss percentage
	// rx stats have packets_received & packets_lost => packets_lost/(packets_received + packets_lost)
	// tx stats have packets_send & packets_lost => packets_lost/packets_send

	loss_percentages->direction.tx = calculate_loss_percentage(
		(diff->audio_lost.tx + diff->video_lost.tx), (diff->audio.tx + diff->video.tx));
	loss_percentages->direction.rx = calculate_loss_percentage(
		(diff->audio_lost.rx + diff->video_lost.rx), (diff->audio.rx + diff->audio_lost.rx + diff->video.rx + diff->video_lost.rx));
	loss_percentages->channel.audio = calculate_loss_percentage(
		(diff->audio_lost.rx + diff->audio_lost.tx), (diff->audio.rx + diff->audio_lost.rx + diff->audio.tx));
	loss_percentages->channel.video = calculate_loss_percentage(
		(diff->video_lost.rx + diff->video_lost.tx), (diff->video.rx + diff->video_lost.rx + diff->video.tx ));
}


static int read_packet_stats_and_jitter(struct avs_stats *stats, const struct stats_obj *stats_obj)
{
	struct le *le = NULL;
	double audio_jitter = 0;
	int aj_count = 0;
	double audio_jitter_buffer_delay = 0;
	int audio_jitter_buffer_emitted_count = 0;
	double video_jitter = 0;
	int vj_count = 0;
	double video_jitter_buffer_delay = 0;
	int video_jitter_buffer_emitted_count = 0;
	double timestamp = 0;


	if (!stats || !stats_obj) {
		return EINVAL;
	}

	/*
	  Calculation of packet and loss statistics
	  1. read json stats into report
	      webrtc json -> stats.report
	    1.1 rx jitter calculation is done wrt mean of incoming rtps
	        that have nonzero received packets.
	  2. calculate interval percentage for packet loss into tmp variables
	      loss_tx = calculate_loss_percentage(...)
	      loss_rx = calculate_loss_percentage(...)
	  3. save current packet cumulatives into last
	  4. update report.packet.lost with percentage loss
	      report-packets-loss = { loss_tx, loss_rx}
	*/

	// 1. read json stats into report
	LIST_FOREACH(&stats_obj->inbound_rtp, le) {
		const struct stats_inbound_rtp *data = (struct stats_inbound_rtp *)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.packets.audio.rx += data->packets_received;
			stats->report.packets.audio_lost.rx += data->packets_lost;
			timestamp = max(timestamp, data->timestamp);
			if (data->packets_received) {
				audio_jitter += data->jitter;
				aj_count++;
				audio_jitter_buffer_delay += data->jitter_buffer_delay;
				audio_jitter_buffer_emitted_count += data->jitter_buffer_emitted_count;
			}
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.packets.video.rx += data->packets_received;
			stats->report.packets.video_lost.rx += data->packets_lost;
			timestamp = max(timestamp, data->timestamp);
			if (data->packets_received) {
				video_jitter += data->jitter;
				vj_count++;
				video_jitter_buffer_delay += data->jitter_buffer_delay;
				video_jitter_buffer_emitted_count += data->jitter_buffer_emitted_count;
			}
		}
	}

	LIST_FOREACH(&stats_obj->outbound_rtp, le) {
		const struct stats_outbound_rtp *data = (struct stats_outbound_rtp *)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.packets.audio.tx += data->packets_sent;
			timestamp = max(timestamp, data->timestamp);
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.packets.video.tx += data->packets_sent;
			timestamp = max(timestamp, data->timestamp);
			if (data->packets_sent) {
				stats->report.quality_limitation = data->quality_limitation;
			}
		}
	}

	LIST_FOREACH(&stats_obj->remote_inbound_rtp, le) {
		const struct stats_remote_inbound_rtp *data = (struct stats_remote_inbound_rtp *)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.packets.audio_lost.tx += data->packets_lost;
			stats->report.jitter.audio.tx = max(stats->report.jitter.audio.tx, (1000 * data->jitter));
			timestamp = max(timestamp, data->timestamp);
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.packets.video_lost.tx += data->packets_lost;
			stats->report.jitter.video.tx = max(stats->report.jitter.video.tx, (1000 * data->jitter));
			timestamp = max(timestamp, data->timestamp);
		}
	}

	// 1.1 calcualete rx jitter in ms with taking mean
	stats->report.jitter.audio.rx = safe_normalize(audio_jitter, aj_count);
	stats->report.jitter.video.rx = safe_normalize(video_jitter, vj_count);

	// 1.2 calculate rx jitter buffer delay for audio and video
	stats->report.jitter_buffer_delay.audio = safe_normalize(audio_jitter_buffer_delay, audio_jitter_buffer_emitted_count);
	stats->report.jitter_buffer_delay.video = safe_normalize(video_jitter_buffer_delay, video_jitter_buffer_emitted_count);

	// 2. calculate interval difference into a temp variable
    struct stats_packet_counts diff = {
		.audio.tx = stats->report.packets.audio.tx - stats->last_packets.audio.tx,
		.audio.rx = stats->report.packets.audio.rx - stats->last_packets.audio.rx,
		.video.tx = stats->report.packets.video.tx - stats->last_packets.video.tx,
		.video.rx = stats->report.packets.video.rx - stats->last_packets.video.rx,
		.audio_lost.tx = stats->report.packets.audio_lost.tx - stats->last_packets.audio_lost.tx,
		.audio_lost.rx = stats->report.packets.audio_lost.rx - stats->last_packets.audio_lost.rx,
		.video_lost.tx = stats->report.packets.video_lost.tx - stats->last_packets.video_lost.tx,
		.video_lost.rx = stats->report.packets.video_lost.rx - stats->last_packets.video_lost.rx
	};

	// 2.1 calculate time normized packet stats if time difference make sense
	uint64_t timestamp_in_ms = normalize_timestamp_to_ms(timestamp);
	uint64_t time_difference_in_ms = timestamp_in_ms - stats->last_timestamp_in_ms;

	if (time_difference_in_ms != timestamp) {
		update_per_sec_stats(&stats->report.packets_per_sec, &diff, time_difference_in_ms);
	}

	// 3. save current packet cumulatives into last
	stats->last_packets = stats->report.packets;
	stats->last_timestamp_in_ms = timestamp_in_ms;

	// 4. update report.lost_percentages with calculated percentages
	update_loss_percentages(&stats->report.loss_percentages, &diff);

	return 0;
}

// Helper function to read percieved rtt
static void read_rtt_rx(struct avs_stats *stats, const struct stats_obj *stats_obj)
{
	struct le *le = NULL;

	if (!stats || !stats_obj) {
		return;
	}

	// Calculate mean rtt from remote inbound reports
	double remote_inbound_audio_rtt = 0;
	double remote_inbound_video_rtt = 0;
	int remote_inbound_audio_rtt_count = 0;
	int remote_inbound_video_rtt_count = 0;

	LIST_FOREACH(&stats_obj->remote_inbound_rtp, le) {
		const struct stats_remote_inbound_rtp *data = (struct stats_remote_inbound_rtp *)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			remote_inbound_audio_rtt += data->rtt;
			remote_inbound_audio_rtt_count++;
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			remote_inbound_video_rtt += data->rtt;
			remote_inbound_video_rtt_count++;
		}
	}

	stats->report.rtt.remote_inbound.audio = remote_inbound_audio_rtt ? 1000 * (remote_inbound_audio_rtt / remote_inbound_audio_rtt_count) : 0;
	stats->report.rtt.remote_inbound.video = remote_inbound_video_rtt ? 1000 * (remote_inbound_video_rtt / remote_inbound_video_rtt_count) : 0;
}

static int read_rtt_and_connection(struct avs_stats *stats, const struct stats_obj *stats_obj)
{
	struct le *le = NULL;
	const char *selected_pair_id = NULL;
	const struct stats_candidate_pair *selected = NULL;
	const struct stats_local_candidate *local = NULL, *remote = NULL;

	if (!stats || !stats_obj) {
		return EINVAL;
	}

	// First check if there is a "transport" report that should have selected pair
	LIST_FOREACH(&stats_obj->transport, le) {
		const struct stats_transport *tr = le->data;
		if (!tr->selected_pair_id)
			continue;
		/* A single connection report cannot represent distinct transports. */
		if (selected_pair_id && !streq(selected_pair_id, tr->selected_pair_id))
			goto out;
		selected_pair_id = tr->selected_pair_id;
	}

	/* Prefer the transport reference; Firefox also marks the pair itself. */
	LIST_FOREACH(&stats_obj->candidate_pair, le) {
		const struct stats_candidate_pair *data = (struct stats_candidate_pair *)le->data;

		if (selected_pair_id && data->id &&
		    streq(selected_pair_id, data->id)) {
			selected = data;
			break;
		}
		if (!selected_pair_id && data->selected) {
			if (selected)
				goto out;
			selected = data;
		}
	}

	if (!selected)
		goto out;
	stats->report.ice_pair_selected = true;
	if (selected->has_rtt && isfinite(selected->current_rtt) &&
	    selected->current_rtt >= 0 && selected->current_rtt <= UINT32_MAX / 1000.0) {
		stats->report.rtt.candidate_pair = 1000 * selected->current_rtt;
		stats->report.rtt.candidate_pair_valid = true;
	}
	// use last connected local candidate id to get connection details
	LIST_FOREACH(&stats_obj->local_candidate, le) {
		const struct stats_local_candidate *data = (struct stats_local_candidate *)le->data;

		if (data->id && selected->local_candidate_id && streq(data->id, selected->local_candidate_id)) {
			local = data;
			break;
		}
	}
	LIST_FOREACH(&stats_obj->remote_candidate, le) {
		const struct stats_local_candidate *data = le->data;
		if (data->id && selected->remote_candidate_id && streq(data->id, selected->remote_candidate_id)) {
			remote = data;
			break;
		}
	}
	if (local) {
		stats->report.legacy_local_proto = local->proto;
		stats->report.local_proto = local->cand == STATS_CAND_RELAY && local->relay_tls
			? STATS_PROTO_TLS : local->proto;
		stats->report.local_cand = local->cand;
	}
	if (remote) {
		stats->report.remote_proto = remote->proto;
		stats->report.remote_cand = remote->cand;
	}

 out:
	read_rtt_rx(stats, stats_obj);

	return 0;
}

static int read_audio_level(struct avs_stats *stats, const struct stats_obj *stats_obj)
{
	struct le *le = NULL;

	if (!stats || !stats_obj) {
		return EINVAL;
	}

	LIST_FOREACH(&stats_obj->audio_source, le) {
		const struct stats_audio_source *asp = (struct stats_audio_source *)le->data;
		stats->report.audio_level = (int)(asp->level * 255.0);
	}

	return 0;
}


static void destructor(void *arg)
{
	struct avs_stats *stats = (void *)arg;
	list_flush(&stats->last_bytes);
	list_flush(&stats->last_rx);
	list_flush(&stats->rx_tracks);
	mem_deref(stats->ema);
	mem_deref(stats->lock);

	(void)stats;
}

int stats_alloc(struct avs_stats **statsp, enum icall_conv_type conv_type, void *arg)
{
	struct avs_stats *stats;
	int err = 0;

	if (!statsp)
		return EINVAL;

	stats = mem_zalloc(sizeof(*stats), destructor);
	if (!stats)
		return ENOMEM;

	stats->arg = arg;
	err = lock_alloc(&stats->lock);
	if (err) {
		mem_deref(stats);
		return err;
	}
	stats->conv_type = conv_type;
	err = ema_alloc(&(stats->ema), NULL);
	if (err != 0) {
		mem_deref(stats);
		return err;
	} 

	memset(&stats->report, 0, sizeof(stats->report));
	reset_tx(&stats->report);
	memset(&stats->last_packets, 0, sizeof(stats->last_packets));
	stats->last_timestamp_in_ms = 0;

	*statsp = stats;

	return err;
}


static void inbound_destructor(void *arg)
{
	struct stats_inbound_rtp *data = arg;
	mem_deref(data->id);
	mem_deref(data->track_id);
	mem_deref(data->user_id);
	mem_deref(data->client_id);
}

static struct stats_inbound_rtp *parse_inbound_rtp(struct json_object *jitem)
{
	const char *kind_str = NULL;

	struct stats_inbound_rtp *data;
	data = mem_zalloc(sizeof(*data), inbound_destructor);
	if (!data)
		return NULL;
	jzon_strdup(&data->id, jitem, "id");
	jzon_strdup(&data->track_id, jitem, "trackIdentifier");
	jzon_strdup(&data->user_id, jitem, "avsUserId");
	jzon_strdup(&data->client_id, jitem, "avsClientId");
	data->has_bytes = !jzon_u64(&data->bytes_received, jitem, "bytesReceived");
	data->fps = -1;
	jzon_double(&data->fps, jitem, "framesPerSecond");
	jzon_int(&data->width, jitem, "frameWidth");
	jzon_int(&data->height, jitem, "frameHeight");

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	data->has_packets = !jzon_int(&data->packets_received, jitem, "packetsReceived");
	data->has_lost = !jzon_int(&data->packets_lost, jitem, "packetsLost");
	data->has_jitter = !jzon_double(&data->jitter, jitem, "jitter");
	jzon_double(&data->jitter_buffer_delay, jitem, "jitterBufferDelay");
	jzon_int(&data->jitter_buffer_emitted_count, jitem, "jitterBufferEmittedCount");
	jzon_double(&data->timestamp, jitem, "timestamp");

	return data;
}

static void outbound_destructor(void *arg)
{
	struct stats_outbound_rtp *data = arg;
	mem_deref(data->id);
}

static void remote_inbound_destructor(void *arg)
{
	struct stats_remote_inbound_rtp *data = arg;
	mem_deref(data->local_id);
}

static struct stats_outbound_rtp *parse_outbound_rtp(struct json_object *jitem)
{
	const char *kind_str = NULL;
	const char *quality_limitation_str = NULL;

	struct stats_outbound_rtp* data;
	data = mem_zalloc(sizeof(*data), outbound_destructor);
	if (!data)
		return NULL;
	const char *id = jzon_str(jitem, "id");
	if (id && str_dup(&data->id, id))
		return mem_deref(data);
	data->fps = -1;
	data->has_bytes_sent = !jzon_u64(&data->bytes_sent, jitem, "bytesSent");
	jzon_double(&data->fps, jitem, "framesPerSecond");
	jzon_int(&data->width, jitem, "frameWidth");
	jzon_int(&data->height, jitem, "frameHeight");

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	quality_limitation_str = jzon_str(jitem, "qualityLimitationReason");
	data->quality_limitation = stats_parse_quality_limitation(quality_limitation_str);

	jzon_int(&data->packets_sent, jitem, "packetsSent");
	jzon_double(&data->timestamp, jitem, "timestamp");

	return data;
}

static struct stats_remote_inbound_rtp *parse_remote_inbound_rtp(struct json_object *jitem)
{
	const char *kind_str = NULL;

	struct stats_remote_inbound_rtp *data;
	data = mem_zalloc(sizeof(*data), remote_inbound_destructor);
	if (!data)
		return NULL;
	const char *local_id = jzon_str(jitem, "localId");
	if (local_id && str_dup(&data->local_id, local_id))
		return mem_deref(data);
	data->fraction_lost = -1;
	jzon_double(&data->fraction_lost, jitem, "fractionLost");

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	jzon_int(&data->packets_lost, jitem, "packetsLost");
	data->has_jitter = !jzon_double(&data->jitter, jitem, "jitter");
	jzon_double(&data->timestamp, jitem, "timestamp");
	jzon_double(&data->rtt, jitem, "roundTripTime");

	return data;
}

static struct stats_audio_source *parse_audio_source(struct json_object *jitem)
{
	struct stats_audio_source *data;
	data = mem_zalloc(sizeof(*data), NULL);
	memset(data, 0, sizeof(*data));

	jzon_double(&data->level, jitem, "audioLevel");

	return data;
}

static struct stats_candidate_pair *parse_candidate_pair(struct json_object *jitem)
{
	const char *state_str = NULL;
	const char *id_str = NULL;
	const char *local_candidate_id_str = NULL;

	struct stats_candidate_pair *data;
	data = mem_zalloc(sizeof(*data), candidate_pair_destructor);
	memset(data, 0, sizeof(*data));

	state_str = jzon_str(jitem, "state");
	data->state = stats_parse_state(state_str);
	jzon_bool(&data->nominated, jitem, "nominated");
	jzon_bool(&data->selected, jitem, "selected");
	data->has_rtt = !jzon_double(&data->current_rtt, jitem, "currentRoundTripTime");

	id_str = jzon_str(jitem, "id");
	if (id_str) {
		str_dup(&data->id, id_str);
	}

	local_candidate_id_str = jzon_str(jitem, "localCandidateId");
	if (local_candidate_id_str) {
		str_dup(&data->local_candidate_id, local_candidate_id_str);
	}
	const char *remote_id = jzon_str(jitem, "remoteCandidateId");
	if (remote_id)
		str_dup(&data->remote_candidate_id, remote_id);

	return data;
}

static struct stats_local_candidate *parse_local_candidate(struct json_object *jitem)
{
	const char *id_str = NULL;
	const char *proto_str = NULL;
	const char *cand_str = NULL;

	struct stats_local_candidate *data;
	data = mem_zalloc(sizeof(*data),local_candidate_destructor);
	if (!data)
		return NULL;
	const char *relay = jzon_str(jitem, "relayProtocol");
	data->relay_tls = relay && streq(relay, "tls");

	proto_str = jzon_str(jitem, "protocol");
	data->proto = stats_parse_poto(proto_str);
	cand_str = jzon_str(jitem, "candidateType");
	data->cand = stats_parse_cand(cand_str);

	id_str = jzon_str(jitem, "id");
	if (id_str) {
		str_dup(&data->id, id_str);
	}

	return data;
}

static struct stats_transport *parse_transport(struct json_object *jitem)
{
	const char *id_str = NULL;

	struct stats_transport *data;
	data = mem_zalloc(sizeof(*data),transport_destructor);

	id_str = jzon_str(jitem, "selectedCandidatePairId");
	if (id_str) {
		str_dup(&data->selected_pair_id, id_str);
	}

	return data;
}

static int parse_json(const char *report, struct stats_obj *stats_obj) {
	const char *type_str = NULL;
	const char *kind_str = NULL;
	int err = 0;

	if (!report || !stats_obj) {
		return EINVAL;
	}

	struct json_object *jobj;
	err = jzon_decode(&jobj, report, strlen(report));
	if (err) {
		return EPROTO;
	}

	//jzon_dump(jobj);

	// we expect json array as root
	if (!jzon_is_array(jobj)) {
		err = EINVAL;
		goto out;
	}

	int items = json_object_array_length(jobj);
	for (int i = 0; i < items; ++i) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jobj, i);
		if (!jitem) {
			// probably ok to skip unread items
			continue;
		}

		// jzon_dump(jitem);

		type_str = jzon_str(jitem, "type");
		enum stats_type type = stats_parse_type(type_str);

		switch (type) {
			case STATS_TYPE_INBOUND_RTP: {
				struct stats_inbound_rtp *irp = NULL;
				irp = parse_inbound_rtp(jitem);
				if (irp) {
					list_append(&stats_obj->inbound_rtp, &irp->le, irp);
				}
			}
				break;

			case STATS_TYPE_OUTBOUND_RTP: {
				struct stats_outbound_rtp *orp = NULL;
				orp = parse_outbound_rtp(jitem);
				if (orp) {
					list_append(&stats_obj->outbound_rtp, &orp->le, orp);
				}
			}
				break;

			case STATS_TYPE_REMOTE_INBOUND_RTP: {
				struct stats_remote_inbound_rtp *rirp = NULL;
				rirp = parse_remote_inbound_rtp(jitem);
				if (rirp) {
					list_append(&stats_obj->remote_inbound_rtp, &rirp->le, rirp);
				}
			}
				break;

			case STATS_TYPE_MEDIA_SOURCE: {
				kind_str = NULL;
				kind_str = jzon_str(jitem, "kind");
				enum stats_kind kind = stats_parse_kind(kind_str);

				if (kind == STATS_KIND_AUDIO) {
					struct stats_audio_source *asp = NULL;
					asp = parse_audio_source(jitem);
					if (asp) {
						list_append(&stats_obj->audio_source, &asp->le, asp);
					}
				}
			}
				break;

			case STATS_TYPE_CANDIDATE_PAIR: {
				struct stats_candidate_pair *cp = NULL;
				cp = parse_candidate_pair(jitem);
				if (cp) {
					list_append(&stats_obj->candidate_pair, &cp->le, cp);
				}
			}
				break;

			case STATS_TYPE_LOCAL_CANDIDATE:
			case STATS_TYPE_REMOTE_CANDIDATE: {
				struct stats_local_candidate *lc = NULL;
				lc = parse_local_candidate(jitem);
				if (lc) {
					list_append(type == STATS_TYPE_LOCAL_CANDIDATE ?
						    &stats_obj->local_candidate : &stats_obj->remote_candidate,
						    &lc->le, lc);
				}
			}
				break;

			case STATS_TYPE_TRANSPORT: {
				struct stats_transport *tr = NULL;
				tr = parse_transport(jitem);
				if (tr) {
					list_append(&stats_obj->transport, &tr->le, tr);
				}
			}
				break;

			default:
				break;
		}

		mem_deref(jitem);
	}

out:
	mem_deref(jobj);

	return err;
}

// Quality level thresholds
const int RTT_LOW = 50;
const int RTT_HIGH = 150;
const int JITTER_LOW = 10;
const int JITTER_HIGH = 50;
const int PACKET_LOSS_LOW = 5;
const int PACKET_LOSS_HIGH = 10;

// Stream direction weights
const float UPSTREAM_WEIGHT = 0.7;
const float DOWNSTREM_WEIGHT = (1.0 - UPSTREAM_WEIGHT);

// Overall quality weights
const float JITTER_WEIGHT = 0.35;
const float PACKET_LOSS_WEIGHT = 0.35;
const float RTT_WEIGHT = (1.0 - JITTER_WEIGHT - PACKET_LOSS_WEIGHT);

/* quality info */
const int STATS_QUALITY_NORMAL = 1;
const int STATS_QUALITY_MEDIUM = 2;
const int STATS_QUALITY_POOR = 3;

static int normalize_to_levels(int num, int low_threshold, int high_threshold) {
	if (num < low_threshold) {
		return STATS_QUALITY_NORMAL;
	}
	else if (num > high_threshold) {
		return STATS_QUALITY_POOR;
	}
	else {
		return STATS_QUALITY_MEDIUM;
	}
}

static int average_if_exists(int audio, int video) {
	if (audio && video) {
		return (audio + video) / 2;
	}
	// means we have at most one of them significant
	return audio + video;
}

static float normalize_quality(const struct avs_stats *stats) {
	// Stats are 3 step normalized wrt corresponding thresholds
	const int rtt_candidate_pair = normalize_to_levels(stats->report.rtt.candidate_pair, RTT_LOW, RTT_HIGH);
	const int rtt_remote_inbound = normalize_to_levels(average_if_exists(stats->report.rtt.remote_inbound.audio, stats->report.rtt.remote_inbound.video), RTT_LOW, RTT_HIGH);
	const int jitter_tx = normalize_to_levels(average_if_exists(stats->report.jitter.audio.tx, stats->report.jitter.video.tx), JITTER_LOW, JITTER_HIGH);
	const int jitter_rx = normalize_to_levels(average_if_exists(stats->report.jitter.audio.rx, stats->report.jitter.video.rx), JITTER_LOW, JITTER_HIGH);
	const int packet_loss_tx = normalize_to_levels(stats->report.loss_percentages.direction.tx, PACKET_LOSS_LOW, PACKET_LOSS_HIGH);
	const int packet_loss_rx = normalize_to_levels(stats->report.loss_percentages.direction.rx, PACKET_LOSS_LOW, PACKET_LOSS_HIGH);

	float rtt = 0;
	float jitter = 0;
	float packet_loss = 0;
	bool has_rtt = stats->report.rtt.candidate_pair_valid;

	if ( stats->conv_type == ICALL_CONV_TYPE_CONFERENCE || stats->conv_type == ICALL_CONV_TYPE_CONFERENCE_MLS) {
		// provide slightly higher importance to upstream stats fon conference
		bool has_remote_rtt = stats->report.rtt.remote_inbound.audio > 0 ||
			stats->report.rtt.remote_inbound.video > 0;
		if (has_rtt && has_remote_rtt)
			rtt = UPSTREAM_WEIGHT * rtt_remote_inbound + DOWNSTREM_WEIGHT * rtt_candidate_pair;
		else if (has_remote_rtt)
			rtt = rtt_remote_inbound;
		else if (has_rtt)
			rtt = rtt_candidate_pair;
		has_rtt = has_rtt || has_remote_rtt;
		jitter = UPSTREAM_WEIGHT * jitter_tx + DOWNSTREM_WEIGHT * jitter_rx;
		packet_loss = UPSTREAM_WEIGHT * packet_loss_tx + DOWNSTREM_WEIGHT * packet_loss_rx;
	}
	else {
		// for 1-1 calls favor downstream information to isolate stats from peer connection
		rtt = rtt_candidate_pair;
		jitter = jitter_rx;
		packet_loss = packet_loss_rx;
	}

	// provide packet loss and jitter a bit more importance than latency
	if (!has_rtt)
		return (JITTER_WEIGHT * jitter + PACKET_LOSS_WEIGHT * packet_loss) /
			(JITTER_WEIGHT + PACKET_LOSS_WEIGHT);
	return JITTER_WEIGHT * jitter + PACKET_LOSS_WEIGHT * packet_loss + RTT_WEIGHT * rtt;
}

static void rx_track_destructor(void *arg)
{
	struct stats_rx_track *track = arg;
	mem_deref(track->id);
	mem_deref(track->track_id);
	mem_deref(track->user_id);
	mem_deref(track->client_id);
}

static bool same_source(const char *a, const char *b)
{
	return (!a && !b) || (a && b && streq(a, b));
}

static int read_rx_media(struct avs_stats *stats, struct stats_obj *obj)
{
	struct list next = LIST_INIT;
	struct le *le, *prev_le;
	int err = 0;
	list_flush(&stats->rx_tracks);
	LIST_FOREACH(&obj->inbound_rtp, le) {
		const struct stats_inbound_rtp *current = le->data;
		const struct stats_last_bytes *previous = NULL;
		struct stats_rx_track *track;
		struct stats_last_bytes *last;
		if (!current->id || (current->kind != STATS_KIND_AUDIO && current->kind != STATS_KIND_VIDEO))
			continue;
		track = mem_zalloc(sizeof(*track), rx_track_destructor);
		last = mem_zalloc(sizeof(*last), last_bytes_destructor);
		if (!track || !last) {
			mem_deref(track);
			mem_deref(last);
			err = ENOMEM;
			goto out;
		}
		list_append(&stats->rx_tracks, &track->le, track);
		list_append(&next, &last->le, last);
		err = str_dup(&track->id, current->id);
		err |= str_dup(&last->id, current->id);
		if (current->track_id)
			err |= str_dup(&track->track_id, current->track_id);
		if (current->user_id) {
			err |= str_dup(&track->user_id, current->user_id);
			err |= str_dup(&last->user_id, current->user_id);
		}
		if (current->client_id) {
			err |= str_dup(&track->client_id, current->client_id);
			err |= str_dup(&last->client_id, current->client_id);
		}
		if (err)
			goto out;
		track->video = current->kind == STATS_KIND_VIDEO;
		track->media.bitrate = track->media.loss = -1;
		track->media.jitter = current->has_jitter ? current->jitter * 1000 : -1;
		track->media.width = current->width;
		track->media.height = current->height;
		track->media.fps = current->fps;
		LIST_FOREACH(&stats->last_rx, prev_le) {
			const struct stats_last_bytes *item = prev_le->data;
			if (streq(item->id, current->id) &&
			    same_source(item->user_id, current->user_id) &&
			    same_source(item->client_id, current->client_id)) {
				previous = item;
				break;
			}
		}
		if (previous && current->timestamp > previous->timestamp) {
			uint64_t elapsed = normalize_timestamp_to_ms(current->timestamp) -
				normalize_timestamp_to_ms(previous->timestamp);
			if (elapsed && previous->has_bytes && current->has_bytes &&
			    current->bytes_received >= previous->bytes_sent)
				track->media.bitrate = (current->bytes_received - previous->bytes_sent) * 8000.0 / elapsed;
			if (previous->has_lost && current->has_packets && current->has_lost) {
				double received = (double)current->packets_received - previous->packets_received;
				double lost = (double)current->packets_lost - previous->packets_lost;
				if (received >= 0 && lost >= 0 && received + lost > 0)
					track->media.loss = lost * 100.0 / (received + lost);
			}
		}
		last->bytes_sent = current->bytes_received;
		last->has_bytes = current->has_bytes;
		last->has_lost = current->has_packets && current->has_lost;
		last->packets_received = current->packets_received;
		last->packets_lost = current->packets_lost;
		last->timestamp = current->timestamp;
	}
 out:
	list_flush(&stats->last_rx);
	while (list_head(&next)) {
		struct le *entry = list_head(&next);
		list_unlink(entry);
		list_append(&stats->last_rx, entry, entry->data);
	}
	stats->report.rx_tracks = &stats->rx_tracks;
	return err;
}

static void read_tx_media(struct avs_stats *stats, struct stats_obj *obj,
			 enum stats_kind kind, struct stats_tx_media *result)
{
	struct le *le, *prev_le, *feedback_le;
	bool found = false, bitrate_valid = true;
	double bitrate = 0;
	result->bitrate = result->jitter = result->loss = result->fps = -1;
	result->width = result->height = 0;
	LIST_FOREACH(&obj->outbound_rtp, le) {
		const struct stats_outbound_rtp *current = le->data;
		const struct stats_last_bytes *previous = NULL;
		if (current->kind != kind)
			continue;
		found = true;
		LIST_FOREACH(&stats->last_bytes, prev_le) {
			const struct stats_last_bytes *item = prev_le->data;
			if (item->id && current->id && streq(item->id, current->id)) {
				previous = item;
				break;
			}
		}
		if (previous && current->has_bytes_sent &&
		    current->bytes_sent >= previous->bytes_sent &&
		    normalize_timestamp_to_ms(current->timestamp) >
		    normalize_timestamp_to_ms(previous->timestamp)) {
			bitrate += (current->bytes_sent - previous->bytes_sent) * 8000.0 /
				(normalize_timestamp_to_ms(current->timestamp) -
				 normalize_timestamp_to_ms(previous->timestamp));
		}
		else
			bitrate_valid = false;

		/* For simulcast show the largest reported video layer. */
		if (current->width > result->width ||
		    (current->width == result->width && current->height > result->height)) {
			result->width = current->width;
			result->height = current->height;
			result->fps = current->fps;
		}
		LIST_FOREACH(&obj->remote_inbound_rtp, feedback_le) {
			const struct stats_remote_inbound_rtp *feedback = feedback_le->data;
			if (!current->id || !feedback->local_id ||
			    !streq(current->id, feedback->local_id))
				continue;
			if (feedback->has_jitter && feedback->jitter >= 0)
				result->jitter = max(result->jitter, feedback->jitter * 1000);
			if (feedback->fraction_lost >= 0 && feedback->fraction_lost <= 1)
				result->loss = max(result->loss, feedback->fraction_lost * 100);
		}
	}
	if (found && bitrate_valid)
		result->bitrate = bitrate;
}

static int update_report(struct avs_stats *stats, const char *report_json)
{
	int err = 0;
	
	if (!stats || !stats->ema || !report_json)
		return EINVAL;

	memset(&stats->report, 0, sizeof(stats->report));
	reset_tx(&stats->report);

	struct stats_obj stats_obj = {.audio_source = LIST_INIT,
							.inbound_rtp = LIST_INIT,
							.outbound_rtp = LIST_INIT,
							.remote_inbound_rtp = LIST_INIT,
							.candidate_pair = LIST_INIT,
							.local_candidate = LIST_INIT,
							.remote_candidate = LIST_INIT,
							.transport = LIST_INIT,
						};

	err = parse_json(report_json, &stats_obj);
	if (err) {
		warning("stats_update: parse_json failed: %m\n", err);
		goto out;
	}

	err = read_packet_stats_and_jitter(stats, &stats_obj);
	if (err) {
		warning("stats_update: read_packet_stats failed: %m\n", err);
		goto out;
	}
	err = read_rtt_and_connection(stats, &stats_obj);
	if (err) {
		warning("stats_update: read_rtt failed: %m\n", err);
		goto out;
	}
	err = read_audio_level(stats, &stats_obj);
	if (err) {
		warning("stats_update: read_audio_level failed: %m\n", err);
		goto out;
	}
	read_tx_media(stats, &stats_obj, STATS_KIND_AUDIO, &stats->report.tx_audio);
	err = read_rx_media(stats, &stats_obj);
	if (err)
		goto out;
	read_tx_media(stats, &stats_obj, STATS_KIND_VIDEO, &stats->report.tx_video);
	list_flush(&stats->last_bytes);
	struct le *le;
	LIST_FOREACH(&stats_obj.outbound_rtp, le) {
		const struct stats_outbound_rtp *current = le->data;
		struct stats_last_bytes *last;
		if (!current->id || !current->has_bytes_sent)
			continue;
		last = mem_zalloc(sizeof(*last), last_bytes_destructor);
		if (!last) {
			err = ENOMEM;
			goto out;
		}
		err = str_dup(&last->id, current->id);
		if (err) {
			mem_deref(last);
			goto out;
		}
		last->bytes_sent = current->bytes_sent;
		last->timestamp = current->timestamp;
		list_append(&stats->last_bytes, &last->le, last);
	}

	const float quality = normalize_quality(stats);
	err = ema_update(stats->ema, quality);
	if (err) {
		warning("stats_update: ema_update failed: %m\n", err);
		goto out;
	}

 out:
	list_flush(&stats_obj.audio_source);
	list_flush(&stats_obj.inbound_rtp);
	list_flush(&stats_obj.outbound_rtp);
	list_flush(&stats_obj.remote_inbound_rtp);
	list_flush(&stats_obj.candidate_pair);
	list_flush(&stats_obj.local_candidate);
	list_flush(&stats_obj.remote_candidate);
	list_flush(&stats_obj.transport);

	if (err) {
		warning("stats_update: parsing json failed: %s\n",
			report_json);
	}
	
	return err;
}

int stats_update(struct avs_stats *stats, const char *report_json)
{
	int err;
	if (!stats || !report_json)
		return EINVAL;
	lock_write_get(stats->lock);
	err = update_report(stats, report_json);
	lock_rel(stats->lock);
	return err;
}

static int copy_report(struct avs_stats *stats, struct stats_report *report)
{
	int err = 0;

	if (!stats || !report)
		return EINVAL;

	// Process raw stats to generate ema and mos estimate enhancements
	err = ema_get_val(stats->ema, &stats->report.quality_index);
	stats->report.mos_estimate = stats->report.rtt.candidate_pair_valid ?
		g107_2_estimate(stats->report.rtt.candidate_pair,
		stats->report.loss_percentages.channel.audio, 
		stats->report.jitter_buffer_delay.audio) : -1;

	*report = stats->report;
	report->rx_tracks = NULL;

	return err;
}

static void snapshot_destructor(void *arg)
{
	list_flush(arg);
}

void stats_report_release(struct stats_report *report)
{
	if (report) {
		mem_deref((void *)report->rx_tracks);
		report->rx_tracks = NULL;
	}
}

int stats_get_report(struct avs_stats *stats, struct stats_report *report)
{
	int err;
	if (!stats || !report)
		return EINVAL;
	lock_write_get(stats->lock);
	err = copy_report(stats, report);
	lock_rel(stats->lock);
	return err;
}

int stats_get_report_snapshot(struct avs_stats *stats, struct stats_report *report)
{
	struct list *tracks;
	struct le *le;
	int err;
	if (!stats || !report)
		return EINVAL;
	memset(report, 0, sizeof(*report));
	tracks = mem_zalloc(sizeof(*tracks), snapshot_destructor);
	if (!tracks)
		return ENOMEM;
	lock_write_get(stats->lock);
	err = copy_report(stats, report);
	if (err)
		goto out;
	LIST_FOREACH(&stats->rx_tracks, le) {
		const struct stats_rx_track *source = le->data;
		struct stats_rx_track *track = mem_zalloc(sizeof(*track), rx_track_destructor);
		if (!track) {
			err = ENOMEM;
			goto out;
		}
		list_append(tracks, &track->le, track);
		track->video = source->video;
		track->media = source->media;
		err = str_dup(&track->id, source->id);
		if (source->track_id)
			err |= str_dup(&track->track_id, source->track_id);
		if (source->user_id)
			err |= str_dup(&track->user_id, source->user_id);
		if (source->client_id)
			err |= str_dup(&track->client_id, source->client_id);
		if (err)
			goto out;
	}
 out:
	lock_rel(stats->lock);
	if (err)
		mem_deref(tracks);
	else
		report->rx_tracks = tracks;
	return err;
}

char *stats_proto_name(enum stats_proto proto)
{
	switch(proto) {
	case STATS_PROTO_UNKNOWN:
		return "Unknown";
		
	case STATS_PROTO_UDP:
		return "UDP";

	case STATS_PROTO_TCP:
		return "TCP";

	case STATS_PROTO_TLS:
		return "TLS";

	default:
		return "???";
	}
}

char *stats_cand_name(enum stats_cand cand)
{
	switch (cand) {
	case STATS_CAND_UNKNOWN:
		return "Unknown";
		
	case STATS_CAND_HOST:
		return "Host";

	case STATS_CAND_SRFLX:
		return "Srflx";
		
	case STATS_CAND_PRFLX:
		return "Prflx";
		
	case STATS_CAND_RELAY:
		return "Relay";

	default:
		return "???";
	}
}

char *stats_quality_limitation_name(enum stats_quality_limitation quality_limitation)
{
	switch (quality_limitation) {
	case STATS_QUALITY_LIMITATION_NONE:
		return "none";
		
	case STATS_QUALITY_LIMITATION_BANDWIDTH:
		return "bandwidth";

	case STATS_QUALITY_LIMITATION_CPU:
		return "cpu";
		
	case STATS_QUALITY_LIMITATION_OTHER:
		return "other";

	default:
		return "no information";
	}
}
