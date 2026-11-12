
/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef STATS_H
#define STATS_H

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

#include <stdio.h>

#include "re.h"
#include "peerflow.h"

namespace wire {

class CallStatsCallback : public webrtc::RTCStatsCollectorCallback
{
public:
	CallStatsCallback(struct peerflow *pf) :
		pf_(pf)
	{
	}

	virtual ~CallStatsCallback()
	{
	}

	void OnStatsDelivered(
		const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		info("peerflow(%p): stats: %s\n", pf_, report->ToJson().c_str());
	}

	void AddRef() const {
	}

	virtual webrtc::RefCountReleaseStatus Release() const {
		return webrtc::RefCountReleaseStatus::kDroppedLastRef;
	}

private:
	struct peerflow *pf_;
};

class NetStatsCallback : public webrtc::RTCStatsCollectorCallback
{
public:
	NetStatsCallback(struct peerflow* pf, struct avs_stats *stats) :
		pf_(pf),
		stats_(stats),
		active_(true)
	{
		lock_alloc(&lock_);
	}

	virtual ~NetStatsCallback()
	{
		setActive(false);
		mem_deref(lock_);
	}

	void setActive(bool active)
	{
		lock_write_get(lock_);
		active_ = active;
		lock_rel(lock_);
	}

	/* Whether audio is sent with RED (1/0, -1 unknown), for the log */
	void setSendRed(int red)
	{
		lock_write_get(lock_);
		send_red_ = red;
		lock_rel(lock_);
	}

	/* The selected pair's RTT from the last stats (ms, -1 none yet),
	 * for adaptive RED */
	int pairRttMs()
	{
		int rtt;

		lock_write_get(lock_);
		rtt = pair_rtt_ms_;
		lock_rel(lock_);

		return rtt;
	}

	void OnStatsDelivered(
		const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		std::string rstr = report->ToJson();
		const char *report_json = rstr.c_str();

		lock_write_get(lock_);		
		if (active_) {
			stats_update(stats_, report_json);
			updatePairRtt(report);
			logPeriodic(report);
		}
		lock_rel(lock_);
	}

	void AddRef() const {
	}

