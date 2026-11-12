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

/* Adaptive RED around the opus encoder. */

#ifndef AUDIO_ENCODERS_H
#define AUDIO_ENCODERS_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "api/audio_codecs/audio_encoder.h"
#include "rtc_base/buffer.h"

#include "re.h"
#include "avs.h"

/* An audio encoder passing everything on to the one it wraps. */
class ForwardingEncoder : public webrtc::AudioEncoder {
public:
	explicit ForwardingEncoder(std::unique_ptr<webrtc::AudioEncoder> enc)
		: enc_(std::move(enc))
	{
	}

	int SampleRateHz() const override { return enc_->SampleRateHz(); }
	size_t NumChannels() const override { return enc_->NumChannels(); }
	int RtpTimestampRateHz() const override {
		return enc_->RtpTimestampRateHz();
	}
	size_t Num10MsFramesInNextPacket() const override {
		return enc_->Num10MsFramesInNextPacket();
	}
	size_t Max10MsFramesInAPacket() const override {
		return enc_->Max10MsFramesInAPacket();
	}
	int GetTargetBitrate() const override {
		return enc_->GetTargetBitrate();
	}
	void Reset() override { enc_->Reset(); }
	bool SetFec(bool enable) override { return enc_->SetFec(enable); }
	bool SetDtx(bool enable) override { return enc_->SetDtx(enable); }
	bool GetDtx() const override { return enc_->GetDtx(); }
	bool SetApplication(Application app) override {
		return enc_->SetApplication(app);
	}
	void SetMaxPlaybackRate(int hz) override {
		enc_->SetMaxPlaybackRate(hz);
	}
	std::span<std::unique_ptr<webrtc::AudioEncoder>>
	ReclaimContainedEncoders() override {
		return std::span<std::unique_ptr<webrtc::AudioEncoder>>(&enc_, 1);
	}
	bool EnableAudioNetworkAdaptor(absl::string_view config) override {
		return enc_->EnableAudioNetworkAdaptor(config);
	}
	void DisableAudioNetworkAdaptor() override {
		enc_->DisableAudioNetworkAdaptor();
	}
	void OnReceivedUplinkPacketLossFraction(float fraction) override {
		enc_->OnReceivedUplinkPacketLossFraction(fraction);
	}
	void OnReceivedTargetAudioBitrate(int bps) override {
		enc_->OnReceivedTargetAudioBitrate(bps);
	}
	void OnReceivedUplinkBandwidth(int bps,
				       std::optional<int64_t> period_ms) override {
		enc_->OnReceivedUplinkBandwidth(bps, period_ms);
	}
	void OnReceivedUplinkAllocation(
		webrtc::BitrateAllocationUpdate update) override {
		enc_->OnReceivedUplinkAllocation(update);
	}
	void OnReceivedRtt(int rtt_ms) override { enc_->OnReceivedRtt(rtt_ms); }
	void OnReceivedOverhead(size_t bytes) override {
		enc_->OnReceivedOverhead(bytes);
	}
	void SetReceiverFrameLengthRange(int min_ms, int max_ms) override {
		enc_->SetReceiverFrameLengthRange(min_ms, max_ms);
	}
	webrtc::ANAStats GetANAStats() const override {
		return enc_->GetANAStats();
	}
	std::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>>
	GetFrameLengthRange() const override {
		return enc_->GetFrameLengthRange();
	}
	std::optional<std::pair<webrtc::DataRate, webrtc::DataRate>>
	GetBitrateRange() const override {
		return enc_->GetBitrateRange();
	}

protected:
	EncodedInfo EncodeImpl(uint32_t rtp_timestamp,
			       std::span<const int16_t> audio,
			       webrtc::Buffer *encoded) override {
		return enc_->Encode(rtp_timestamp, audio, encoded);
	}

	std::unique_ptr<webrtc::AudioEncoder> enc_;
};


/* Whether a call's adaptive RED is on (1/0, -1 not known yet), for the
 * stats log. The encoder finds it by the id in the opus fmtp
 * (SDP_OWN_RED_ID); peerflow registers it at creation.
 */
struct RedState {
	std::atomic<int> on{-1};
};

