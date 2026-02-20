
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

#include "avsstats.h"
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
		const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		info("peerflow(%p): stats: %s\n", pf_, report->ToJson().c_str());
	}

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}

private:
	struct peerflow *pf_;
};

class NetStatsCallback : public webrtc::RTCStatsCollectorCallback
{
public:
	NetStatsCallback(struct peerflow* pf) :
		pf_(pf),
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

		err = str_dup(&tmp, stats);
		if (err)
			return;

		lock_write_get(lock_);	       		
		info("peerflow(%p): WPB-23494 OnStatsDelivered err=%d len=%d stats=%s\n", pf_, err, (int)str_len(tmp), stats);
		mem_deref(current_stats_);
		current_stats_ = tmp;
		lock_rel(lock_);

		std::vector<const webrtc::RTCInboundRtpStreamStats*> streamStats =
			report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
		std::vector<const webrtc::RTCInboundRtpStreamStats*>::iterator it;
		uint32_t packetsLost = 0;

		for (it = streamStats.begin(); it != streamStats.end(); it++) {
			const webrtc::RTCInboundRtpStreamStats* s = *it;

			if (s->kind) {
				if (s->kind == "video") {
					vpkts_recv += *s->packets_received;
				}
				else if (s->kind == "audio") {
					apkts_recv += *s->packets_received;
				}
			}
			if (s->packets_lost) {
				packetsLost += *s->packets_lost;
			}
		}

		std::vector<const webrtc::RTCOutboundRtpStreamStats*> ostreamStats =
			report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
		std::vector<const webrtc::RTCOutboundRtpStreamStats*>::iterator oit;

		for (oit = ostreamStats.begin(); oit != ostreamStats.end(); oit++) {
			const webrtc::RTCOutboundRtpStreamStats* s = *oit;

			if (s->kind) {
				if (s->kind == "video") {
					vpkts_sent += *s->packets_sent;
				}
				else if (s->kind == "audio") {
					apkts_sent += *s->packets_sent;
				}
			}
		}
		float downloss = 0.0f;

		downloss = packetsLost;
#if 0
		downloss = packetsLost - lost_;
		lost_ = packetsLost;
		lost_ += packetsLost;
#endif

		std::vector<const webrtc::RTCIceCandidatePairStats*> iceStats =
			report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
		std::vector<const webrtc::RTCIceCandidatePairStats*>::iterator iceIt;

		float rtt = 0.0f;

		for (iceIt = iceStats.begin(); iceIt != iceStats.end(); iceIt++) {
			const webrtc::RTCIceCandidatePairStats* s = *iceIt;
#if 0
			struct mbuf *mb = mbuf_alloc(1024);
#else
			struct mbuf *mb = NULL;
#endif
			std::string state = *s->state;
			if (state == "succeeded") {
				rtt = *s->current_round_trip_time * 1000.0f;
			}
			if (mb) {
				mbuf_printf(mb, "IceCandidaptePair<%s>:\n", s->id().c_str());

				if (s->local_candidate_id.has_value()
				    && s->remote_candidate_id.has_value()) {
					std::string lid = *s->local_candidate_id;
					std::string rid = *s->remote_candidate_id;

					std::vector<const webrtc::RTCLocalIceCandidateStats*> lcandStats =
						report->GetStatsOfType<webrtc::RTCLocalIceCandidateStats>();
					std::vector<const webrtc::RTCLocalIceCandidateStats*>::iterator lcandIt;
					bool found = false;
					for (lcandIt = lcandStats.begin(); lcandIt != lcandStats.end() && !found; lcandIt++) {
						const webrtc::RTCLocalIceCandidateStats* c = *lcandIt;

						if (c->id() == lid) {
							mbuf_printf(mb, "\tLocal-candidate<%s>:\n", c->id().c_str());
							for (const auto& attribute : c->Attributes()) {
								mbuf_printf(mb, "\t\t%s = %s\n", attribute.name(), attribute.ToString().c_str());
							}
							found = true;
						}
					}
					std::vector<const webrtc::RTCRemoteIceCandidateStats*> rcandStats =
						report->GetStatsOfType<webrtc::RTCRemoteIceCandidateStats>();
					std::vector<const webrtc::RTCRemoteIceCandidateStats*>::iterator rcandIt;
					found = false;
					for (rcandIt = rcandStats.begin(); rcandIt != rcandStats.end() && !found; rcandIt++) {
						const webrtc::RTCRemoteIceCandidateStats* c = *rcandIt;

						if (c->id() == rid) {
							mbuf_printf(mb, "\tRemote-candidate<%s>:\n", c->id().c_str());
							for (const auto& attribute : c->Attributes()) {
								mbuf_printf(mb, "\t\t%s = %s\n", attribute.name(), attribute.ToString().c_str());
							}
							found = true;
						}
					}
				}
				for (const auto& attribute : s->Attributes()) {
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

		int audio_level = 0;
		bool found = false;
		const webrtc::RTCAudioSourceStats *asrc_stats = NULL;
		for(webrtc::RTCStatsReport::ConstIterator cit = report->begin(); cit != report->end() && !found; cit++) {
			if (streq(cit->type(), "media-source")) {
				found = true;
				const webrtc::RTCStats& cstats = *cit;
				asrc_stats = (const webrtc::RTCAudioSourceStats *)&cstats;
			}
		}
		if (asrc_stats) {
			audio_level = (int)(*asrc_stats->audio_level * 255.0);
		}

		AvsStats avs_stats;
		avs_stats.ReadFromRTCReport(report);

		info("WPB-23494 stats: pf(%p) connection: %s jitter: {%f, %f}", pf_, avs_stats.connection.c_str(), avs_stats.jitter.first, avs_stats.jitter.second);

		info("WPB-23494 stats: pf(%p) audio_level: %d pl: %.02f rtt: %.02f\n", pf_, audio_level, downloss, rtt);
		lock_write_get(lock_);
		if (active_) {
			peerflow_set_stats(pf_,
					   audio_level,
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

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	

private:
	struct peerflow* pf_;
	uint32_t lost_;
	bool active_;
	struct lock *lock_;
	char *current_stats_;	
};

}

#endif

