#include "avsstats.h"
#include "peerflow.h"

#include <re.h>
#include <avs.h>

namespace wire {
	void AvsStats::ReadFromRTCReport(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) 
	{
		if (!report) {
			return;
		}

		readPacketStats(report);
		readRtt(report);
		readAudioLevel(report);
		readConnection(report);
		readJitter(report);
	}

	void AvsStats::readPacketStats(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		packets_lost_down = 0;
		packets_received = Packets();
		const auto inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();

		for (const auto& rs: inbound_rtp_stats) {
			if (rs->kind && rs->packets_received) {
				if (*(rs->kind) == "audio") {
					packets_received.audio += *(rs->packets_received);
				} else if (*(rs->kind) == "video") {
					packets_received.video += *(rs->packets_received);
				}
			}
			if (rs->packets_lost) {
				packets_lost_down += *(rs->packets_lost);
			}
		}

		packets_sent = Packets();
		const auto outbound_rtp_stats = report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();

		for (const auto& rs: outbound_rtp_stats) {
			if (rs->kind && rs->packets_sent) {
				if (*(rs->kind) == "audio") {
					packets_sent.audio += *(rs->packets_sent);
				} else if (*(rs->kind) == "video") {
					packets_sent.video += *(rs->packets_sent);
				}
			}
		}

		packets_lost_up = 0;
		const auto remote_inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>();

		for (const auto& rs: remote_inbound_rtp_stats) {
			if (rs->packets_lost) {
				packets_lost_up += *(rs->packets_lost);
			}
		}
	}

	void AvsStats::readAudioLevel(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		audio_level = 0;
		const auto audio_stats = report->GetStatsOfType<webrtc::RTCAudioSourceStats>();

		for (const auto& as: audio_stats) {
			if (as->audio_level) {
				audio_level = (int)(*(as->audio_level) * 255.0);
				break;
			}
		}
	}

	void AvsStats::readRtt(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		rtt = 0.0;
		const auto candidate_pair_stats = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();

		for (const auto& cps: candidate_pair_stats) {
#if 0
			struct mbuf *mb = mbuf_alloc(1024);
#else
			struct mbuf *mb = NULL;
#endif
			if (cps->state && *(cps->state) == "succeeded" && 
					cps->nominated && *(cps->nominated) &&
					cps->current_round_trip_time) {
				rtt = std::max(rtt, (float)(*cps->current_round_trip_time));
			}

			if (NULL == mb) {
				return;
			}

			mbuf_printf(mb, "IceCandidaptePair<%s>:\n", cps->id().c_str());

			if (cps->local_candidate_id) {
				const auto lid = *cps->local_candidate_id;
				const auto lc = report->GetAs<webrtc::RTCLocalIceCandidateStats>(lid);
				if (lc) {
					mbuf_printf(mb, "\tLocal-candidate<%s>:\n", lc->id().c_str());
					for (const auto& attribute : lc->Attributes()) {
						mbuf_printf(mb, "\t\t%s = %s\n", attribute.name(), attribute.ToString().c_str());
					}
				}
			}

			if (cps->remote_candidate_id) {
				const auto rid = *cps->remote_candidate_id;
				const auto rc = report->GetAs<webrtc::RTCLocalIceCandidateStats>(rid);
				if (rc) {
					mbuf_printf(mb, "\tRemote-candidate<%s>:\n", rc->id().c_str());
					for (const auto& attribute : rc->Attributes()) {
						mbuf_printf(mb, "\t\t%s = %s\n", attribute.name(), attribute.ToString().c_str());
					}
				}
			}

			for (const auto& attribute : cps->Attributes()) {
				mbuf_printf(mb, "\t%s = %s\n", attribute.name(), attribute.ToString().c_str());
			}
			char *logstr = NULL;

			mb->pos = 0;
			mbuf_strdup(mb, &logstr, mb->end);
			info("%s\n", logstr);

			mem_deref(mb);
			mem_deref(logstr);
		}
	}

