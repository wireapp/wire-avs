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

#include <re.h>
#include "audio_mixer_manager_osx.h"
#include <sys/time.h>
#include <string.h>
#include <unistd.h>             // getpid()

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

namespace webrtc {
	
audio_mixer_manager_osx::audio_mixer_manager_osx() :
	input_device_id_(kAudioObjectUnknown),
	output_device_id_(kAudioObjectUnknown),
	no_input_channels_(0),
	no_output_channels_(0)
{
    pthread_mutex_init(&mutex_, NULL);
}

audio_mixer_manager_osx::~audio_mixer_manager_osx(){
	close();

	pthread_mutex_destroy(&mutex_);
}

int32_t audio_mixer_manager_osx::close(){
	close_speaker();
	close_microphone();

	return 0;
}

int32_t audio_mixer_manager_osx::close_speaker(){
	pthread_mutex_lock(&mutex_);

	output_device_id_ = kAudioObjectUnknown;
	no_output_channels_ = 0;

	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::close_microphone(){
	pthread_mutex_lock(&mutex_);

	input_device_id_ = kAudioObjectUnknown;
	no_input_channels_ = 0;

	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::open_speaker(AudioDeviceID device_id){
	pthread_mutex_lock(&mutex_);

	OSStatus err = noErr;
	UInt32 size = 0;
	pid_t hogPid = -1;

	output_device_id_ = device_id;

	AudioObjectPropertyAddress propertyAddress = { kAudioDevicePropertyHogMode,
			kAudioDevicePropertyScopeOutput, 0 };

	size = sizeof(hogPid);
	err = AudioObjectGetPropertyData(output_device_id_,
			&propertyAddress, 0, NULL, &size, &hogPid);

	if (hogPid == -1){
		info("audio_mixer_manager_osx: No process has hogged the input device \n");
	}
	else if (hogPid == getpid()){
		info("audio_mixer_manager_osx: Our process has hogged the input device \n");
	} else{
		warning("audio_mixer_manager_osx:Another process (pid = %d) has hogged the input device",
			static_cast<int> (hogPid));

		pthread_mutex_unlock(&mutex_);
		return -1;
    }

	propertyAddress.mSelector = kAudioDevicePropertyStreamFormat;

	AudioStreamBasicDescription streamFormat;
	size = sizeof(AudioStreamBasicDescription);
	memset(&streamFormat, 0, size);
	err = AudioObjectGetPropertyData(output_device_id_,
			&propertyAddress, 0, NULL, &size, &streamFormat);

	no_output_channels_ = streamFormat.mChannelsPerFrame;

	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::open_microphone(AudioDeviceID device_id){
	pthread_mutex_lock(&mutex_);

	OSStatus err = noErr;
	UInt32 size = 0;
	pid_t hogPid = -1;

	input_device_id_ = device_id;

	AudioObjectPropertyAddress propertyAddress = { kAudioDevicePropertyHogMode,
			kAudioDevicePropertyScopeInput, 0 };
	size = sizeof(hogPid);
	err =AudioObjectGetPropertyData(input_device_id_,
			&propertyAddress, 0, NULL, &size, &hogPid);
	if (hogPid == -1){
		info("audio_mixer_manager_osx: No process has hogged the input device \n");
	}
	else if (hogPid == getpid()){
		info("audio_mixer_manager_osx: Our process has hogged the input device \n");
	} else{
		warning("audio_mixer_manager_osx: Another process (pid = %d) has hogged the input device \n",
			static_cast<int> (hogPid));

		pthread_mutex_unlock(&mutex_);
		return -1;
	}

	propertyAddress.mSelector = kAudioDevicePropertyStreamFormat;

	AudioStreamBasicDescription streamFormat;
	size = sizeof(AudioStreamBasicDescription);
	memset(&streamFormat, 0, size);
	err = AudioObjectGetPropertyData(input_device_id_,
			&propertyAddress, 0, NULL, &size, &streamFormat);

	no_input_channels_ = streamFormat.mChannelsPerFrame;

	pthread_mutex_unlock(&mutex_);
	return 0;
}

bool audio_mixer_manager_osx::speaker_is_initialized() const{
	return (output_device_id_ != kAudioObjectUnknown);
}

bool audio_mixer_manager_osx::microphone_is_initialized() const{
	return (input_device_id_ != kAudioObjectUnknown);
}

int32_t audio_mixer_manager_osx::set_speaker_volume(uint32_t volume){
	pthread_mutex_lock(&mutex_);

	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}

	OSStatus err = noErr;
	UInt32 size = 0;
	bool success = false;

	// volume range is 0.0 - 1.0, convert from 0 -255
	const Float32 vol = (Float32)(volume / 255.0);

	assert(vol <= 1.0 && vol >= 0.0);

	AudioObjectPropertyAddress propertyAddress = {
			kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput,
			0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(output_device_id_, &propertyAddress,
										&isSettable);
	if (err == noErr && isSettable){
		size = sizeof(vol);
		err = AudioObjectSetPropertyData(output_device_id_,
				&propertyAddress, 0, NULL, size, &vol);

		pthread_mutex_unlock(&mutex_);
		return 0;
	}

    // Otherwise try to set each channel.
	for (UInt32 i = 1; i <= no_output_channels_; i++) {
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(output_device_id_, &propertyAddress,
											&isSettable);
		if (err == noErr && isSettable){
			size = sizeof(vol);
			err = AudioObjectSetPropertyData(output_device_id_,
											&propertyAddress, 0, NULL, size, &vol);
        }
		success = true;
	}

	if (!success){
		warning("audio_mixer_manager_osx: Unable to set a volume on any output channel \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}

	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::speaker_volume(uint32_t& volume) const{
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;
	UInt32 size = 0;
	unsigned int channels = 0;
	Float32 channelVol = 0;
	Float32 vol = 0;

	AudioObjectPropertyAddress propertyAddress = {
			kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput,
			0 };
	Boolean hasProperty = AudioObjectHasProperty(output_device_id_,&propertyAddress);
	if (hasProperty){
		size = sizeof(vol);
		err = AudioObjectGetPropertyData(output_device_id_,
				&propertyAddress, 0, NULL, &size, &vol);

		// vol 0.0 to 1.0 -> convert to 0 - 255
		volume = static_cast<uint32_t> (vol * 255 + 0.5);
    } else{
		vol = 0;
        for (UInt32 i = 1; i <= no_output_channels_; i++){
			channelVol = 0;
			propertyAddress.mElement = i;
			hasProperty = AudioObjectHasProperty(output_device_id_,&propertyAddress);
			if (hasProperty){
				size = sizeof(channelVol);
				err = AudioObjectGetPropertyData(output_device_id_,
						&propertyAddress, 0, NULL, &size, &channelVol);

				vol += channelVol;
				channels++;
			}
		}

		if (channels == 0){
			warning("audio_mixer_manager_osx: Unable to get a volume on any channel \n");
			return -1;
		}

		assert(channels > 0);
		// vol 0.0 to 1.0 -> convert to 0 - 255
		volume = static_cast<uint32_t> (255 * vol / channels + 0.5);
	}

	info("audio_mixer_manager_osx: speaker_volume() => vol=%i", vol);

	return 0;
}

int32_t audio_mixer_manager_osx::max_speaker_volume(uint32_t& max_volume) const {
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	max_volume = 255;

	return 0;
}

int32_t audio_mixer_manager_osx::min_speaker_volume(uint32_t& min_volume) const {
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	min_volume = 0;

	return 0;
}

int32_t audio_mixer_manager_osx::speaker_volume_step_size(uint16_t& step_size) const {
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	step_size = 1;

	return 0;
}

int32_t audio_mixer_manager_osx::speaker_volume_is_available(bool& available) {
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;

	AudioObjectPropertyAddress propertyAddress = {
			kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput,
			0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(output_device_id_, &propertyAddress, &isSettable);
	if (err == noErr && isSettable){
		available = true;
		return 0;
	}

	for (UInt32 i = 1; i <= no_output_channels_; i++){
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(output_device_id_, &propertyAddress, &isSettable);
		if (err != noErr || !isSettable){
			available = false;
			warning("audio_mixer_manager_osx: Volume cannot be set for output channel %d, err=%d \n",
					i, err);
			return -1;
		}
	}

	available = true;
	return 0;
}

int32_t audio_mixer_manager_osx::stereo_playout_is_available(bool& available){
	if (output_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	available = (no_output_channels_ == 2);
	return 0;
}

int32_t audio_mixer_manager_osx::stereo_recording_is_available(bool& available){
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	available = (no_input_channels_ == 2);
	return 0;
}

int32_t audio_mixer_manager_osx::microphone_mute_is_available(bool& available){
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;

	AudioObjectPropertyAddress propertyAddress = { kAudioDevicePropertyMute,
			kAudioDevicePropertyScopeInput, 0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
	if (err == noErr && isSettable){
		available = true;
		return 0;
	}

	for (UInt32 i = 1; i <= no_input_channels_; i++){
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
		if (err != noErr || !isSettable){
			available = false;
			warning("audio_mixer_manager_osx: Mute cannot be set for output channel %d, err=%d",
                         i, err);
			return -1;
		}
	}

	available = true;
	return 0;
}

int32_t audio_mixer_manager_osx::set_microphone_mute(bool enable){
	pthread_mutex_lock(&mutex_);

	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}

	OSStatus err = noErr;
	UInt32 size = 0;
	UInt32 mute = enable ? 1 : 0;
	bool success = false;

	AudioObjectPropertyAddress propertyAddress = { kAudioDevicePropertyMute,
			kAudioDevicePropertyScopeInput, 0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
	if (err == noErr && isSettable){
		size = sizeof(mute);
		err = AudioObjectSetPropertyData(input_device_id_,
				&propertyAddress, 0, NULL, size, &mute);

		return 0;
	}

	for (UInt32 i = 1; i <= no_input_channels_; i++){
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
		if (err == noErr && isSettable){
			size = sizeof(mute);
			err = AudioObjectSetPropertyData(input_device_id_,
					&propertyAddress, 0, NULL, size, &mute);
		}
		success = true;
	}

	if (!success){
		warning("audio_mixer_manager_osx: Unable to set mute on any input channel \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}
    
	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::microphone_mute(bool& enabled) const {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;
	UInt32 size = 0;
	unsigned int channels = 0;
	UInt32 channelMuted = 0;
	UInt32 muted = 0;

	AudioObjectPropertyAddress propertyAddress = { kAudioDevicePropertyMute,
			kAudioDevicePropertyScopeInput, 0 };
	Boolean hasProperty = AudioObjectHasProperty(input_device_id_, &propertyAddress);
	if (hasProperty){
		size = sizeof(muted);
		err = AudioObjectGetPropertyData(input_device_id_,
				&propertyAddress, 0, NULL, &size, &muted);

		// 1 means muted
		enabled = static_cast<bool> (muted);
    } else{
		for (UInt32 i = 1; i <= no_input_channels_; i++){
			muted = 0;
			propertyAddress.mElement = i;
			hasProperty = AudioObjectHasProperty(input_device_id_, &propertyAddress);
			if (hasProperty){
				size = sizeof(channelMuted);
				err = AudioObjectGetPropertyData(input_device_id_,
						&propertyAddress, 0, NULL, &size, &channelMuted);

				muted = (muted && channelMuted);
				channels++;
			}
		}

		if (channels == 0){
			warning("audio_mixer_manager_osx: Unable to get mute for any channel \n");
			return -1;
		}

		assert(channels > 0);
		// 1 means muted
		enabled = static_cast<bool> (muted);
	}

	info("audio_mixer_manager_osx: AudioMixerManagerMac::MicrophoneMute() => enabled=%d \n",
                 enabled);

	return 0;
}

int32_t audio_mixer_manager_osx::microphone_volume_is_available(bool& available) {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;

	AudioObjectPropertyAddress
	propertyAddress = { kAudioDevicePropertyVolumeScalar,
						kAudioDevicePropertyScopeInput, 0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
	if (err == noErr && isSettable){
		available = true;
		return 0;
	}

	for (UInt32 i = 1; i <= no_input_channels_; i++){
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
		if (err != noErr || !isSettable){
			available = false;
			warning("audio_mixer_manager_osx: Volume cannot be set for input channel %d, err=%d \n",
				i, err);
			return -1;
		}
	}

	available = true;
	return 0;
}

int32_t audio_mixer_manager_osx::set_microphone_volume(uint32_t volume){
	pthread_mutex_lock(&mutex_);
    
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}
	OSStatus err = noErr;
	UInt32 size = 0;
	bool success = false;

	// volume range is 0.0 - 1.0, convert from 0 - 255
	const Float32 vol = (Float32)(volume / 255.0);

	assert(vol <= 1.0 && vol >= 0.0);

	AudioObjectPropertyAddress
	propertyAddress = { kAudioDevicePropertyVolumeScalar,
						kAudioDevicePropertyScopeInput, 0 };
	Boolean isSettable = false;
	err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
	if (err == noErr && isSettable){
		size = sizeof(vol);
		err = AudioObjectSetPropertyData(input_device_id_,
				&propertyAddress, 0, NULL, size, &vol);

		pthread_mutex_unlock(&mutex_);
		return 0;
    }

	for (UInt32 i = 1; i <= no_input_channels_; i++){
		propertyAddress.mElement = i;
		isSettable = false;
		err = AudioObjectIsPropertySettable(input_device_id_, &propertyAddress, &isSettable);
		if (err == noErr && isSettable){
			size = sizeof(vol);
			err = AudioObjectSetPropertyData(input_device_id_,
					&propertyAddress, 0, NULL, size, &vol);
		}
		success = true;
	}

	if (!success){
		warning("audio_mixer_manager_osx: Unable to set a level on any input channel \n");
		pthread_mutex_unlock(&mutex_);
		return -1;
	}

	pthread_mutex_unlock(&mutex_);
	return 0;
}

int32_t audio_mixer_manager_osx::microphone_volume(uint32_t& volume) const {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	OSStatus err = noErr;
	UInt32 size = 0;
	unsigned int channels = 0;
	Float32 channelVol = 0;
	Float32 volFloat32 = 0;

	AudioObjectPropertyAddress
	propertyAddress = { kAudioDevicePropertyVolumeScalar,
						kAudioDevicePropertyScopeInput, 0 };
	Boolean hasProperty = AudioObjectHasProperty(input_device_id_, &propertyAddress);
	if (hasProperty){
		size = sizeof(volFloat32);
		err = AudioObjectGetPropertyData(input_device_id_,
				&propertyAddress, 0, NULL, &size, &volFloat32);

		// vol 0.0 to 1.0 -> convert to 0 - 255
		volume = static_cast<uint32_t> (volFloat32 * 255 + 0.5);
	} else{
		volFloat32 = 0;
		for (UInt32 i = 1; i <= no_input_channels_; i++){
			channelVol = 0;
			propertyAddress.mElement = i;
			hasProperty = AudioObjectHasProperty(input_device_id_, &propertyAddress);
			if (hasProperty){
				size = sizeof(channelVol);
				err = AudioObjectGetPropertyData(input_device_id_,
						&propertyAddress, 0, NULL, &size, &channelVol);

				volFloat32 += channelVol;
				channels++;
			}
		}

		if (channels == 0){
			warning("audio_mixer_manager_osx: Unable to get a level on any channel \n");
			return -1;
		}

		assert(channels > 0);
		// vol 0.0 to 1.0 -> convert to 0 - 255
		volume = static_cast<uint32_t>
			(255 * volFloat32 / channels + 0.5);
	}

	debug("audio_mixer_manager_osx: AudioMixerManagerMac::MicrophoneVolume() => vol=%u", volume);

	return 0;
}

int32_t audio_mixer_manager_osx::max_microphone_volume(uint32_t& max_volume) const {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	max_volume = 255;

	return 0;
}

int32_t audio_mixer_manager_osx::min_microphone_volume(uint32_t& min_volume) const {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	min_volume = 0;

	return 0;
}

int32_t audio_mixer_manager_osx::microphone_volume_step_size(uint16_t& step_size) const {
	if (input_device_id_ == kAudioObjectUnknown){
		warning("audio_mixer_manager_osx: device ID has not been set \n");
		return -1;
	}

	step_size = 1;

	return 0;
}

}

