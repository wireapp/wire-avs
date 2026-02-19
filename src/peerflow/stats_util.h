
#ifndef STATS_UTIL_H
#define STATS_UTIL_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

namespace wire {

    std::string getConnection(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
		// get connection information
        if (!report) {
            return "";
        }

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

		const auto local_candidate_stats = report->GetStatsOfType<webrtc::RTCLocalIceCandidateStats>();

		std::string connection;
		// Now get the first protocol and transport assuming all will be same
		// WPB-23494 Is this assumption correct? what to do else?
		if (!local_candidate_ids.empty()) {
			for (const auto& lc : local_candidate_stats) {
				if (lc->protocol && lc->candidate_type) {
					if (*lc->candidate_type == "relay") {
						connection = "Relay/";
					}
					connection.append(*(lc->protocol));
					break;
				}
			}
		}

        return connection;
    }

	// WPB-23494: We iterate over the same RTCInboundRtpStreamStats container
	// in stats.h for received packets and packet loss calculation
	// Consider joining these two for a single iteration over verctor
	std::pair<double, double> getJitter(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
        if (!report) {
            return {0.0, 0.0};
        }
		double audio_jitter = 0;
		double video_jitter = 0;
		const auto inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();

		for (const auto& rs : inbound_rtp_stats) {
			// process non zero audio and video jitters
			if (rs->kind && rs->jitter && *(rs->jitter)) {
				// WPB-23494: Taking maximum of jitters
				// or taking average on nonzero values?
				if (*(rs->kind) == "audio") {
					audio_jitter = std::max(audio_jitter, *(rs->jitter));
				}
				else if (*(rs->kind) == "video") {
					video_jitter = std::max(video_jitter, *(rs->jitter));
				}
			}
		}

		return {audio_jitter, video_jitter};
	}
}

#endif