#ifndef AVSSTATS_H
#define AVSSTATS_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

#include <re.h>
#include <avs.h>

namespace wire {
	struct Jitter : stats_jitter {
		Jitter(float audio = 0, float video = 0): stats_jitter({audio, video}) {}
	};
	struct Packets :stats_packet_counts {
		Packets(uint32_t audio = 0, uint32_t video = 0): stats_packet_counts({audio, video}) {}
	};
	struct AvsStats {

		Jitter jitter_down;
		Jitter jitter_up;
		stats_protocol protocol;
		stats_candidate candidate;
		Packets packets_sent;
		Packets packets_received;
		uint64_t packets_lost_up;
		uint64_t packets_lost_down;
		int audio_level;
		float rtt;

		AvsStats(): protocol(PROTOCOL_UNKNOWN), candidate(CANDIDATE_UNKNOWN) {}

		void ReadFromRTCReport(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		stats_protocol readProtocol(const std::optional<std::string>& protocol_opt);
		stats_candidate readCandidate(const std::optional<std::string>& candidate_opt);
	private:
		void readPacketStats(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		void readRtt(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		void readAudioLevel(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		void readConnection(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
		void readJitter(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
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
		NetStatsCallback(struct peerflow* pf): pf_(pf), packets_lost_up(0), packets_lost_down(0), current_stats_(NULL),	active_(true) {
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
		uint32_t packets_lost_up;
		uint32_t packets_lost_down;
		bool active_;
		struct lock *lock_;
		char *current_stats_;	
	};
}

#endif