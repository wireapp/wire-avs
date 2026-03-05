
#include <re.h>
#include "avs.h"

#include "stats.h"

enum stats_type parse_type(const char *type) {
	if (streq(type, "inbound-rtp")) {
		return STATS_TYPE_INBOUND_RTP;
	}
	else if (streq(type, "outbound-rtp")) {
		return STATS_TYPE_OUTBOUND_RTP;
	}
	else if (streq(type, "remote-inbound-rtp")) {
		return STATS_TYPE_REMOTE_INBOUND_RTP;
	}
	else { 
		return STATS_TYPE_UNKNOWN;
	}
}

enum stats_kind parse_kind(const char *kind) {
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

struct avs_stats {
	struct stats_report report;

	void *arg;
};

static int read_packet_stats(struct avs_stats *stats, const char *report)
{
	const char* type_str = NULL;
	const char* kind_str = NULL;

	struct json_object *jobj;
	int err = jzon_decode(&jobj, report, strlen(report));
	if (err) {
		warning("unable to decode quality info");
		return EINVAL;
	}

	// jzon_dump(jobj);

	// we expect json array as root
	if (!jzon_is_array(jobj)) {
		mem_deref(jobj);
		return EINVAL;
	}

	int items = json_object_array_length(jobj);
	for (int i = 0; i < items; ++i) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jobj, i);
		if (!jitem) {
			// probably ok to skip unread items
			warning("unable to decode %d item %s", i, report);
			continue;
		}

		// jzon_dump(jitem);

		type_str = jzon_str(jitem, "type");
		enum stats_type type = parse_type(type_str);

		if (!(type == STATS_TYPE_INBOUND_RTP || 
				type == STATS_TYPE_OUTBOUND_RTP || 
				type ==STATS_TYPE_REMOTE_INBOUND_RTP)) {
			// not interested with other types here
			continue;
		}

		kind_str = jzon_str(jitem, "kind");
		enum stats_kind kind = parse_kind(kind_str);

		if (!(kind == STATS_KIND_AUDIO || kind == STATS_KIND_VIDEO)) {
			// not interested with other kinds here
			continue;
		}

		if (type == STATS_TYPE_INBOUND_RTP) {
			int packets_received = 0;
			jzon_int(&packets_received, jitem, "packetsReceived");
			int packets_lost = 0;
			jzon_int(&packets_lost, jitem, "packetsLost");

			if (kind == STATS_KIND_AUDIO) {
				stats->report.packets.audio_rx += packets_received;
			}
			else if (kind == STATS_KIND_VIDEO) {
				stats->report.packets.video_rx += packets_received;
			}

			stats->report.packets.lost_rx += packets_lost;
		}
		else if (type == STATS_TYPE_OUTBOUND_RTP) {
			int packets_sent = 0;
			jzon_int(&packets_sent, jitem, "packetsSent");

			if (kind == STATS_KIND_AUDIO) {
				stats->report.packets.audio_tx += packets_sent;
			}
			else if (kind == STATS_KIND_VIDEO) {
				stats->report.packets.video_tx += packets_sent;
			}
		}
		else if (type == STATS_TYPE_REMOTE_INBOUND_RTP) {
			int packets_lost = 0;
			jzon_int(&packets_lost, jitem, "packetsLost");

			stats->report.packets.lost_tx += packets_lost;
		}

		mem_deref(jitem);
	}

	mem_deref(jobj);
	return 0;
}

static int read_rtt(struct avs_stats *stats, const char *report)
{
	// Parse report for RTT
	return 0;
}

static int read_audio_level(struct avs_stats *stats, const char *report)
{
	// Parse report for audio-level
	return 0;
}

static int read_connection(struct avs_stats *stats, const char *report)
{
	// Parse report for connection info
	return 0;
}

static int read_jitter(struct avs_stats *stats, const char *report)
{
	// Parse report for jitter
	return 0;
}


static void destructor(void *arg)
{
	struct avs_stats *stats = (void *)arg;

	(void)stats;
}

int stats_alloc(struct avs_stats **statsp, void *arg)
{
	struct avs_stats *stats;
	int err = 0;

	stats = mem_zalloc(sizeof(*stats), destructor);
	if (!stats)
		return ENOMEM;

	stats->arg = arg;

	*statsp = stats;

	return err;
}

int stats_update(struct avs_stats *stats, const char *report_json)
{
	int err = 0;
	
	if (!stats || !report_json)
		return EINVAL;

	memset(&stats->report, 0, sizeof(stats->report));

	err |= read_packet_stats(stats, report_json);
	err |= read_rtt(stats, report_json);
	err |= read_audio_level(stats, report_json);
	err |= read_connection(stats, report_json);
	err |= read_jitter(stats, report_json);

	return err;
}

int stats_get_report(struct avs_stats *stats, struct stats_report *report)
{
	if (!stats || !report)
		return EINVAL;

	*report = stats->report;

	return 0;
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
