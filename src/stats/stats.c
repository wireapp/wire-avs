
#include <re.h>
#include "avs.h"

#include <math.h>

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

enum stats_type {
	STATS_TYPE_UNKNOWN,
	STATS_TYPE_INBOUND_RTP,
	STATS_TYPE_OUTBOUND_RTP,
	STATS_TYPE_REMOTE_INBOUND_RTP,
	STATS_TYPE_MEDIA_SOURCE,
	STATS_TYPE_CANDIDATE_PAIR,
	STATS_TYPE_LOCAL_CANDIDATE,
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
	enum stats_kind kind;
	int packets_received;
	int packets_lost;
	double jitter;
	struct le le;
};

struct stats_outbound_rtp {
	enum stats_kind kind;
	int packets_sent;
	struct le le;
};

struct stats_remote_inbound_rtp {
	enum stats_kind kind;
	int packets_lost;
	double jitter;
	struct le le;
};

struct stats_audio_source {
	double level;
	struct le le;
};

struct stats_candidate_pair {
	enum stats_state state;
	bool nominated;
	double current_rtt;
	char *id;
	char *local_candidate_id;
	struct le le;
};

static void candidate_pair_destructor(void *arg)
{
	struct stats_candidate_pair *item = arg;
	mem_deref(item->id);
	mem_deref(item->local_candidate_id);
}

struct stats_local_candidate {
	char *id;
	enum stats_proto proto;
	enum stats_cand cand;
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
	struct list transport;
};

struct avs_stats {
	struct stats_report report;
	struct stats_packet_counts last_packets;

	void *arg;
};

static uint32_t calculate_loss_percentage(uint32_t packets, uint32_t lost) {
	if (packets)
		return (lost / (float)packets) * 100;
	else
		return 0;
}

static int read_packet_stats_and_jitter(struct avs_stats *stats, struct stats_obj* stats_obj)
{
	struct le *le = NULL;
	double audio_jitter = 0;
	int aj_count = 0;
	double video_jitter = 0;
	int vj_count = 0;

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
		struct stats_inbound_rtp* data = (struct stats_inbound_rtp*)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.packets.audio.rx += data->packets_received;
			if (data->packets_received) {
				audio_jitter += data->jitter;
				aj_count ++;
			}
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.packets.video.rx += data->packets_received;
			if (data->packets_received) {
				video_jitter += data->jitter;
				vj_count ++;
			}
		}

		stats->report.packets.lost.rx += data->packets_lost;
	}

	LIST_FOREACH(&stats_obj->outbound_rtp, le) {
		struct stats_outbound_rtp* data = (struct stats_outbound_rtp*)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.packets.audio.tx += data->packets_sent;
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.packets.video.tx += data->packets_sent;
		}
	}

	LIST_FOREACH(&stats_obj->remote_inbound_rtp, le) {
		struct stats_remote_inbound_rtp* data = (struct stats_remote_inbound_rtp*)le->data;

		if (data->kind == STATS_KIND_AUDIO) {
			stats->report.jitter.audio.tx = max(stats->report.jitter.audio.tx, (1000 * data->jitter));
		}
		else if (data->kind == STATS_KIND_VIDEO) {
			stats->report.jitter.video.tx = max(stats->report.jitter.video.tx, (1000 * data->jitter));
		}

		stats->report.packets.lost.tx += data->packets_lost;
	}

	// 1.1 calcualete rx jitter in ms with taking mean
	stats->report.jitter.audio.rx = aj_count ? 1000 * (audio_jitter / aj_count) : 0;
	stats->report.jitter.video.rx = vj_count ? 1000 * (video_jitter / vj_count) : 0;

	// 2. calculate interval percentage for packet loss into tmp variables
	uint32_t loss_tx = calculate_loss_percentage(
		(stats->report.packets.audio.tx - stats->last_packets.audio.tx +
		stats->report.packets.video.tx - stats->last_packets.video.tx),
		stats->report.packets.lost.tx - stats->last_packets.lost.tx);

	uint32_t loss_rx = calculate_loss_percentage(
		(stats->report.packets.audio.rx - stats->last_packets.audio.rx +
		stats->report.packets.video.rx - stats->last_packets.video.rx),
		stats->report.packets.lost.rx - stats->last_packets.lost.rx);

	// 3. save current packet cumulatives into last
	stats->last_packets = stats->report.packets;

	// 4. update report.packet.lost with calculated percentages
	stats->report.packets.lost.tx = loss_tx;
	stats->report.packets.lost.rx = loss_rx;

	return 0;
}

