#include "avsstats.h"

namespace wire {
    void AvsStats::ReadFromRTCReport(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) 
    {
        if (!report) {
            return;
        }

        // get individual RTCReport types that will be used in statistics gathering
        // and be shared 
        const auto transport_stats = report->GetStatsOfType<webrtc::RTCTransportStats>();
        const auto candidate_pair_stats = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
        const auto inbound_rtp_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();

        readConnection(report, transport_stats, candidate_pair_stats);
        readJitter(inbound_rtp_stats);
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
            if (lc->protocol && lc->candidate_type) {
                if (*lc->candidate_type == "relay") {
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
                }
                else if (*(rs->kind) == "video") {
                    jitter.second = std::max(jitter.second, *(rs->jitter));
                }
            }
        }
    }
}