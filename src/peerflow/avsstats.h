#ifndef AVSSTATS_H
#define AVSSTATS_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

#include <re.h>
#include <avs.h>

namespace wire {
	struct Jitter {
		double audio;
		double video;
		Jitter(double audio = 0, double video = 0): audio(audio), video(video) {}
	};
	struct Packets {
		uint32_t audio;
		uint32_t video;
		Packets(uint32_t audio = 0, uint32_t video = 0): audio(audio), video(video) {}
	};
	struct AvsStats {

		Jitter jitter;
		protocol_type protocol;
		candidate_type candidate;
		Packets packets_sent;
		Packets packets_received;
		uint64_t packets_lost;
		int audio_level;
		float rtt;

		AvsStats(): protocol(PROTOCOL_UNKNOWN), candidate(CANDIDATE_UNKNOWN) {}

		void ReadFromRTCReport(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		protocol_type readProtocol(const std::optional<std::string>& protocol_opt);
		candidate_type readCandidate(const std::optional<std::string>& candidate_opt);
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

	class CallStatsCallback : public webrtc::RTCStatsCollectorCallback {
	public:
		CallStatsCallback(struct peerflow *pf): pf_(pf) {}
		virtual ~CallStatsCallback() {}
		void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
			info("peerflow(%p): stats: %s\n", pf_, report->ToJson().c_str());
		}
		void AddRef() const {}
		virtual rtc::RefCountReleaseStatus Release() const {
			return rtc::RefCountReleaseStatus::kDroppedLastRef;
		}

	private:
		struct peerflow *pf_;
	};

	class NetStatsCallback : public webrtc::RTCStatsCollectorCallback {
	public:
		NetStatsCallback(struct peerflow* pf): pf_(pf), lost_(0), current_stats_(NULL),	active_(true) {
			lock_alloc(&lock_);
		}
		virtual ~NetStatsCallback();

		// webrtc::RTCStatsCollectorCallback
		void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		void AddRef() const {}
		virtual rtc::RefCountReleaseStatus Release() const {
			return rtc::RefCountReleaseStatus::kDroppedLastRef;
		}

		void setActive(bool active);
		int currentStats(char **stats);

	private:
		struct peerflow* pf_;
		uint32_t lost_;
		bool active_;
		struct lock *lock_;
		char *current_stats_;	
	};
}

#endif