static int read_rtt_and_connection(struct avs_stats *stats, struct stats_obj* stats_obj)
{
	struct le *le = NULL;
	const char* connected_local_candidate_id = NULL;
	const char* selected_pair_id = NULL;

	if (!stats || !stats_obj) {
		return EINVAL;
	}

	// First check if there is a "transport" report that should have selected pair
	le = list_head(&stats_obj->transport);
	if (le) {
		struct stats_transport *head = (struct stats_transport*)le->data;
		selected_pair_id = head->selected_pair_id;
	}

	// When we have a "transport" report search selected pair, else
	// search a connected pair (which is succeeded and nominated)
	LIST_FOREACH(&stats_obj->candidate_pair, le) {
		struct stats_candidate_pair* data = (struct stats_candidate_pair*)le->data;

		if (selected_pair_id) {
			if (data->id && streq(selected_pair_id, data->id)) {
				stats->report.rtt = max(stats->report.rtt, (1000 * data->current_rtt));
				connected_local_candidate_id = data->local_candidate_id;
				break;
			}
		}
		else {
			// we will try to find connected pair without "transport" info
			if (data->nominated && (data->state == STATS_STATE_SUCCEEDED)) {
				stats->report.rtt = max(stats->report.rtt, (1000 * data->current_rtt));
				if (data->local_candidate_id) {
					connected_local_candidate_id = data->local_candidate_id;
				}
			}
		}
	}



	if (!connected_local_candidate_id) {
		// maybe ok that we dont have connection atm
		return 0;
	}

	// use last connected local candidate id to get connection details
	LIST_FOREACH(&stats_obj->local_candidate, le) {
		struct stats_local_candidate* data = (struct stats_local_candidate*)le->data;

		if (data->id && streq(data->id, connected_local_candidate_id)) {
			stats->report.proto = data->proto;
			stats->report.cand = data->cand;
			break;
		}
	}

	return 0;
}

static int read_audio_level(struct avs_stats *stats, struct stats_obj* stats_obj)
{
	struct le *le = NULL;

	if (!stats || !stats_obj) {
		return EINVAL;
	}

	LIST_FOREACH(&stats_obj->audio_source, le) {
		struct stats_audio_source* asp = (struct stats_audio_source*)le->data;
		stats->report.audio_level = (int)(asp->level * 255.0);
	}

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

	memset(&stats->report, 0, sizeof(stats->report));
	memset(&stats->last_packets, 0, sizeof(stats->last_packets));

	*statsp = stats;

	return err;
}


static struct stats_inbound_rtp* parse_inbound_rtp(struct json_object *jitem)
{
	const char* kind_str = NULL;

	struct stats_inbound_rtp* data;
	data = mem_zalloc(sizeof(*data), NULL);
	memset(data, 0, sizeof(*data));

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	jzon_int(&data->packets_received, jitem, "packetsReceived");
	jzon_int(&data->packets_lost, jitem, "packetsLost");
	jzon_double(&data->jitter, jitem, "jitter");

	return data;
}

static struct stats_outbound_rtp* parse_outbound_rtp(struct json_object *jitem)
{
	const char* kind_str = NULL;

	struct stats_outbound_rtp* data;
	data = mem_zalloc(sizeof(*data), NULL);
	memset(data, 0, sizeof(*data));

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	jzon_int(&data->packets_sent, jitem, "packetsSent");

	return data;
}

static struct stats_remote_inbound_rtp* parse_remote_inbound_rtp(struct json_object *jitem)
{
	const char* kind_str = NULL;

	struct stats_remote_inbound_rtp* data;
	data = mem_zalloc(sizeof(*data), NULL);
	memset(data, 0, sizeof(*data));