static std::mutex red_states_mutex;
static std::map<uint32_t, std::shared_ptr<RedState>> red_states;

static uint32_t red_state_register(void)
{
	std::lock_guard<std::mutex> lock(red_states_mutex);
	uint32_t id;

	do {
		/* Positive, as it goes through an int in the fmtp */
		id = (rand_u32() & 0x7fffffff) | 1;
	} while (red_states.count(id));
	red_states[id] = std::make_shared<RedState>();

	return id;
}

static std::shared_ptr<RedState> red_state_find(uint32_t id)
{
	std::lock_guard<std::mutex> lock(red_states_mutex);
	auto it = red_states.find(id);

	return it != red_states.end() ? it->second : nullptr;
}

static void red_state_unregister(uint32_t id)
{
	std::lock_guard<std::mutex> lock(red_states_mutex);

	red_states.erase(id);
}


/* RED (RFC 2198) done by us rather than by WebRTC, which fixes it for the
 * whole call and sends the copies on top of the bitrate allocated to audio:
 * on a 30-60 kbit/s link that sent 1.2-1.7 times the bandwidth estimate.
 * Here RED and opus share the allocation (target, which includes the
 * headers):
 *
 * - RED is on while opus would get at least RED_ON_BPS for itself and for
 *   each copy at the longest frame length (where the headers take the
 *   least); it goes on after RED_ON_HOLD_MS of that, and off as soon as it
 *   falls below RED_OFF_BPS. Below that, one stream at the full target
 *   sounds better than copies of half of it, which opus can only encode
 *   narrowband.
 * - Except on a lossy link: after RED_LOSS_REPORTS reports in a row with at
 *   least RED_LOSS_ON loss, RED stays on whatever the budget, until no
 *   report had RED_LOSS_OFF for RED_LOSS_HOLD_MS. There the losses are the
 *   link's own (gsm_14: 20 % random loss), and without the copies each one
 *   is a lost frame (5 times the concealment at the 6 kbps floor). With a
 *   short budget opus then stays at its minimum and the copy goes on top,
 *   as WebRTC's RED does.
 * - And while we don't know the loss: before the first loss report, and
 *   when no report came for RED_REPORT_STALE_MS (reports lost, or the link
 *   down for a while). Normal gaps between reports are up to ~15 s.
 * - When the budget falls below RED_OFF_BPS, RED goes off only with a loss
 *   report from after the fall that had less than RED_LOSS_ON. A link that
 *   turns bad mid-call drops the budget at once, but the reports that make
 *   it lossy come 5-15 s apart; without this RED was off in between.
 * - While RED is on, opus gets its share: target minus the headers (IP/UDP/
 *   SRTP and RED), divided by the copies plus one. Adaptive ptime then picks
 *   the frame length for that share. Opus stays at most at its own max (the
 *   negotiated maxaveragebitrate).
 * - The loss passed to opus decides its in-band FEC, and with too little
 *   bitrate for that at the loss given, opus lowers its audio bandwidth to
 *   keep the FEC (mediumband from 17 to 19.5 kbps at 15 %). So: the FEC
 *   loss floor until the first loss report, then the reported loss; while
 *   RED is on, at most RED_OPUS_LOSS (RED already covers a single loss, and
 *   at up to 5 % opus drops the FEC rather than the bandwidth).
 *
 * The packets are what WebRTC's AudioEncoderCopyRed makes. Without a RED
 * payload type (not negotiated), it only does the loss and caps opus at its
 * max.
 */
#define RED_ON_BPS        16000
#define RED_OFF_BPS       14000
#define RED_ON_HOLD_MS    3000
#define RED_OPUS_LOSS     0.05f
#define RED_LOSS_ON       0.10f
#define RED_LOSS_OFF      0.05f
#define RED_LOSS_REPORTS  2
#define RED_LOSS_HOLD_MS  10000
#define RED_REPORT_STALE_MS 20000

