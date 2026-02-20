
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

