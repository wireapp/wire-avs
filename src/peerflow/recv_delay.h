/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#ifndef RECV_DELAY_H
#define RECV_DELAY_H

#include <algorithm>
#include <deque>
#include <map>

#include "api/transport/rtp/rtp_source.h"

namespace wire {

/* How much later than lately the incoming audio is played: per source,
 * the delay of its last played frame (play time - RTP timestamp, so
 * the clocks' offset cancels out) against the lowest one of the last
 * WINDOW_MS. A frame can't be played before it arrives, so when the
 * link turns slow at once and its queue grows, this grows with it
 * (gsm_14: by 0.7 s every second from the switch), seconds before the
 * selected pair goes weak; on a steady link it stays within the
 * jitter. Nothing played since the last tick (a stall: frames are sent
 * all the time, also in silence) adds the time since the last frame to
 * the last excess: the frame that comes next is that much later still
 * (gsm_14 over TURN TCP: no frame for 4 s from the switch).
 * A sender's capture pausing without its RTP timestamp moving on shows
 * as a delay too, for at most WINDOW_MS.
 * Fed from RtpReceiverInterface::GetSources() once per stats tick.
 */
class RecvDelay
{
public:
	/* Adds the audio sources of one tick (now in ms) */
	void add(uint64_t now, const std::vector<webrtc::RtpSource> &srcs)
	{
		for (const webrtc::RtpSource &src : srcs) {
			Source *s;
			int64_t delay;
			int32_t step;

			if (src.source_type() != webrtc::RtpSourceType::SSRC)
				continue;

			s = &sources_[src.source_id()];
			s->seen = now;
			if (s->play_ms == src.timestamp().ms())
				continue;  /* nothing played since */

			if (s->play_ms >= 0) {
				step = (int32_t)(src.rtp_timestamp()
						 - s->rtp_ts);
				s->ext_ts += step;
			}
			s->rtp_ts = src.rtp_timestamp();
			s->play_ms = src.timestamp().ms();

			delay = s->play_ms - s->ext_ts / CLOCK_KHZ;
			s->window.push_back({now, delay});
			s->fresh = true;
		}
	}

	/* The highest excess delay (ms) of the sources, -1 none (no audio
	 * for 10 s, e.g. on hold). Also forgets sources gone for WINDOW_MS
	 * (WebRTC keeps them 10 s after their last frame).
	 */
	int excessMs(uint64_t now)
	{
		int excess = -1;

		for (auto it = sources_.begin(); it != sources_.end();) {
			Source &s = it->second;
			int64_t low;

			if (now - s.seen > WINDOW_MS) {
				it = sources_.erase(it);
				continue;
			}
			while (!s.window.empty()
			       && now - s.window.front().first > WINDOW_MS)
				s.window.pop_front();

			if (s.fresh && !s.window.empty()) {
				low = s.window.back().second;
				for (const auto &w : s.window)
					low = std::min(low, w.second);
				s.excess = s.window.back().second - low;
				s.fresh_at = now;
				excess = std::max(excess, (int)s.excess);
			}
			else if (s.play_ms >= 0) {
				excess = std::max(excess, (int)(s.excess
					+ (int64_t)(now - s.fresh_at)));
			}
			s.fresh = false;
			++it;
		}

		return excess;
	}

private:
	static const int64_t CLOCK_KHZ = 48;  /* opus */
	static const uint64_t WINDOW_MS = 10000;

	struct Source {
		int64_t play_ms = -1;  /* when its last frame was played */
		uint32_t rtp_ts = 0;
		int64_t ext_ts = 0;    /* rtp_ts, unwrapped */
		uint64_t seen = 0;     /* last tick it was among the sources */
		bool fresh = false;    /* played a frame this tick */
		uint64_t fresh_at = 0; /* tick it last played a frame */
		int64_t excess = 0;    /* at fresh_at */
		std::deque<std::pair<uint64_t, int64_t>> window;  /* delays */
	};

	std::map<uint32_t, Source> sources_;
};

}

#endif
