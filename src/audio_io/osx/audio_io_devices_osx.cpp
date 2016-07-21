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
#include "audio_io_osx.h"
#include <sys/time.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif
    
    
namespace webrtc {
	int32_t audio_io_osx::SetPlayoutDevice(uint16_t index){
		if (play_is_initialized_){
			return -1;
		}
        
		AudioDeviceID playDevices[MaxNumberDevices];
		uint32_t nDevices = get_number_devices(kAudioDevicePropertyScopeOutput,
												playDevices, MaxNumberDevices);
		
		info("audio_io_osx: number of availiable waveform-audio output devices is %u \n", nDevices);
        
		if (index > (nDevices - 1)){
			error("audio_io_osx: device index is out of range [0,%u] \n", (nDevices - 1));
			return -1;
		}
        
		output_device_index_ = index;
		output_device_specified_ = true;
        
		return 0;
	}
    
	int32_t audio_io_osx::SetRecordingDevice(uint16_t index){
		if (rec_is_initialized_){
			return -1;
		}
        
		AudioDeviceID recDevices[MaxNumberDevices];
		uint32_t nDevices = get_number_devices(kAudioDevicePropertyScopeInput,
												recDevices, MaxNumberDevices);
        
		info("audio_io_osx: number of availiable waveform-audio input devices is %u", nDevices);
        
		if (index > (nDevices - 1)){
			error("audio_io_osx: device index is out of range [0,%u] \n", (nDevices - 1));
			return -1;
		}
        
		input_device_index_ = index;
		input_device_specified_ = true;
        
		return 0;
	}
    
	int32_t audio_io_osx::init_speaker(){
		if (is_playing_){
			return -1;
		}
        
		if (init_device(output_device_index_, output_device_id_, false) == -1){
			return -1;
		}
        
		if (input_device_id_ == output_device_id_){
			two_devices_ = false;
		} else{
			two_devices_ = true;
		}
        
		if (mixer_manager_.open_speaker(output_device_id_) == -1){
			return -1;
		}

		return 0;
	}
        
	int32_t audio_io_osx::init_microphone(){
		if (is_recording_){
			return -1;
		}
        
		if (init_device(input_device_index_, input_device_id_, true) == -1){
			return -1;
		}
        
		if (input_device_id_ == output_device_id_){
			two_devices_ = false;
		} else{
			two_devices_ = true;
		}
        
		if (mixer_manager_.open_microphone(input_device_id_) == -1){
			return -1;
		}

		return 0;
	}
    
	int32_t audio_io_osx::get_number_devices(const AudioObjectPropertyScope scope,
											AudioDeviceID scopedDeviceIds[],
											const uint32_t deviceListLength)
	{
		OSStatus err = noErr;
        
		AudioObjectPropertyAddress propertyAddress = {
			kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
			kAudioObjectPropertyElementMaster };
		UInt32 size = 0;
		err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &size);
		if(err != noErr){
			error("audio_io_osx: AudioObjectGetPropertyDataSize returned %d \n", err);
		}
		if (size == 0){
			warning("audio_io_osx: No devices \n");
			return 0;
		}
        
		AudioDeviceID* deviceIds = (AudioDeviceID*) malloc(size);
		UInt32 numberDevices = size / sizeof(AudioDeviceID);
		AudioBufferList* bufferList = NULL;
		UInt32 numberScopedDevices = 0;
        
		UInt32 hardwareProperty = 0;
		if (scope == kAudioDevicePropertyScopeOutput){
			hardwareProperty = kAudioHardwarePropertyDefaultOutputDevice;
		} else{
			hardwareProperty = kAudioHardwarePropertyDefaultInputDevice;
		}
        
		AudioObjectPropertyAddress
		propertyAddressDefault = { hardwareProperty,
									kAudioObjectPropertyScopeGlobal,
									kAudioObjectPropertyElementMaster };
        