class AdaptiveRedEncoder : public ForwardingEncoder {
public:
	AdaptiveRedEncoder(std::unique_ptr<webrtc::AudioEncoder> enc,
			   int red_pt, int depth, float floor,
			   std::shared_ptr<RedState> state)
		: ForwardingEncoder(std::move(enc)),
		  red_pt_(red_pt),
		  depth_(red_pt > 0 ? std::min(std::max(depth, 0), 9) : 0),
		  floor_(floor),
		  max_bps_(enc_->GetTargetBitrate()),
		  state_(std::move(state)),
		  history_(depth_)
	{
		if (state_)
			state_->on = 0;
		sendLoss();
	}

	void Reset() override {
		enc_->Reset();
		for (auto& h: history_) {
			h.first = EncodedInfo();
			h.second.Clear();
		}
	}

	void OnReceivedOverhead(size_t bytes) override {
		overhead_bytes_ = bytes;
		enc_->OnReceivedOverhead(bytes);
	}

	void OnReceivedUplinkAllocation(
		webrtc::BitrateAllocationUpdate update) override {
		last_alloc_ = update;
		target_bps_ = (int)update.target_bitrate.bps();
		decide();
		sendTarget();
	}

	void OnReceivedUplinkBandwidth(int bps,
				       std::optional<int64_t> period_ms) override {
		last_alloc_.reset();
		period_ms_ = period_ms;
		target_bps_ = bps;
		decide();
		sendTarget();
	}

	/* Per RTCP report from the other side: the fraction of our packets
	 * it lost since the last one.
	 */
	void OnReceivedUplinkPacketLossFraction(float fraction) override {
		loss_ = fraction;
		report_ts_ = tmr_jiffies();
		report_since_low_ = true;
		updateLossy();
		sendLoss();
	}

protected:
	EncodedInfo EncodeImpl(uint32_t rtp_timestamp,
			       std::span<const int16_t> audio,
			       webrtc::Buffer *encoded) override {
		/* RED goes on after a hold also when the target stops
		 * changing (as at the allocation cap), and opus's share
		 * depends on the frame length (headers per packet), which
		 * adaptive ptime changes after a packet. The same task queue
		 * as the bitrate updates.
		 */
		if (target_bps_ > 0) {
			const bool was_on = red_on_;

			decide();
			if (red_on_ != was_on || frameMs() != sent_frame_ms_)
				sendTarget();
		}

		primary_.Clear();
		EncodedInfo info = enc_->Encode(rtp_timestamp, audio,
						&primary_);

		if (info.encoded_bytes == 0)
			return info;

		const EncodedInfo leaf = info;

		if (red_on_ && info.encoded_bytes < MAX_RED_BLOCK)
			encodeRed(rtp_timestamp, info, encoded);
		else
			encoded->AppendData(primary_);

		/* Keep the copies also with RED off, so it has them as
		 * soon as it is on again.
		 */
		if (leaf.speech && depth_ > 0) {
			for (size_t i = history_.size() - 1; i > 0; --i) {
				history_[i].first = history_[i - 1].first;
				history_[i].second.SetData(history_[i - 1].second);
			}
			history_[0].first = leaf;
			history_[0].second.SetData(primary_);
		}

		return info;
	}

private:
	static const size_t MAX_PACKET = 1200;
	static const size_t MAX_RED_BLOCK = 1 << 10;
	static const uint32_t MAX_TS_DELTA = 1 << 14;
	static const size_t RED_HDR = 4;
	static const size_t RED_LAST_HDR = 1;

	int frameMs() const {
		return std::max((int)enc_->Num10MsFramesInNextPacket(), 1) * 10;
	}

	int maxFrameMs() const {
		auto range = enc_->GetFrameLengthRange();

		return range ? std::max((int)range->second.ms(), frameMs())
			: frameMs();
	}

	static int perPacketBps(size_t bytes, int frame_ms) {
		return (int)(bytes * 8 * 1000 / frame_ms);
	}

	/* Opus's payload bitrate for the target at a frame length, with
	 * RED on or off.
	 */
	int share(bool red, int frame_ms) const {
		int bps = target_bps_ - perPacketBps(overhead_bytes_, frame_ms);

		if (red) {
			bps -= perPacketBps(RED_LAST_HDR + RED_HDR * depth_,
					    frame_ms);
			bps /= depth_ + 1;
		}

		return std::max(std::min(bps, max_bps_), 0);
	}

