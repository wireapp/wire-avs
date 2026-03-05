enum stats_type {
	STATS_TYPE_UNKNOWN,
	STATS_TYPE_INBOUND_RTP,
	STATS_TYPE_OUTBOUND_RTP,
	STATS_TYPE_REMOTE_INBOUND_RTP,
};

enum stats_type parse_type(const char *type_str);

enum stats_kind {
	STATS_KIND_UNKNOWN,
	STATS_KIND_AUDIO,
	STATS_KIND_VIDEO,
};

enum stats_kind parse_kind(const char *kind_str);