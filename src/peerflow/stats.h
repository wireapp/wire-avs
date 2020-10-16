
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

#include "re.h"
#include "peerflow.h"

namespace wire {

class CallStatsCallback : public rtc::RefCountedObject<webrtc::RTCStatsCollectorCallback>
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
		const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		info("peerflow(%p): stats: %s\n", pf_, report->ToJson().c_str());
	}

private:
	struct peerflow *pf_;
};

class NetStatsCallback : public rtc::RefCountedObject<webrtc::RTCStatsCollectorCallback>
{
public:
	NetStatsCallback(struct peerflow* pf) :
		pf_(pf),
		total_(0),
		lost_(0),
		current_stats_(NULL),		
		active_(true)
	{
		lock_alloc(&lock_);
	}

	virtual ~NetStatsCallback()
	{
		setActive(false);
		mem_deref(lock_);
		mem_deref(current_stats_);		
	}

	void setActive(bool active)
	{
		lock_write_get(lock_);
		active_ = active;
		lock_rel(lock_);
	}

	void OnStatsDelivered(
		const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		std::string sstat = report->ToJson();
		const char *stats = sstat.c_str();
		char *tmp = NULL;
		uint32_t apkts_recv = 0, vpkts_recv = 0;
		uint32_t apkts_sent = 0, vpkts_sent = 0;
		int err;

		lock_write_get(lock_);	       		
		err = str_dup(&tmp, stats);
		info("peerflow(%p): OnStatsDelivered err=%d len=%d\n", pf_, err, (int)str_len(tmp));
		mem_deref(current_stats_);
		current_stats_ = tmp;
		lock_rel(lock_);
		
		std::vector<const webrtc::RTCInboundRTPStreamStats*> streamStats =
			report->GetStatsOfType<webrtc::RTCInboundRTPStreamStats>();
		std::vector<const webrtc::RTCInboundRTPStreamStats*>::iterator it;
		uint32_t packetsLost = 0, packetsTotal = 0;

		for (it = streamStats.begin(); it != streamStats.end(); it++) {
			const webrtc::RTCInboundRTPStreamStats* s = *it;

			if (s->media_type.is_defined()) {
				if (*s->media_type == "video") {
					vpkts_recv += *s->packets_received;
				}
				else if (*s->media_type == "audio") {
					apkts_recv += *s->packets_received;
				}
			}
			if (s->packets_received.is_defined()) {
				packetsTotal += *s->packets_received;
			}
			if (s->packets_lost.is_defined()) {
				packetsTotal += *s->packets_lost;
				packetsLost += *s->packets_lost;
			}
		}

		std::vector<const webrtc::RTCOutboundRTPStreamStats*> ostreamStats =
			report->GetStatsOfType<webrtc::RTCOutboundRTPStreamStats>();
		std::vector<const webrtc::RTCOutboundRTPStreamStats*>::iterator oit;

		for (oit = ostreamStats.begin(); oit != ostreamStats.end(); oit++) {
			const webrtc::RTCOutboundRTPStreamStats* s = *oit;

			if (s->media_type.is_defined()) {
				if (*s->media_type == "video") {
					vpkts_sent += *s->packets_sent;
				}
				else if (*s->media_type == "audio") {
					apkts_sent += *s->packets_sent;
				}
			}
		}

		if (packetsTotal < total_) {
			total_ = 0;
			lost_ = 0;
		}

		packetsTotal -= total_;
		packetsLost -= lost_;
		total_ += packetsTotal;
		lost_ += packetsLost;

		float downloss = 0.0f;
		if (packetsTotal > 0) {
			downloss = (100.0f * packetsLost) / packetsTotal;
		}

		std::vector<const webrtc::RTCIceCandidatePairStats*> iceStats =
			report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
		std::vector<const webrtc::RTCIceCandidatePairStats*>::iterator iceIt;

		float rtt = 0.0f;

		for (iceIt = iceStats.begin(); iceIt != iceStats.end(); iceIt++) {
			const webrtc::RTCIceCandidatePairStats* s = *iceIt;
			std::string state = *s->state;
			if (state == "succeeded") {
				rtt = *s->current_round_trip_time * 1000.0f;
			}
		}

		info("stats callback: pl: %.02f rtt: %.02f\n", downloss, rtt);
		lock_write_get(lock_);
		if (active_) {
			peerflow_set_stats(pf_,
					   apkts_recv,
					   vpkts_recv,
					   apkts_sent,
					   vpkts_sent,
					   downloss,
					   rtt);
		}
		lock_rel(lock_);
	}

	int currentStats(char **stats)
	{
		int err;
		
		lock_write_get(lock_);

		if (current_stats_)
			err = str_dup(stats, current_stats_);
		else
			err = ENOENT;

		lock_rel(lock_);
		
		return err;
	}

	

private:
	struct peerflow* pf_;
	uint32_t total_, lost_;
	bool active_;
	struct lock *lock_;
	char *current_stats_;	
};

}

#endif

