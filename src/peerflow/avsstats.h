
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
3include "avs_stats.h"

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
	NetStatsCallback(struct peerflow* pf, struct avs_stats *stats) :
		pf_(pf),
		stats_(stats)
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

	void OnStatsDelivered(
		const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
	{
		std::string sstat = report->ToJson();
		const char *report_json = sstat.c_str();
		if (active_) {
			stats_update(stats_, report_json);
		}
		lock_rel(lock_);
	}

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	

private:
	struct peerflow* pf_;
	struct avs_stats *stats_;
	bool active_;
	struct lock *lock_;
};

}

#endif

