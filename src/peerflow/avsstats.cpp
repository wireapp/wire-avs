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

		const auto inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
		const auto outbound_rtp_stats = report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
		readPacketStats(inbound_rtp_stats, outbound_rtp_stats);

		const auto candidate_pair_stats = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
		readRtt(report, candidate_pair_stats);

		const auto audio_stats = report->GetStatsOfType<webrtc::RTCAudioSourceStats>();
		readAudioLevel(audio_stats);

		const auto transport_stats = report->GetStatsOfType<webrtc::RTCTransportStats>();
		readConnection(report, transport_stats, candidate_pair_stats);

		readJitter(inbound_rtp_stats);
	}

	void AvsStats::readPacketStats(const std::vector<const webrtc::RTCInboundRtpStreamStats*>& inbound_rtp_stats, 
						const std::vector<const webrtc::RTCOutboundRtpStreamStats*>& outbound_rtp_stats) {

		packets_lost = 0;
		packets_received = {0.0, 0.0};
		packets_sent = {0.0, 0.0};

		for (const auto& rs: inbound_rtp_stats) {
			if (rs->kind && rs->packets_received) {
				if (*(rs->kind) == "audio") {
					packets_received.first += *(rs->packets_received);
				} else if (*(rs->kind) == "video") {
					packets_received.second += *(rs->packets_received);
				}
			}
			if (rs->packets_lost) {
				packets_lost += *(rs->packets_lost);
			}
		}

		for (const auto& rs: outbound_rtp_stats) {
			if (rs->kind && rs->packets_sent) {
				if (*(rs->kind) == "audio") {
					packets_sent.first += *(rs->packets_sent);
				} else if (*(rs->kind) == "video") {
					packets_sent.second += *(rs->packets_sent);
				}
			}
		}
	}

	void AvsStats::readAudioLevel(const std::vector<const webrtc::RTCAudioSourceStats*>& audio_stats) {
		audio_level = 0;
		for (const auto& as: audio_stats) {
			if (as->audio_level) {
				audio_level = (int)(*(as->audio_level) * 255.0);
				break;
			}
		}
	}

	void AvsStats::readRtt(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
						const std::vector<const webrtc::RTCIceCandidatePairStats*>& candidate_pair_stats) {
		rtt = 0.0;
		for (const auto& cps: candidate_pair_stats) {
#if 0
			struct mbuf *mb = mbuf_alloc(1024);
#else
			struct mbuf *mb = NULL;
#endif
			if (cps->state && *(cps->state) == "succeeded") {
				rtt = *(cps->current_round_trip_time) * 1000.0f;
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

	void AvsStats::readConnection(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
						const std::vector<const webrtc::RTCTransportStats*>& transport_stats, 
						const std::vector<const webrtc::RTCIceCandidatePairStats*>& candidate_pair_stats) {

		// First check if implementation supports / have RTCTransportStats
		// transport stats will contain selectedCandidatePairId which will make
		// finding used candidate easier / robust
		std::vector<std::string> local_candidate_ids;
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
			if (lc && lc->protocol && lc->candidate_type) {
				if (*(lc->candidate_type) == "relay") {
					connection = "Relay/";
				}
				connection.append(*(lc->protocol));
				break;
			}
		}
	}

	void AvsStats::readJitter(const std::vector<const webrtc::RTCInboundRtpStreamStats*>& inbound_rtp_stats) {
		jitter = {0.0, 0.0};
		for (const auto& rs : inbound_rtp_stats) {
			// process non zero audio and video jitters
			if (rs->kind && rs->jitter && *(rs->jitter)) {
				// Use maximum of jitters
				// ToDiscuss: Shall we use mean of nonzero instead? 
				if (*(rs->kind) == "audio") {
					jitter.first = std::max(jitter.first, *(rs->jitter));
				} else if (*(rs->kind) == "video") {
					jitter.second = std::max(jitter.second, *(rs->jitter));
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

		float downloss = 0.0f;
		downloss = avs_stats.packets_lost;
#if 0
		downloss = avs.stats.packets_lost - lost_;
		lost_ = avs.stats.packets_lost;
#endif

		info("stats: pf(%p) audio_level: %d pl: %.02f rtt: %.02f connection: %s, jitter: {%f, %f}\n", 
			pf_, avs_stats.audio_level, downloss, avs_stats.rtt, 
			avs_stats.connection.c_str(), avs_stats.jitter.first, avs_stats.jitter.second);

		lock_write_get(lock_);
		if (active_) {
			peerflow_set_stats(pf_,
					   avs_stats.audio_level,
					   avs_stats.packets_received.first,
					   avs_stats.packets_received.second,
					   avs_stats.packets_sent.first,
					   avs_stats.packets_sent.second,
					   downloss,
					   avs_stats.rtt);
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