	void AvsStats::readConnection(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {

		// First check if implementation supports / have RTCTransportStats
		// transport stats will contain selectedCandidatePairId which will make
		// finding used candidate easier / robust
		std::vector<std::string> local_candidate_ids;
		const auto transport_stats = report->GetStatsOfType<webrtc::RTCTransportStats>();
		const auto candidate_pair_stats = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();

		for (const auto& ts : transport_stats) {
			if (ts->selected_candidate_pair_id) {
				for (const auto& cps : candidate_pair_stats) {
					if (cps->id() == ts->selected_candidate_pair_id) {
						if (cps->local_candidate_id) {
							local_candidate_ids.push_back(*(cps->local_candidate_id));
						}
					}
				}
			}
		}

		// If we dont have local_candidate_ids we need to iterate through
		// RTCIceCandidatePairStats to filter manually wrt
		// 	"state": "succeeded",
		// 	"nominated": true,
		if (local_candidate_ids.empty()) {
			for (const auto& cps : candidate_pair_stats) {
				if (cps->state && *(cps->state) == "succeeded" && cps->nominated && *(cps->nominated)) {
					if (cps->local_candidate_id) {
						local_candidate_ids.push_back(*(cps->local_candidate_id));
					}
				}
			}
		}

		// Now get the first protocol and transport assuming all will be same
		// Is this assumption correct? what to do else?
		for (const auto& id : local_candidate_ids) {
			const auto lc = report->GetAs<webrtc::RTCLocalIceCandidateStats>(id);
			if (lc) {
				protocol = readProtocol(lc->protocol);
				candidate = readCandidate(lc->candidate_type);
				break;
			}
		}
	}

	stats_protocol AvsStats::readProtocol(const std::optional<std::string>& protocol_opt) {
		static const std::unordered_map<std::string, stats_protocol> protocol_map = {
			{"udp", PROTOCOL_UDP}, {"tcp", PROTOCOL_TCP}};
		if (protocol_opt) {
			const auto it = protocol_map.find(*protocol_opt);
			if (it != protocol_map.end()) {
				return it->second;
			}
		}
		return PROTOCOL_UNKNOWN;
	}

	stats_candidate AvsStats::readCandidate(const std::optional<std::string>& candidate_opt) {
		static const std::unordered_map<std::string, stats_candidate> candidate_map = {
			{"host", CANDIDATE_HOST},
			{"srflx", CANDIDATE_SRFLX},
			{"prflx", CANDIDATE_PRFLX},
			{"relay", CANDIDATE_RELAY}};
		if (candidate_opt) {
			const auto it = candidate_map.find(*candidate_opt);
			if (it != candidate_map.end()) {
				return it->second;
			}
		}
		return CANDIDATE_UNKNOWN;
	}

	void AvsStats::readJitter(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		jitter_up = Jitter();
		const auto inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();

		for (const auto& rs : inbound_rtp_stats) {
			// process non zero audio and video jitters
			if (rs->kind && rs->jitter && *(rs->jitter)) {
				// Use maximum of jitters
				if (*(rs->kind) == "audio") {
					jitter_up.audio = std::max(jitter_up.audio, (float)(*rs->jitter));
				} else if (*(rs->kind) == "video") {
					jitter_up.video = std::max(jitter_up.video, (float)(*rs->jitter));
				}
			}
		}

		jitter_down = Jitter();
		const auto remote_inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>();

		for (const auto& rs : remote_inbound_rtp_stats) {
			// process non zero audio and video jitters
			if (rs->kind && rs->jitter && *(rs->jitter)) {
				// Use maximum of jitters
				if (*(rs->kind) == "audio") {
					jitter_down.audio = std::max(jitter_down.audio, (float)(*rs->jitter));
				} else if (*(rs->kind) == "video") {
					jitter_down.video = std::max(jitter_down.video, (float)(*rs->jitter));
				}
			}
		}
	}

	NetStatsCallback::~NetStatsCallback() {
		setActive(false);
		mem_deref(lock_);
		mem_deref(current_stats_);		
	}

	void NetStatsCallback::setActive(bool active) {
		lock_write_get(lock_);
		active_ = active;
		lock_rel(lock_);
	}

	void NetStatsCallback::OnStatsDelivered( const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		std::string sstat = report->ToJson();
		const char *stats = sstat.c_str();
		char *tmp = NULL;

		int err;

		err = str_dup(&tmp, stats);
		if (err)
			return;

		lock_write_get(lock_);
		// info("peerflow(%p): OnStatsDelivered err=%d len=%d stats=%s\n", pf_, err, (int)str_len(tmp), stats);
		mem_deref(current_stats_);
		current_stats_ = tmp;
		lock_rel(lock_);

		AvsStats avs_stats;
		avs_stats.ReadFromRTCReport(report);

		float loss_down = 0.0f;
		loss_down = avs_stats.packets_lost_down;
		float loss_up = 0.0f;
		loss_up = avs_stats.packets_lost_up;
#if 0
		loss_down = avs.stats.packets_lost_down - packets_lost_down;
		packets_lost_down = avs.stats.packets_lost_down;
		loss_up = avs.stats.packets_lost_up - packets_lost_up;
		packets_lost_up = avs.stats.packets_lost_up;
#endif

		info("stats: pf(%p) audio_level: %d downstream loss: %.02f upstream loss: %.02f rtt: %.02f, connection: {%d, %d}, upstream jitter: {%f, %f}, downstream jitter: {%f, %f}, \n", 
			pf_, avs_stats.audio_level, loss_down, loss_up, avs_stats.rtt, 
			avs_stats.protocol, avs_stats.candidate, 
			avs_stats.jitter_up.audio, avs_stats.jitter_up.video,
			avs_stats.jitter_down.audio, avs_stats.jitter_down.video);

		lock_write_get(lock_);
		if (active_) {
			peerflow_set_stats(pf_,
						avs_stats.audio_level,
						avs_stats.packets_received.audio,
						avs_stats.packets_received.video,
						avs_stats.packets_sent.audio,
						avs_stats.packets_sent.video,
						loss_up, // upstream packet loss
						loss_down, // downstream packet loss
						1000.0 * avs_stats.rtt, // rtt in ms
						1000.0 * std::max(avs_stats.jitter_up.audio, avs_stats.jitter_up.video), // upstream jitter in ms
						1000.0 * std::max(avs_stats.jitter_down.audio, avs_stats.jitter_down.video), // downstream jitter in ms
						avs_stats.protocol,
						avs_stats.candidate);
		}
		lock_rel(lock_);
	}

	int NetStatsCallback::currentStats(char **stats) {
		int err;
		lock_write_get(lock_);

		if (current_stats_) {
			err = str_dup(stats, current_stats_);
		} else {
			err = ENOENT;
		}

		lock_rel(lock_);
		
		return err;
	}

}