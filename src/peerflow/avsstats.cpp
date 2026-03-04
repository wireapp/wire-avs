#include "avsstats.h"
#include "peerflow.h"

#include <re.h>
#include <avs.h>

namespace wire {
	

	NetStatsCallback::~NetStatsCallback() {
		setActive(false);
		mem_deref(lock_);
		mem_deref(current_stats_);		
	}

	void NetStatsCallback::setActive(bool active) {
		lock_write_get(lock_);
		active_ = active;
		lock_rel(lock_);
	}

	void NetStatsCallback::OnStatsDelivered( const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {

	int NetStatsCallback::currentStats(char **stats) {
		int err;
		lock_write_get(lock_);

		if (current_stats_) {
			err = str_dup(stats, current_stats_);
		} else {
			err = ENOENT;
		}

		lock_rel(lock_);
		
		return err;
	}

}
