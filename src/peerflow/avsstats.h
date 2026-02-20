#ifndef AVSSTATS_H
#define AVSSTATS_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

namespace wire {
	struct AvsStats {
		std::string connection;
		std::pair<double, double> jitter;
		std::pair<uint64_t, uint64_t> packets_sent;
		std::pair<uint64_t, uint64_t> packets_received;
		uint64_t packets_lost;
		int audio_level;
		float rtt;

		void ReadFromRTCReport(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

		private:
			void readPacketStats(const std::vector<const webrtc::RTCInboundRtpStreamStats*>& inbound_rtp_stats, 
								const std::vector<const webrtc::RTCOutboundRtpStreamStats*>& outbound_rtp_stats);
			void readRtt(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
						const std::vector<const webrtc::RTCIceCandidatePairStats*>& candidate_pair_stats);
			void readAudioLevel(const std::vector<const webrtc::RTCAudioSourceStats*>& audio_stats);
			void readConnection(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
								const std::vector<const webrtc::RTCTransportStats*>& transport_stats, 
								const std::vector<const webrtc::RTCIceCandidatePairStats*>& candidate_pair_stats);
			void readJitter(const std::vector<const webrtc::RTCInboundRtpStreamStats*>& inbound_rtp_stats);
	};
}

#endif