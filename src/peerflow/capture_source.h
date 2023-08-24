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

class CaptureSource : public webrtc::Notifier<webrtc::VideoTrackSourceInterface>
{
public:
	void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
			     const rtc::VideoSinkWants& wants);

	void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink);

	void HandleFrame(struct avs_vidframe *frame);

	static CaptureSource* GetInstance();
	static void ReleaseInstance();

	void AddRef() const;
	rtc::RefCountReleaseStatus Release() const;

	webrtc::MediaSourceInterface::SourceState state() const;

	bool remote() const { return false; }

	bool is_screencast() const { return false; }

	absl::optional<bool> needs_denoising() const { return absl::nullopt; }

	bool GetStats(Stats* stats) { return false; }

	bool SupportsEncodedOutput() const { return false; }

	void GenerateKeyFrame() {}

	void AddEncodedSink(rtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) {}

	void RemoveEncodedSink(rtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink) {}

private:
	CaptureSource();
	~CaptureSource();

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

};

#endif  // CAPTURE_SOURCE_H