	kind_str = jzon_str(jitem, "kind");
	data->kind = stats_parse_kind(kind_str);

	jzon_int(&data->packets_lost, jitem, "packetsLost");
	jzon_double(&data->jitter, jitem, "jitter");

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
	jzon_double(&data->current_rtt, jitem, "currentRoundTripTime");

	id_str = jzon_str(jitem, "id");
	if (id_str) {
		str_dup(&data->id, id_str);
	}

	local_candidate_id_str = jzon_str(jitem, "localCandidateId");
	if (local_candidate_id_str) {
		str_dup(&data->local_candidate_id, local_candidate_id_str);
	}

	return data;
}

static struct stats_local_candidate *parse_local_candidate(struct json_object *jitem)
{
	const char* id_str = NULL;
	const char* proto_str = NULL;
	const char* cand_str = NULL;

	struct stats_local_candidate *data;
	data = mem_zalloc(sizeof(*data),local_candidate_destructor);

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

static int parse_json(const char *report, struct stats_obj* stats_obj) {
	const char* type_str = NULL;
	const char* kind_str = NULL;
	int err = 0;

	if (!report || !stats_obj) {
		return EINVAL;
	}

	struct json_object *jobj;
	err = jzon_decode(&jobj, report, strlen(report));
	if (err) {
		return EPROTO;
	}

	// jzon_dump(jobj);

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
				struct stats_inbound_rtp* irp = NULL;
				irp = parse_inbound_rtp(jitem);
				if (irp) {
					list_append(&stats_obj->inbound_rtp, &irp->le, irp);
				}
			}
				break;

			case STATS_TYPE_OUTBOUND_RTP: {
				struct stats_outbound_rtp* orp = NULL;
				orp = parse_outbound_rtp(jitem);
				if (orp) {
					list_append(&stats_obj->outbound_rtp, &orp->le, orp);
				}
			}
				break;

			case STATS_TYPE_REMOTE_INBOUND_RTP: {
				struct stats_remote_inbound_rtp* rirp = NULL;
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
					struct stats_audio_source* asp = NULL;
					asp = parse_audio_source(jitem);
					if (asp) {
						list_append(&stats_obj->audio_source, &asp->le, asp);
					}
				}
			}
				break;

			case STATS_TYPE_CANDIDATE_PAIR: {
				struct stats_candidate_pair* cp = NULL;
				cp = parse_candidate_pair(jitem);
				if (cp) {
					list_append(&stats_obj->candidate_pair, &cp->le, cp);
				}
			}
				break;

			case STATS_TYPE_LOCAL_CANDIDATE: {
				struct stats_local_candidate* lc = NULL;
				lc = parse_local_candidate(jitem);
				if (lc) {
					list_append(&stats_obj->local_candidate, &lc->le, lc);
				}
			}
				break;

			case STATS_TYPE_TRANSPORT: {
				struct stats_transport* tr = NULL;
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

int stats_update(struct avs_stats *stats, const char *report_json)
{
	int err = 0;
	
	if (!stats || !report_json)
		return EINVAL;

	memset(&stats->report, 0, sizeof(stats->report));

	struct stats_obj stats_obj = {.audio_source = LIST_INIT,
							.inbound_rtp = LIST_INIT,
							.outbound_rtp = LIST_INIT,
							.remote_inbound_rtp = LIST_INIT,
							.candidate_pair = LIST_INIT,
							.local_candidate = LIST_INIT,
							.transport = LIST_INIT,
						};

	err |= parse_json(report_json, &stats_obj);

	err |= read_packet_stats_and_jitter(stats, &stats_obj);
	err |= read_rtt_and_connection(stats, &stats_obj);
	err |= read_audio_level(stats, &stats_obj);

	list_flush(&stats_obj.audio_source);
	list_flush(&stats_obj.inbound_rtp);
	list_flush(&stats_obj.outbound_rtp);
	list_flush(&stats_obj.remote_inbound_rtp);
	list_flush(&stats_obj.candidate_pair);
	list_flush(&stats_obj.local_candidate);
	list_flush(&stats_obj.transport);

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