		AudioDeviceID usedID;
		UInt32 uintSize = sizeof(UInt32);
		err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddressDefault, 0, NULL, &uintSize, &usedID);
		if(err != noErr){
			error("audio_io_osx: AudioObjectGetPropertyDataSize returned %d \n", err);
		}
		if (usedID != kAudioDeviceUnknown){
			scopedDeviceIds[numberScopedDevices] = usedID;
			numberScopedDevices++;
		} else{
			warning("audio_io_osx: GetNumberDevices(): Default device unknown \n");
		}
        
		// Then list the rest of the devices
		bool listOK = true;
        
		err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &size, deviceIds);
		if(err != noErr){
			error("audio_io_osx: AudioObjectGetPropertyDataSize returned %d \n", err);
			listOK = false;
		} else {
			propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
			propertyAddress.mScope = scope;
			propertyAddress.mElement = 0;
			for (UInt32 i = 0; i < numberDevices; i++) {
				// Check for input channels
				err = AudioObjectGetPropertyDataSize(deviceIds[i], &propertyAddress, 0, NULL, &size);
				if (err == kAudioHardwareBadDeviceError) {
					// This device doesn't actually exist; continue iterating.
					continue;
				} else if (err != noErr){
					listOK = false;
					break;
				}
                
				bufferList = (AudioBufferList*) malloc(size);
				err = AudioObjectGetPropertyData(deviceIds[i], &propertyAddress, 0, NULL, &size, bufferList);
				if (err != noErr){
					listOK = false;
					break;
				}
                
				if (bufferList->mNumberBuffers > 0){
					if (numberScopedDevices >= deviceListLength){
						error("audio_io_osx: Device list is not long enough \n");
						listOK = false;
						break;
					}
                    
					scopedDeviceIds[numberScopedDevices] = deviceIds[i];
					numberScopedDevices++;
				}
                
				free(bufferList);
				bufferList = NULL;
			}  // for
		}
        
		if (!listOK){
			if (deviceIds){
				free(deviceIds);
				deviceIds = NULL;
			}
            
			if (bufferList){
				free(bufferList);
				bufferList = NULL;
			}
            
			return -1;
		}
        
		if (deviceIds){
			free(deviceIds);
			deviceIds = NULL;
		}
        
		return numberScopedDevices;
	}
    
	int32_t audio_io_osx::get_device_name(const AudioObjectPropertyScope scope,
											const uint16_t index,
											char* name)
	{
		OSStatus err = noErr;
		UInt32 len = kAdmMaxDeviceNameSize;
		AudioDeviceID deviceIds[MaxNumberDevices];
        
		int numberDevices = get_number_devices(scope, deviceIds, MaxNumberDevices);
		if (numberDevices < 0){
			return -1;
		} else if (numberDevices == 0){
			error("audio_io_osx: No devices \n");
			return -1;
		}
        
		AudioDeviceID usedID;
        
		// Check if there is a default device
		bool isDefaultDevice = false;
		if (index == 0){
			UInt32 hardwareProperty = 0;
			if (scope == kAudioDevicePropertyScopeOutput){
				hardwareProperty = kAudioHardwarePropertyDefaultOutputDevice;
			} else {
				hardwareProperty = kAudioHardwarePropertyDefaultInputDevice;
			}
			AudioObjectPropertyAddress propertyAddress = { hardwareProperty,
				kAudioObjectPropertyScopeGlobal,
				kAudioObjectPropertyElementMaster };
			UInt32 size = sizeof(UInt32);
			err = AudioObjectGetPropertyData(kAudioObjectSystemObject,
				&propertyAddress, 0, NULL, &size, &usedID);
			if (usedID == kAudioDeviceUnknown){
				warning("audio_io_osx: GetDeviceName(): Default device unknown \n");
			} else {
				isDefaultDevice = true;
			}
		}
        
		AudioObjectPropertyAddress propertyAddress = {
			kAudioDevicePropertyDeviceName, scope, 0 };
        
		if (isDefaultDevice) {
			char devName[len];
            
			err = AudioObjectGetPropertyData(usedID, &propertyAddress, 0, NULL, &len, devName);
            
			sprintf(name, "default (%s)", devName);
		} else {
			if (index < numberDevices) {
				usedID = deviceIds[index];
			} else{
				usedID = index;
			}
			err = AudioObjectGetPropertyData(usedID, &propertyAddress, 0, NULL, &len, name);
		}
        
		return 0;
	}
    
	int32_t audio_io_osx::init_device(const uint16_t userDeviceIndex,
                                        AudioDeviceID& deviceId,
										const bool isInput)
	{
		OSStatus err = noErr;
		UInt32 size = 0;
		AudioObjectPropertyScope deviceScope;
		AudioObjectPropertySelector defaultDeviceSelector;
		AudioDeviceID deviceIds[MaxNumberDevices];
        
		if (isInput) {
			deviceScope = kAudioDevicePropertyScopeInput;
			defaultDeviceSelector = kAudioHardwarePropertyDefaultInputDevice;
		} else {
			deviceScope = kAudioDevicePropertyScopeOutput;
			defaultDeviceSelector = kAudioHardwarePropertyDefaultOutputDevice;
		}
        
		AudioObjectPropertyAddress
		propertyAddress = { defaultDeviceSelector,
			kAudioObjectPropertyScopeGlobal,
			kAudioObjectPropertyElementMaster };
        
		// Get the actual device IDs
		int numberDevices = get_number_devices(deviceScope, deviceIds, MaxNumberDevices);
		if (numberDevices < 0) {
			return -1;
		} else if (numberDevices == 0) {
			error("InitDevice(): No devices \n");
			return -1;
		}
        
		bool isDefaultDevice = false;
		deviceId = kAudioDeviceUnknown;
		if (userDeviceIndex == 0){
			// Try to use default system device
			size = sizeof(AudioDeviceID);
			err = AudioObjectGetPropertyData(kAudioObjectSystemObject,
											&propertyAddress, 0, NULL, &size, &deviceId);
			if(err != noErr){
				error("audio_io_osx: AudioObjectGetPropertyData returned %d \n", err);
			}
            
			if (deviceId == kAudioDeviceUnknown){
				warning("audio_io_osx: No default device exists \n");
			} else{
				isDefaultDevice = true;
			}
		}
        
		if (!isDefaultDevice){
			deviceId = deviceIds[userDeviceIndex];
		}
        
		char devName[128];
		char devManf[128];
		memset(devName, 0, sizeof(devName));
		memset(devManf, 0, sizeof(devManf));
        
		propertyAddress.mSelector = kAudioDevicePropertyDeviceName;
		propertyAddress.mScope = deviceScope;
		propertyAddress.mElement = 0;
		size = sizeof(devName);
		err = AudioObjectGetPropertyData(deviceId,&propertyAddress, 0, NULL, &size, devName);
		if(err != noErr){
			error("audio_io_osx: AudioObjectGetPropertyData returned %d \n", err);
		}
		propertyAddress.mSelector = kAudioDevicePropertyDeviceManufacturer;
		size = sizeof(devManf);
		err = AudioObjectGetPropertyData(deviceId,&propertyAddress, 0, NULL, &size, devManf);
		if(err != noErr){
			error("audio_io_osx: AudioObjectGetPropertyData returned %d \n", err);
		}
		if (isInput) {
			info("audio_io_osx: Input device: %s %s \n", devManf, devName);
		} else {
			info("audio_io_osx: Output device: %s %s \n", devManf, devName);
		}
        
		return 0;
	}

	int16_t audio_io_osx::RecordingDevices(){
		AudioDeviceID recDevices[MaxNumberDevices];
		return get_number_devices(kAudioDevicePropertyScopeInput, recDevices,
									MaxNumberDevices);
	}
    
	int32_t audio_io_osx::RecordingDeviceName(uint16_t index,
												char name[kAdmMaxDeviceNameSize],
												char guid[kAdmMaxGuidSize]){
		const uint16_t nDevices(RecordingDevices());
        
		if ((index > (nDevices - 1)) || (name == NULL))	{
			return -1;
		}
		memset(name, 0, kAdmMaxDeviceNameSize);
		if (guid != NULL){
			memset(guid, 0, kAdmMaxGuidSize);
		}
		return get_device_name(kAudioDevicePropertyScopeInput, index, name);
	}
    
	int16_t audio_io_osx::PlayoutDevices(){
		AudioDeviceID playDevices[MaxNumberDevices];
		return get_number_devices(kAudioDevicePropertyScopeOutput, playDevices,
									MaxNumberDevices);
	}
    
	int32_t audio_io_osx::PlayoutDeviceName(uint16_t index,
												char name[kAdmMaxDeviceNameSize],
												char guid[kAdmMaxGuidSize])
	{
		const uint16_t nDevices(PlayoutDevices());
        
		if ((index > (nDevices - 1)) || (name == NULL)){
			return -1;
		}
		memset(name, 0, kAdmMaxDeviceNameSize);
		if (guid != NULL){
			memset(guid, 0, kAdmMaxGuidSize);
		}
        
		return get_device_name(kAudioDevicePropertyScopeOutput, index, name);
	}
    
	int32_t audio_io_osx::handle_default_device_change() {
    	pthread_mutex_lock(&dev_ch_mutex_); // Callbacks can come from different threads at the same time so this is needed
    
		int32_t ret = 0;
    
		bool is_recording = is_recording_;
		bool is_playing = is_playing_;
    
		info("audio_io_osx::handle_default_device_change() \n");
		if( is_recording ){
			if ( StopRecording() != 0 ) {
				error("audio_io_osx: StopRecording failed! \n");
				ret = -1;
				goto out;
			}
		}
		if( is_playing ){
			if( StopPlayout() != 0 ){
				error("audio_io_osx: StopPlayout failed!");
				ret = -1;
				goto out;
			}
		}
    
		if ( SetRecordingDevice(0) != 0 ) {
			error("audio_io_osx: SetRecordingDevice failed!");
			ret = -1;
			goto out;
		}
    
		if ( init_microphone() != 0 ) {
			error("audio_io_osx: InitMicrophone failed!");
			ret = -1;
			goto out;
		}
    
		if( SetPlayoutDevice(0) != 0 ){
			error("audio_io_osx: SetPlayoutDevice failed!n");
			ret = -1;
			goto out;
		}
    
		if( init_speaker() != 0 ){
			error("audio_io_osx: InitSpeaker failed!");
			ret = -1;
			goto out;
		}
    
		if( is_recording ){
			if( InitRecording() != 0 ) {
				error("audio_io_osx: InitRecording failed!");
				ret = -1;
				goto out;
			}
			if( StartRecording() != 0 ) {
				error("audio_io_osx: StartRecording failed!");
				ret = -1;
				goto out;
        	}
		}
    
		if( is_playing ){
			if( InitPlayout() != 0 ){
				error("audio_io_osx: InitPlayout failed!");
				ret = -1;
				goto out;
			}
			if( StartPlayout() != 0 ){
				error("audio_io_osx: StartPlayout failed!");
				ret = -1;
				goto out;
			}
		}
	out:
    	pthread_mutex_unlock(&dev_ch_mutex_);
    	return ret;
	}
}
