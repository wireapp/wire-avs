/*
 * Wire
 * Copyright (C) 2016 Wire Swiss GmbH
 *
 * The Wire Software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * The Wire Software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Wire Software. If not, see <http://www.gnu.org/licenses/>.
 *
 * This module of the Wire Software uses software code from
 * WebRTC (https://chromium.googlesource.com/external/webrtc)
 *
 * *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 * *
 * *  Use of the WebRTC source code on a stand-alone basis is governed by a
 * *  BSD-style license that can be found in the LICENSE file in the root of
 * *  the source tree.
 * *  An additional intellectual property rights grant can be found
 * *  in the file PATENTS.  All contributing project authors to Web RTC may
 * *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_MIXER_MANAGER_OSX_H
#define AUDIO_MIXER_MANAGER_OSX_H

#include "webrtc/modules/audio_device/include/audio_device.h"
#include <pthread.h>

#include <CoreAudio/CoreAudio.h>

namespace webrtc {
	
class audio_mixer_manager_osx
{
public:
	int32_t open_speaker(AudioDeviceID device_id);
	int32_t open_microphone(AudioDeviceID device_id);
	int32_t set_speaker_volume(uint32_t volume);
	int32_t speaker_volume(uint32_t& volume) const;
	int32_t max_speaker_volume(uint32_t& max_volume) const;
	int32_t min_speaker_volume(uint32_t& min_volume) const;
	int32_t speaker_volume_step_size(uint16_t& step_size) const;
	int32_t speaker_volume_is_available(bool& available);
	int32_t stereo_playout_is_available(bool& available);
	int32_t stereo_recording_is_available(bool& available);
	int32_t microphone_mute_is_available(bool& available);
	int32_t set_microphone_mute(bool enable);
	int32_t microphone_mute(bool& enabled) const;
	int32_t microphone_volume_is_available(bool& available);
	int32_t set_microphone_volume(uint32_t volume);
	int32_t microphone_volume(uint32_t& volume) const;
	int32_t max_microphone_volume(uint32_t& max_volume) const;
	int32_t min_microphone_volume(uint32_t& min_volume) const;
	int32_t microphone_volume_step_size(uint16_t& step_size) const;
	int32_t close();
	int32_t close_speaker();
	int32_t close_microphone();
	bool speaker_is_initialized() const;
	bool microphone_is_initialized() const;

public:
	audio_mixer_manager_osx();
	~audio_mixer_manager_osx();

private:
	pthread_mutex_t mutex_;

	AudioDeviceID input_device_id_;
	AudioDeviceID output_device_id_;

	uint16_t no_input_channels_;
	uint16_t no_output_channels_;
};
	
}  // namespace webrtc

#endif  // AUDIO_MIXER_MANAGER_OSX_H
