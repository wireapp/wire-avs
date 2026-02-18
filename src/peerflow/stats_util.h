
#ifndef STATS_UTIL_H
#define STATS_UTIL_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

#include <iostream>
// #include "re.h"


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
            std::cout << "Found transport stats with id: " << ts->id() << std::endl;
			if (ts->selected_candidate_pair_id) {
				//info("WPB-23494 pf(%p) selected candidate id: %s", pf_, ts->selected_candidate_pair_id->c_str());
                std::cout << " Where selected candidate to search is: " << *(ts->selected_candidate_pair_id) << std::endl;
				for (const auto& cps : candidate_pair_stats) {
                    std::cout << " Found a candidate pair stats with id: " << cps->id() << std::endl;
					if (cps->id() == ts->selected_candidate_pair_id) {
						if (cps->local_candidate_id) {
							local_candidate_ids.push_back(*(cps->local_candidate_id));
							//info("WPB-23494 pf(%p) local candidate id%s", pf_, cps->local_candidate_id->c_str());
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
            std::cout << "-- empty local candidates" << std::endl;
			for (const auto& cps : candidate_pair_stats) {
				if (cps->state && *(cps->state) == "succeeded" && cps->nominated && *(cps->nominated)) {
					if (cps->local_candidate_id) {
						local_candidate_ids.push_back(*(cps->local_candidate_id));
						//info("WPB-23494 pf(%p) local candidate id thrugh iteration %s", pf_, cps->local_candidate_id->c_str());
					}
				}
			}
		}


		const auto local_candidate_stats = report->GetStatsOfType<webrtc::RTCLocalIceCandidateStats>();

		std::string connection;
		// Now get the first protocl and transport assuming all will be same
		// WPB-23494 Is this assumption correct? what to do else
		if (!local_candidate_ids.empty()) {
			for (const auto& lc : local_candidate_stats) {
				if (lc->protocol && lc->candidate_type) {
					//info("WPB-23494 pf(%p) protocol: %s candidate type: %s", pf_, lc->protocol->c_str(), lc->candidate_type->c_str());
					connection = *(lc->protocol);
					if (*lc->candidate_type == "relay") {
						connection.append("/turn");
					}
					break;
				}
			}
		}

        return connection;
    }
}

#endif