	/* Pass the target on to opus, its share (with its headers) */
	void sendTarget() {
		const int fl = frameMs();
		const int bps = perPacketBps(overhead_bytes_, fl)
			+ share(red_on_, fl);

		sent_frame_ms_ = fl;
		if (last_alloc_) {
			webrtc::BitrateAllocationUpdate u = *last_alloc_;

			u.target_bitrate = webrtc::DataRate::BitsPerSec(bps);
			enc_->OnReceivedUplinkAllocation(u);
		}
		else {
			enc_->OnReceivedUplinkBandwidth(bps, period_ms_);
		}
	}

	void sendLoss() {
		float loss = loss_ < 0 ? floor_ : loss_;

		if (red_on_)
			loss = std::min(loss, RED_OPUS_LOSS);

		if (sent_loss_ < 0 || std::abs(loss - sent_loss_) >= 0.01f) {
			info("pf: opus: loss %.1f%% (%s %.1f%%, RED %s)\n",
			     loss * 100.0,
			     loss_ < 0 ? "floor" : "reported",
			     (loss_ < 0 ? floor_ : loss_) * 100.0,
			     red_on_ ? "on" : "off");
		}
		sent_loss_ = loss;
		enc_->OnReceivedUplinkPacketLossFraction(loss);
	}

	/* Per loss report: is the link lossy (see the top) */
	void updateLossy() {
		const uint64_t now = tmr_jiffies();

		if (loss_ >= RED_LOSS_OFF)
			lossy_ts_ = now;
		lossy_reports_ = loss_ >= RED_LOSS_ON ? lossy_reports_ + 1 : 0;

		if (!lossy_ && lossy_reports_ >= RED_LOSS_REPORTS) {
			lossy_ = true;
			info("pf: adaptive RED: lossy link: loss %.1f%% (on from "
			     "%d%% in %d reports)\n", loss_ * 100.0,
			     (int)(RED_LOSS_ON * 100), RED_LOSS_REPORTS);
		}
		else if (lossy_ && now - lossy_ts_ >= RED_LOSS_HOLD_MS) {
			lossy_ = false;
			info("pf: adaptive RED: lossy link over: loss %.1f%% "
			     "(below %d%% for %d s)\n", loss_ * 100.0,
			     (int)(RED_LOSS_OFF * 100), RED_LOSS_HOLD_MS / 1000);
		}
	}

	void decide() {
		const uint64_t now = tmr_jiffies();
		const int fl = maxFrameMs();
		const int bps = share(true, fl);

		if (depth_ == 0 || overhead_bytes_ == 0)
			return;

		updateBlind(now);

		const bool low = bps < RED_OFF_BPS;

		if (low && !low_) {
			report_since_low_ = false;
			if (red_on_ && !lossy_ && !blind_) {
				info("pf: adaptive RED: share %.1f kbps below %d, "
				     "off with a loss report below %d%%\n",
				     bps / 1000.0, RED_OFF_BPS / 1000,
				     (int)(RED_LOSS_ON * 100));
			}
		}
		low_ = low;

		if (red_on_) {
			if (low && !lossy_ && !blind_ && report_since_low_
			    && loss_ < RED_LOSS_ON)
				setRed(false, bps, fl);
		}
		else if (lossy_ || blind_) {
			setRed(true, bps, fl);
		}
		else if (bps < RED_ON_BPS) {
			on_ts_ = 0;
		}
		/* The first target: right away */
		else if (!decided_) {
			setRed(true, bps, fl);
		}
		else if (!on_ts_) {
			on_ts_ = now;
		}
		else if (now - on_ts_ >= RED_ON_HOLD_MS) {
			setRed(true, bps, fl);
		}

		decided_ = true;
	}

	/* No loss report yet, or none for RED_REPORT_STALE_MS */
	void updateBlind(uint64_t now) {
		const bool blind = loss_ < 0
			|| now - report_ts_ >= RED_REPORT_STALE_MS;

		if (blind == blind_)
			return;

		blind_ = blind;
		if (blind) {
			info("pf: adaptive RED: no loss report for %d s\n",
			     RED_REPORT_STALE_MS / 1000);
		}
		else {
			info("pf: adaptive RED: %s\n", had_reports_
			     ? "loss reports again" : "first loss report");
		}
		had_reports_ = true;
	}

