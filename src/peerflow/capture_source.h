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

#ifndef CAPTURE_SOURCE_H
#define CAPTURE_SOURCE_H

#include "api/media_stream_interface.h"
#include "api/notifier.h"

namespace wire {

	class CaptureSource : private webrtc::RefCountInterface, public webrtc::VideoTrackSourceInterface
{
public:
	CaptureSource();
	~CaptureSource();

	void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
			     const webrtc::VideoSinkWants& wants);

	/* Same as above, but associates the sink with a local
	 * user/client, which is passed to the test stamp handler
	 * (see capture_source_set_stamp_handler).
	 */
	void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
			     const webrtc::VideoSinkWants& wants,
			     const char *userid,
			     const char *clientid);

	void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink);

	void HandleFrame(struct avs_vidframe *frame);

	void AddRef() const;
	webrtc::RefCountReleaseStatus Release() const;

	webrtc::MediaSourceInterface::SourceState state() const;

	bool remote() const { return false; }

	bool is_screencast() const { return false; }

	std::optional<bool> needs_denoising() const { return std::nullopt; }

	bool GetStats(Stats* stats) { return false; }

	bool SupportsEncodedOutput() const { return false; }

	void GenerateKeyFrame() {}

	void AddEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) {}

	void RemoveEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) {}

	void RegisterObserver(webrtc::ObserverInterface* observer) {};
	void UnregisterObserver(webrtc::ObserverInterface* observer) {};

private:
	struct list  _streaml;
	struct lock* _lock;
	bool         _buffer_rotate;
	uint64_t     _ts_fps;
	uint32_t     _fps_count;
	uint32_t     _fps_send;
	uint32_t     _skip_count;
	uint32_t     _skipped;
	uint32_t     _max_pixel_count;
	bool         _black_frames;
};

/* Per-peerflow proxy for the shared CaptureSource which tags the sinks
 * with the local user/client. Only used when a test stamp handler is set.
 */
class TaggedCaptureSource : public webrtc::VideoTrackSourceInterface
{
public:
	TaggedCaptureSource(CaptureSource *src,
			    const char *userid,
			    const char *clientid);
	~TaggedCaptureSource();

	void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
			     const webrtc::VideoSinkWants& wants) override;

	void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

	webrtc::MediaSourceInterface::SourceState state() const override
	{ return _src->state(); }

	bool remote() const override { return false; }

	bool is_screencast() const override { return false; }

	std::optional<bool> needs_denoising() const override
	{ return std::nullopt; }

	bool GetStats(Stats* stats) override { return false; }

	bool SupportsEncodedOutput() const override { return false; }

	void GenerateKeyFrame() override {}

	void AddEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) override {}

	void RemoveEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) override {}

	void RegisterObserver(webrtc::ObserverInterface* observer) override {}
	void UnregisterObserver(webrtc::ObserverInterface* observer) override {}

private:
	CaptureSource *_src;
	char *_userid;
	char *_clientid;
};

};

#endif  // CAPTURE_SOURCE_H

