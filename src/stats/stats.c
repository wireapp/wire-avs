
#include <re.h>
#include "avs_stats.h"

struct avs_stats {
	struct stats_report report;

	void *arg;
};


static int read_packet_stats(struct avs_stats *stats, const char *report)
{
	// Parse report for packet stats
	return 0;
}

static int read_rtt(consta char *report)
{
	// Parse report for RTT
	return 0;
}

static int read_audio_level(const char *report)
{
	// Parse report for audio-level
	return 0;
}

static int read_connection(const char *report)
{
	// Parse report for connection info
	return 0;
}

static int read_jitter(const char *report)
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
	
	if (!stats || !stats_json)
		return EINVAL;

	err |= read_packet_stats(stats, report_json);
	err |= read_rtt(stats, report_json);
	err |= read_audio_level(stats, report_json);
	err |= read_connection(stats, report_json);
	err |= read_jitter(stats, report_json);

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

	default:
		return "???";
	}
}

char *stats_cand_name(enum statsa_cand cand)
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