	void setRed(bool on, int bps, int frame_ms) {
		red_on_ = on;
		on_ts_ = 0;
		if (state_)
			state_->on = on ? 1 : 0;
		info("pf: adaptive RED %s: target %.1f kbps, opus share %.1f "
		     "kbps at %d ms (on from %d, off below %d%s)\n",
		     on ? "on" : "off", target_bps_ / 1000.0, bps / 1000.0,
		     frame_ms, RED_ON_BPS / 1000, RED_OFF_BPS / 1000,
		     lossy_ ? "; lossy link"
		     : blind_ ? "; no loss reports" : "");
		sendLoss();
	}

	/* As AudioEncoderCopyRed: the copies that fit (size, timestamp
	 * delta), oldest first, then the primary.
	 */
	void encodeRed(uint32_t rtp_timestamp, EncodedInfo& info,
		       webrtc::Buffer *encoded) {
		size_t hdr_len = RED_LAST_HDR;
		size_t avail = MAX_PACKET - info.encoded_bytes;
		size_t n = 0;

		for (; n < history_.size(); ++n) {
			const EncodedInfo& h = history_[n].first;

			if (h.encoded_bytes == 0
			    || avail < RED_HDR + h.encoded_bytes
			    || rtp_timestamp - h.encoded_timestamp
			       >= MAX_TS_DELTA)
				break;
			avail -= RED_HDR + h.encoded_bytes;
			hdr_len += RED_HDR;
		}

		encoded->SetSize(hdr_len);
		size_t off = 0;
		for (size_t i = n; i > 0; --i) {
			const EncodedInfo& h = history_[i - 1].first;
			const uint32_t delta =
				info.encoded_timestamp - h.encoded_timestamp;
			uint8_t *d;

			encoded->AppendData(history_[i - 1].second);
			d = encoded->data() + off;
			d[0] = (uint8_t)(h.payload_type | 0x80);
			d[1] = (uint8_t)((delta << 2) >> 8);
			d[2] = (uint8_t)(((delta << 2) & 0xfc)
					 | ((h.encoded_bytes >> 8) & 0x03));
			d[3] = (uint8_t)(h.encoded_bytes & 0xff);
			off += RED_HDR;
			info.redundant.push_back(h);
		}
		if (n > 0)
			info.redundant.push_back(info);

		encoded->AppendData(primary_);
		encoded->data()[off] = (uint8_t)info.payload_type;

		info.payload_type = red_pt_;
		info.encoded_bytes = encoded->size();
	}

	const int red_pt_;
	const int depth_;
	const float floor_;
	const int max_bps_;
	std::shared_ptr<RedState> state_;
	bool red_on_ = false;
	bool decided_ = false;        /* had a target with the overhead */
	uint64_t on_ts_ = 0;          /* since when RED could be on */
	bool lossy_ = false;          /* RED on whatever the budget */
	int lossy_reports_ = 0;       /* reports in a row >= RED_LOSS_ON */
	uint64_t lossy_ts_ = 0;       /* last report >= RED_LOSS_OFF */
	bool blind_ = true;           /* RED on: loss unknown */
	bool had_reports_ = false;    /* blind_ was false once */
	uint64_t report_ts_ = 0;      /* last loss report */
	bool low_ = false;            /* budget below RED_OFF_BPS */
	bool report_since_low_ = false; /* a loss report since it fell */
	size_t overhead_bytes_ = 0;

	int target_bps_ = 0;          /* last target, with the headers */
	std::optional<webrtc::BitrateAllocationUpdate> last_alloc_;
	std::optional<int64_t> period_ms_;
	int sent_frame_ms_ = 0;
	float loss_ = -1;             /* last reported, -1 none yet */
	float sent_loss_ = -1;

	webrtc::Buffer primary_;
	std::vector<std::pair<EncodedInfo, webrtc::Buffer>> history_;
};

#endif