	virtual webrtc::RefCountReleaseStatus Release() const {
		return webrtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	

private:
	/* Audio counters at the last periodic log, to turn them into
	 * rates over the interval.
	 */
	struct Counters {
		int64_t ts_us = -1;
		uint64_t out_bytes = 0;     /* payload + RTP header */
		uint64_t out_pkts = 0;
		uint64_t in_bytes = 0;
		int64_t in_lost = 0;
		uint64_t in_samples = 0;
		uint64_t in_concealed = 0;
		double in_jb_delay = 0;
		uint64_t in_jb_emitted = 0;
	};

	static const int64_t LOG_INTERVAL_US = 5000000;

	/* Keeps the last RTT the selected pair had; none while there is
	 * no pair (ICE down) */
	void updatePairRtt(
		const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		for (const auto *tr : report->GetStatsOfType<
			     webrtc::RTCTransportStats>()) {
			const webrtc::RTCIceCandidatePairStats *cp;

			if (!tr->selected_candidate_pair_id)
				continue;
			cp = report->GetAs<webrtc::RTCIceCandidatePairStats>(
				*tr->selected_candidate_pair_id);
			if (!cp || !cp->current_round_trip_time)
				continue;

			pair_rtt_ms_ = (int)(*cp->current_round_trip_time
					     * 1000.0);
			break;
		}
	}

	/* Every LOG_INTERVAL_US log one line with what shows whether
	 * audio adapts to the network: the opus target bitrate and the
	 * sent rate against the bandwidth estimate, RTT and loss.
	 * The first report only primes the counters.
	 */
	void logPeriodic(
		const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		const int64_t now_us = report->timestamp().us();
		Counters cur;
		char buf[512];
		size_t n = 0;
		double dt;

		/* Off: forget the counters, so turning it on primes
		 * them again rather than averaging over the gap.
		 */
		if (!peerflow_get_stats_log()) {
			prev_ = Counters();
			return;
		}
		if (prev_.ts_us >= 0 && now_us - prev_.ts_us < LOG_INTERVAL_US)
			return;

		cur.ts_us = now_us;
		dt = (now_us - prev_.ts_us) / 1e6;

#define APPEND(...)						\
		if (n < sizeof(buf))				\
			n += snprintf(buf + n, sizeof(buf) - n, __VA_ARGS__)

		for (const auto *out : report->GetStatsOfType<
			     webrtc::RTCOutboundRtpStreamStats>()) {
			if (out->kind.value_or("") != "audio")
				continue;

			const webrtc::RTCCodecStats *codec = out->codec_id ?
				report->GetAs<webrtc::RTCCodecStats>(
					*out->codec_id) : nullptr;
			std::string mime = codec ?
				codec->mime_type.value_or("?") : "?";

			cur.out_bytes = out->bytes_sent.value_or(0) +
				out->header_bytes_sent.value_or(0);
			cur.out_pkts = out->packets_sent.value_or(0);

			APPEND(" out: codec=%s red=%s target=%.1f kbps",
			       mime.substr(mime.find('/') + 1).c_str(),
			       send_red_ > 0 ? "on" :
			       send_red_ == 0 ? "off" : "?",
			       out->target_bitrate.value_or(0) / 1000.0);
			if (prev_.ts_us >= 0 && dt > 0) {
				APPEND(" rtp=%.1f kbps pkts=%.1f/s",
				       (cur.out_bytes - prev_.out_bytes)
				       * 8 / dt / 1000.0,
				       (cur.out_pkts - prev_.out_pkts) / dt);
			}
			break;
		}

		for (const auto *ri : report->GetStatsOfType<
			     webrtc::RTCRemoteInboundRtpStreamStats>()) {
			if (ri->kind.value_or("") != "audio")
				continue;

			APPEND(" | remote: loss=%.1f%% lost=%d rtt=%.0f ms",
			       ri->fraction_lost.value_or(0) * 100.0,
			       ri->packets_lost.value_or(0),
			       ri->round_trip_time.value_or(0) * 1000.0);
			break;
		}

		for (const auto *tr : report->GetStatsOfType<
			     webrtc::RTCTransportStats>()) {
			const webrtc::RTCIceCandidatePairStats *cp;

			if (!tr->selected_candidate_pair_id)
				continue;
			cp = report->GetAs<webrtc::RTCIceCandidatePairStats>(
				*tr->selected_candidate_pair_id);
			if (!cp)
				continue;

			APPEND(" | pair: bwe=%.1f kbps rtt=%.0f ms",
			       cp->available_outgoing_bitrate.value_or(0)
			       / 1000.0,
			       cp->current_round_trip_time.value_or(0)
			       * 1000.0);
			break;
		}

		for (const auto *in : report->GetStatsOfType<
			     webrtc::RTCInboundRtpStreamStats>()) {
			if (in->kind.value_or("") != "audio")
				continue;

			cur.in_bytes = in->bytes_received.value_or(0) +
				in->header_bytes_received.value_or(0);
			cur.in_lost = in->packets_lost.value_or(0);
			cur.in_samples = in->total_samples_received.value_or(0);
			cur.in_concealed = in->concealed_samples.value_or(0);
			cur.in_jb_delay = in->jitter_buffer_delay.value_or(0);
			cur.in_jb_emitted =
				in->jitter_buffer_emitted_count.value_or(0);

			APPEND(" | in: jitter=%.0f ms",
			       in->jitter.value_or(0) * 1000.0);
			if (prev_.ts_us >= 0 && dt > 0) {
				uint64_t samples =
					cur.in_samples - prev_.in_samples;
				uint64_t emitted =
					cur.in_jb_emitted - prev_.in_jb_emitted;

				APPEND(" rtp=%.1f kbps lost=+%lld"
				       " concealed=%.1f%% jb=%.0f ms",
				       (cur.in_bytes - prev_.in_bytes)
				       * 8 / dt / 1000.0,
				       (long long)(cur.in_lost - prev_.in_lost),
				       samples ? 100.0 * (cur.in_concealed
					- prev_.in_concealed) / samples : 0.0,
				       emitted ? 1000.0 * (cur.in_jb_delay
					- prev_.in_jb_delay) / emitted : 0.0);
			}
			break;
		}
#undef APPEND

		if (prev_.ts_us >= 0 && n > 0)
			info("pf(%p): stats5s:%s\n", pf_, buf);

		prev_ = cur;
	}

	struct peerflow* pf_;
	struct avs_stats *stats_;
	bool active_;
	struct lock *lock_;
	int send_red_ = -1;
	int pair_rtt_ms_ = -1;
	Counters prev_;
};

}

#endif

