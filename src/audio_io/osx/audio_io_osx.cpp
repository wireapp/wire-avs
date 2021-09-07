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
	static void *rec_thread(void *arg){
		return static_cast<audio_io_osx*>(arg)->record_thread();
	}
    
	audio_io_osx::audio_io_osx()
	:
	mixer_manager_(),
	input_device_id_(kAudioObjectUnknown),
	output_device_id_(kAudioObjectUnknown),
	au_play_(NULL),
	au_rec_(NULL),
	is_recording_(false),
	is_playing_(false),
	initialized_(false),
	is_shut_down_(false),
	rec_is_initialized_(false),
	play_is_initialized_(false),
	agc_(false),
	rec_fs_hz_(0),
	play_fs_hz_(0),
	rec_delay_(0),
	play_delay_(0),
	play_delay_measurement_counter_(9999),
	rec_delay_HWandOS_(0),
	rec_delay_measurement_counter_(9999),
	play_delay_warning_(0),
	rec_delay_warning_(0),
	play_buffer_used_(0),
	rec_current_seq_(0),
	rec_buffer_total_size_(0),
	rec_latency_ms_(0),
	play_latency_ms_(0),
	prev_rec_latency_ms_(0),
	prev_play_latency_ms_(0),
	calls_since_reset_(0) {
		audioCallback_ = NULL;
		is_recording_ = false;
		is_playing_ = false;
		rec_tid_ = 0;
                
		pthread_mutex_init(&mutex_, NULL);
		pthread_mutex_init(&dev_ch_mutex_, NULL);
        
		pthread_mutex_init(&cond_mutex_,NULL);
		pthread_cond_init(&cond_, NULL);
		is_running_ = false;
	}

	audio_io_osx::~audio_io_osx() {
		Terminate();

		pthread_mutex_destroy(&mutex_);
		pthread_mutex_destroy(&dev_ch_mutex_);
		pthread_mutex_destroy(&cond_mutex_);
		pthread_cond_destroy(&cond_);
	}
    
	int32_t audio_io_osx::RegisterAudioCallback(AudioTransport* audioCallback) {
		bool is_playing = is_playing_;
		bool is_recording = is_recording_;
		StopPlayout();
		StopRecording(); // Stop the threads that uses audioCallback
		audioCallback_ = audioCallback;
		if(is_playing)
			StartPlayout();
		if(is_recording)
			StartRecording();
		return 0;
	}

    int32_t audio_io_osx::Init() {
        int32_t ret = 0;
        
        pthread_mutex_lock(&mutex_);
        
        if (initialized_) {
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        
        is_shut_down_ = false;
        
        // Create and start capture thread
        if (!rec_tid_) {
            pthread_create(&rec_tid_, NULL, rec_thread, this);
            
            is_running_ = true;
                        
            pthread_cond_init(&cond_, NULL);
            
            int max_prio = sched_get_priority_max(SCHED_RR);
            int min_prio = sched_get_priority_min(SCHED_RR);
            if (max_prio - min_prio <= 2){
                max_prio = 0;
            }
            if(max_prio > 0){
                sched_param param;
                param.sched_priority = max_prio;
                info("audio_io_ios: Setting thread prio to %d \n", max_prio);
                int ret = pthread_setschedparam(rec_tid_, SCHED_RR, &param);
                if(ret != 0){
                    error("audio_io_ios: Failed to set thread priority \n");
                }
            }
        } else {
            warning("Thread already created \n");
        }
        
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyRunLoop,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster };
        CFRunLoopRef runLoop = NULL;
        OSStatus err = noErr;
        UInt32 size = sizeof(CFRunLoopRef);
        err = AudioObjectSetPropertyData(kAudioObjectSystemObject,
                                                           &propertyAddress, 0, NULL, size, &runLoop);
        if(err != noErr){
            error("AudioObjectSetPropertyData returned %d \n", err);
            ret= -1;
            goto out;
        }
        
        // Listen for any device changes.
        propertyAddress.mSelector = kAudioHardwarePropertyDevices;
        err = AudioObjectAddPropertyListener(kAudioObjectSystemObject,
                                                         &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectAddPropertyListener returned %d \n", err);
            ret= -1;
            goto out;
        }
        
        propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        err = AudioObjectAddPropertyListener(kAudioObjectSystemObject,
                                                         &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectAddPropertyListener returned %d \n", err);
            ret= -1;
            goto out;
        }
        
        propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
        err = AudioObjectAddPropertyListener(kAudioObjectSystemObject,
                                                         &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectAddPropertyListener returned %d \n", err);
            ret= -1;
            goto out;
        }
        initialized_ = true;

        init_microphone();
        init_speaker();
        
    out:
        pthread_mutex_unlock(&mutex_);
        return 0;
    }
    
    int32_t audio_io_osx::Terminate() {
        if (!initialized_) {
            return 0;
        }

        // Stop record thread
        if (rec_tid_){
            void* thread_ret;
            
            is_running_ = false;
            
            pthread_cond_signal(&cond_);
            
            pthread_join(rec_tid_, &thread_ret);
            rec_tid_ = 0;
            
            pthread_cond_destroy(&cond_);
        }
        
        mixer_manager_.close();
        
        OSStatus err = noErr;
        
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster };
        err = AudioObjectRemovePropertyListener(kAudioObjectSystemObject,
                                                             &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectRemovePropertyListener returned %d \n", err);
            return -1;
        }
        
        propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        err = AudioObjectRemovePropertyListener(kAudioObjectSystemObject,
                                                             &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectRemovePropertyListener returned %d \n", err);
            return -1;
        }
        
        propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
        err = AudioObjectRemovePropertyListener(kAudioObjectSystemObject,
                                                             &propertyAddress, &object_listener_proc, this);
        if(err != noErr){
            error("AudioObjectRemovePropertyListener returned %d \n", err);
            return -1;
        }
        
        is_shut_down_ = true;
        initialized_ = false;
    
        StopRecording();
        StopPlayout();
                
        return 0;
    }
    
	int32_t audio_io_osx::InitPlayout() {
        int32_t ret = 0;
        
        pthread_mutex_lock(&mutex_);
        
        if (!initialized_) {
            error("audio_io_osx: Not initialized \n");
            ret = -1;
            goto out;
        }
        
        if (is_playing_) {
            error("audio_io_osx: Playout already started \n");
            ret = -1;
            goto out;
        }
        
        if (play_is_initialized_) {
            info("audio_io_osx: Playout already started \n");
            ret = 0;
            goto out;
        }
        
        if (!output_device_specified_) {
            error("audio_io_osx: Playout device is not specified \n");
            ret = -1;
            goto out;
        }
        
        // Initialize the speaker
        if (init_speaker() == -1) {
            warning("audio_io_osx: init_speaker() failed \n");
        }
        
        play_is_initialized_ = true;
        
        if (!rec_is_initialized_) {
            // Audio init
            if (init_audio_unit() == -1) {
                warning("audio_io_osx: InitPlayOrRecord() failed \n");
            }
        } else {
                info("audio_io_osx: Recording already initialized - init_audio_unit() not called \n");
        }
        
    out:
        pthread_mutex_unlock(&mutex_);
        return 0;
	}
    
	bool audio_io_osx::PlayoutIsInitialized() const {
		return play_is_initialized_;
	}
    
	int32_t audio_io_osx::InitRecording() {
        pthread_mutex_lock(&mutex_);
        
        if (!initialized_) {
            error("audio_io_osx: Not initialized \n");
            pthread_mutex_unlock(&mutex_);
            return -1;
        }
        
        if (is_recording_) {
            warning("audio_io_osx: Recording already started \n");
            pthread_mutex_unlock(&mutex_);
            return -1;
        }
        
        if (rec_is_initialized_) {
            error("audio_io_osx: Recording already initialized \n");
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        
        if (!input_device_specified_) {
            error("audio_io_osx: Recording device is not specified \n");
            pthread_mutex_unlock(&mutex_);
            return -1;
        }
        
        // Initialize the microphone
        if (init_microphone() == -1) {
            warning("audio_io_osx: init_microphone() failed \n");
        }
        
        rec_is_initialized_ = true;
        
        if (!play_is_initialized_) {
            // Audio init
            if (init_audio_unit() == -1) {
                warning("audio_io_osx: InitPlayOrRecord() failed \n");
            }
        } else {
            info("audio_device_osx: Playout already initialized - init_audio_unit() " \
                         "not called \n");
        }
        
        pthread_mutex_unlock(&mutex_);
        return 0;
	}
    
	bool audio_io_osx::RecordingIsInitialized() const {
		return true;
	}
    
	int32_t audio_io_osx::StartPlayout() {
        pthread_mutex_lock(&mutex_);
        
        if (!play_is_initialized_) {
            error("audio_io_osx: Playout not initialized \n");
            pthread_mutex_unlock(&mutex_);
            return -1;
        }
        
        if (is_playing_) {
            info("audio_io_osx: Playing already started \n");
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        
        // Reset playout buffer
        memset(play_buffer_, 0, sizeof(play_buffer_));
        play_buffer_used_ = 0;
        play_delay_ = 0;
        play_delay_measurement_counter_ = 9999;
        
        if (!is_recording_) {
            // Start Audio Unit
            debug("audio_io_osx: Starting Audio Unit \n");
            OSStatus result = AudioOutputUnitStart(au_rec_);
            if (0 != result) {
                error("audio_io_osx: Error starting Audio Unit (result=%d) \n", result);
                pthread_mutex_unlock(&mutex_);
                return -1;
            }
            if(au_play_){
                result = AudioOutputUnitStart(au_play_);
                if (0 != result) {
                    error("audio_io_osx: Error starting Audio Unit (result=%d) \n", result);
                    pthread_mutex_unlock(&mutex_);
                    return -1;
                }
            }
        }
        
		is_playing_ = true;
        pthread_mutex_unlock(&mutex_);
		return 0;
    }
    
	bool audio_io_osx::Playing() const {
		return is_playing_;
	}
    
	int32_t audio_io_osx::StartRecording() {
        int32_t ret = 0;
        
        pthread_mutex_unlock(&mutex_);
        
        if (!rec_is_initialized_) {
            error("audio_io_osx: Recording not initialized \n");
            ret = -1;
            goto out;
        }
        
        if (is_recording_) {
            info("audio_io_osx: Recording already started \n");
            ret = 0;
            goto out;
        }
        
        // Reset recording buffer
        memset(rec_buffer_, 0, sizeof(rec_buffer_));
        memset(rec_length_, 0, sizeof(rec_length_));
        memset(rec_seq_, 0, sizeof(rec_seq_));
        rec_current_seq_ = 0;
        rec_buffer_total_size_ = 0;
        rec_delay_ = 0;
        rec_delay_HWandOS_ = 0;
        rec_delay_measurement_counter_ = 9999;
        
        if (!is_playing_) {
            // Start Audio Unit
            debug("audio_io_osx: Starting Audio Unit \n");
            OSStatus result = AudioOutputUnitStart(au_rec_);
            if (0 != result) {
                error("audio_io_osx: Error starting Audio Unit (result=%d) \n", result);
                ret = -1;
                goto out;
            }
            if(au_play_){
                result = AudioOutputUnitStart(au_play_);
                if (0 != result) {
                    error("audio_io_osx: Error starting Audio Unit (result=%d) \n", result);
                    ret = -1;
                    goto out;
                }
            }
        }
        
		is_recording_ = true;
    out:
        pthread_mutex_unlock(&mutex_);
		return 0;
    }
    
	bool audio_io_osx::Recording() const {
		return is_recording_;
	}
    
	int32_t audio_io_osx::StopRecording() {
        pthread_mutex_lock(&mutex_);
        
        if (!rec_is_initialized_) {
            info("audio_io_osx: Recording is not initialized \n");
            goto out;
        }
        
        is_recording_ = false;
        
        if (!is_playing_) {
            // Both playout and recording has stopped, shutdown the device
            shutdown_audio_unit();
        }
        
        rec_is_initialized_ = false;

    out:
        pthread_mutex_unlock(&mutex_);
		return 0;
	}
    
	int32_t audio_io_osx::StopPlayout() {
        pthread_mutex_lock(&mutex_);
        
        if (!play_is_initialized_) {
            info("audio_io_osx: Recording is not initialized \n");
            goto out;
        }
        
        is_playing_ = false;
        
        if (!is_recording_) {
            // Both playout and recording has stopped, shutdown the device
            shutdown_audio_unit();
        }
        
        play_is_initialized_ = false;
        
    out:
        pthread_mutex_unlock(&mutex_);
        return 0;
	}
    
	void* audio_io_osx::record_thread(){
		int16_t audio_buf[FRAME_LEN] = {0};
		uint32_t currentMicLevel = 10;
		uint32_t newMicLevel = 0;
        
		while(1){
            if(!is_running_){
                break;
            }
            
            if(is_running_){
                pthread_mutex_lock(&cond_mutex_);
                pthread_cond_wait(&cond_, &cond_mutex_);
                pthread_mutex_unlock(&cond_mutex_);
            }
                        
            int bufPos = 0;
            unsigned int lowestSeq = 0;
            int lowestSeqBufPos = 0;
            bool foundBuf = true;
            const unsigned int noSamp10ms = rec_fs_hz_ / 100;
                
			if((rec_delay_warning_ > 0 || play_delay_warning_ > 0) && calls_since_reset_ > MIN_CALLS_BETWEEN_RESETS){
				if(rec_delay_warning_ > 0 ){
					error("audio_io_osx: Record Delay Warning reset Audio Device !! \n");
				} else {
					error("audio_io_osx: Playout Delay Warning reset Audio Device !! \n");
				}
				ResetAudioDevice();
                rec_delay_warning_ = 0;
                play_delay_warning_ = 0;
                calls_since_reset_ = 0;
            }
            calls_since_reset_++;
                
            while (foundBuf) {
                foundBuf = false;
                for (bufPos = 0; bufPos < REC_BUFFERS; ++bufPos) {
                    if (noSamp10ms == rec_length_[bufPos]) {
                        if (!foundBuf) {
                            lowestSeq = rec_seq_[bufPos];
                            lowestSeqBufPos = bufPos;
                            foundBuf = true;
                        } else if (rec_seq_[bufPos] < lowestSeq) {
                            lowestSeq = rec_seq_[bufPos];
                            lowestSeqBufPos = bufPos;
                        }
                    }
                }
                    
                // Insert data into the Audio Device Buffer if found any
                if (foundBuf) {
                    uint32_t currentMicLevel(0);
                    uint32_t newMicLevel(0);
                        
                    // Update recording delay
                    update_rec_delay();

                    if (AGC()){
                        mixer_manager_.microphone_volume(currentMicLevel);
                    }
                    
                    if(audioCallback_){
                        int32_t ret = audioCallback_->RecordedDataIsAvailable((void*)rec_buffer_[lowestSeqBufPos],
                                                                              rec_length_[lowestSeqBufPos], 2, 1, rec_fs_hz_,
                                                                              play_delay_ + rec_delay_, 0,
                                                                              currentMicLevel, false, newMicLevel);
                    }
                    
                    if (AGC()){
                        if (newMicLevel != 0){
                            debug("audio_io_osx: AGC change of volume: old=%u => new=%u \n", currentMicLevel, newMicLevel);
                            if (mixer_manager_.set_microphone_volume(newMicLevel) == -1) {
                                warning("audio_io_osx: the required modification of the microphone volume failed \n");
                            }
                        }
                    }
                    rec_seq_[lowestSeqBufPos] = 0;
                    rec_buffer_total_size_ -= rec_length_[lowestSeqBufPos];
                    rec_length_[lowestSeqBufPos] = 0;
                }
            }
		}
		return NULL;
	}
    
    int32_t audio_io_osx::init_audio_unit() {
        
        init_rec_audio_unit();
        
        init_play_audio_unit();
        
        return 0;
    }
    
    int32_t audio_io_osx::shutdown_audio_unit() {
        // Close and delete AU
        OSStatus result = -1;
        if (NULL != au_rec_) {
            result = AudioOutputUnitStop(au_rec_);
            if (0 != result) {
                error("audio_io_osx: Error stopping Audio Unit (result=%d) \n", result);
            }
            result = AudioComponentInstanceDispose(au_rec_);
            if (0 != result) {
                error("audio_io_osx: Error disposing Audio Unit (result=%d) \n", result);
            }
            au_rec_ = NULL;
        }
        if (NULL != au_play_) {
            result = AudioOutputUnitStop(au_play_);
            if (0 != result) {
                error("audio_io_osx: Error stopping Audio Unit (result=%d) \n", result);
            }
            result = AudioComponentInstanceDispose(au_play_);
            if (0 != result) {
                error("audio_io_osx: Error disposing Audio Unit (result=%d) \n", result);
            }
            au_play_ = NULL;
        }

        return 0;
    }
    
    int32_t audio_io_osx::init_rec_audio_unit() {
        OSStatus result = -1;
        
        // Check if already initialized
        if (NULL != au_rec_) {
            info("audio_io_osx: Already initialized \n");
            return 0;
        }
        
        if( input_device_id_ == kAudioObjectUnknown){
            error("audio_io_osx: invalid _inputDeviceID \n");
        }
        
        Float64 nominal_sample_rate;
        UInt32 info_size = sizeof(nominal_sample_rate);
        
        static const AudioObjectPropertyAddress kNominalSampleRateAddress = {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        result = AudioObjectGetPropertyData(input_device_id_,
                                            &kNominalSampleRateAddress,
                                            0,
                                            0,
                                            &info_size,
                                            &nominal_sample_rate);
        if (result != noErr) {
            error("audio_io_osx: Failed to get device nominal sample rate for record AudioUnit \n");
        }
        
        // Start by obtaining an AudioOuputUnit using an AUHAL component description.
        AudioComponent comp;
        AudioComponentDescription desc;
        
        // Description for the Audio Unit we want to use (AUHAL in this case).
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_HALOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        desc.componentFlags = 0;
        desc.componentFlagsMask = 0;
        comp = AudioComponentFindNext(0, &desc);
        
        // Get access to the service provided by the specified Audio Unit.
        result = AudioComponentInstanceNew(comp, &au_rec_);
        if (result) {
            error("audio_io_osx: Cannot open record AudioUnit \n");
        }
        
        UInt32 enableIO = 1;
        
        // Enable input on the AUHAL.
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input,
                                      1,          // input element 1
                                      &enableIO,  // enable
                                      sizeof(enableIO));
        if (result) {
            error("audio_io_osx: Cannot enable input for record AudioUnit \n");
        }
        
        // Disable output on the AUHAL.
        enableIO = 0;
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,          // output element 0
                                      &enableIO,  // disable
                                      sizeof(enableIO));
        if (result) {
            error("audio_io_osx: Cannot disable output for record AudioUnit \n");
        }
        
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global,
                                      0,
                                      &input_device_id_,
                                      sizeof(input_device_id_));
        if (result) {
            error("audio_io_osx: Cannot set current device for record AudioUnit \n");
        }
        
        AURenderCallbackStruct callback;
        callback.inputProc = rec_process;
        callback.inputProcRefCon = this;
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global,
                                      0,
                                      &callback,
                                      sizeof(callback));
        if (result) {
            error("audio_io_osx: Cannot set input callback for record AudioUnit \n");
        }
        
        AudioStreamBasicDescription format_;
        UInt32 size = sizeof(format_);
        result = AudioUnitGetProperty(au_rec_,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &format_,
                                      &size);
        
        format_.mSampleRate = (int)nominal_sample_rate;
        format_.mFormatID = kAudioFormatLinearPCM;
        format_.mFormatFlags    = kLinearPCMFormatFlagIsPacked |
        kLinearPCMFormatFlagIsSignedInteger;
        format_.mBitsPerChannel = sizeof(SInt16) * 8;
        format_.mChannelsPerFrame = 1;
        format_.mFramesPerPacket = 1;  // uncompressed audio
        format_.mBytesPerPacket = (format_.mBitsPerChannel *
                                   format_.mChannelsPerFrame) / 8;
        format_.mBytesPerFrame = format_.mBytesPerPacket;
        format_.mReserved = 0;
        
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output,
                                      1,
                                      &format_,
                                      sizeof(format_));
        if (result) {
            error("audio_io_osx: Cannot set stream properties for record AudioUnit \n");
        }
        
        rec_fs_hz_ = format_.mSampleRate;
        
        UInt32 buffer_size = 0;
        UInt32 property_size = sizeof(buffer_size);
        result = AudioUnitGetProperty(au_rec_,
                                      kAudioDevicePropertyBufferFrameSize,
                                      kAudioUnitScope_Output,
                                      1,
                                      &buffer_size,
                                      &property_size);
        if (result != noErr) {
            error("audio_io_osx: Cannot get callback buffer size for record AudioUnit \n");
        }
        
        buffer_size = rec_fs_hz_/100;
        result = AudioUnitSetProperty(au_rec_,
                                      kAudioDevicePropertyBufferFrameSize,
                                      kAudioUnitScope_Output,
                                      1,
                                      &buffer_size,
                                      sizeof(buffer_size));
        if (result != noErr) {
            error("audio_io_osx: Cannot set callback buffer size for record AudioUnit \n");
        }
        
        result = AudioUnitInitialize(au_rec_);
        if (result) {
            error("audio_io_osx: Cannot initialize record AudioUnit \n");
        }
        
        return 0;
    }
    
    int32_t audio_io_osx::init_play_audio_unit() {
        OSStatus result = -1;
        
        AudioUnit auTmp = au_rec_;
        
        info("audio_io_osx: InitAuPlay two_devices_ = %d \n", two_devices_);
        
        // Check if already initialized
        if (NULL != au_play_) {
            info("audio_io_osx: Already initialized \n");
            return 0;
        }
        
        if( output_device_id_ == kAudioObjectUnknown){
            error("audio_io_osx: invalid _outputDeviceID \n");
        }
        
        Float64 nominal_sample_rate;
        UInt32 info_size = sizeof(nominal_sample_rate);
        
        static const AudioObjectPropertyAddress kNominalSampleRateAddress = {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        result = AudioObjectGetPropertyData(output_device_id_,
                                            &kNominalSampleRateAddress,
                                            0,
                                            0,
                                            &info_size,
                                            &nominal_sample_rate);
        if (result != noErr) {
            error("audio_io_osx: Failed to get device nominal sample rate for playback AudioUnit \n");
        }
        
        // Start by obtaining an AudioOuputUnit using an AUHAL component description.
        if(two_devices_){ // Two devices needs two Audio Units
            AudioComponent comp;
            AudioComponentDescription desc;
            
            // Description for the Audio Unit we want to use (AUHAL in this case).
            desc.componentType = kAudioUnitType_Output;
            desc.componentSubType = kAudioUnitSubType_HALOutput;
            desc.componentManufacturer = kAudioUnitManufacturer_Apple;
            desc.componentFlags = 0;
            desc.componentFlagsMask = 0;
            comp = AudioComponentFindNext(0, &desc);
            
            // Get access to the service provided by the specified Audio Unit.
            result = AudioComponentInstanceNew(comp, &au_play_);
            if (result) {
                error("audio_io_osx: Cannot open playback AudioUnit \n");
            }
            auTmp = au_play_;
        }
        
        UInt32 enableIO = 0;
        if( two_devices_){
            // Disable input on the AUHAL.
            result = AudioUnitSetProperty(auTmp,
                                          kAudioOutputUnitProperty_EnableIO,
                                          kAudioUnitScope_Input,
                                          1,          // input element 1
                                          &enableIO,  // enable
                                          sizeof(enableIO));
            if (result) {
                error("audio_io_osx: Cannot disable input for playback AudioUnit \n");
            }
        }
        
        // Enable output on the AUHAL.
        enableIO = 1;
        result = AudioUnitSetProperty(auTmp,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,          // output element 0
                                      &enableIO,  // disable
                                      sizeof(enableIO));
        if (result) {
            error("audio_io_osx: Cannot enable output for playback AudioUnit \n");
        }
        
        result = AudioUnitSetProperty(auTmp,
                                      kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global,
                                      0,
                                      &output_device_id_,
                                      sizeof(output_device_id_));
        if (result) {
            error("audio_io_osx: Cannot set audio device for playback AudioUnit \n");
        }
        
        AURenderCallbackStruct callback;
        callback.inputProc = play_process;
        callback.inputProcRefCon = this;
        result = AudioUnitSetProperty(auTmp,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Global,
                                      0,
                                      &callback,
                                      sizeof(callback));
        if (result) {
            error("audio_io_osx: Cannot set callback for playback AudioUnit \n");
        }
        
        AudioStreamBasicDescription format_;
        UInt32 size = sizeof(format_);
        result = AudioUnitGetProperty(auTmp,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &format_,
                                      &size);
        if (result) {
            error("audio_io_osx: Cannot get stream properties for playback AudioUnit \n");
        }
        
        format_.mSampleRate = (int)nominal_sample_rate;
        format_.mFormatID = kAudioFormatLinearPCM;
        format_.mFormatFlags = kLinearPCMFormatFlagIsPacked |
        kLinearPCMFormatFlagIsSignedInteger;
        format_.mBitsPerChannel = sizeof(SInt16) * 8;
        format_.mFramesPerPacket = 1;  // uncompressed audio
        format_.mBytesPerPacket = (format_.mBitsPerChannel *
                                   format_.mChannelsPerFrame) / 8;
        format_.mBytesPerFrame = format_.mBytesPerPacket;
        format_.mReserved = 0;
        
        result = AudioUnitSetProperty(auTmp,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0,
                                      &format_,
                                      sizeof(format_));
        if (result) {
            error("audio_io_osx: Cannot set stream properties for playback AudioUnit \n");
        }
        
        play_fs_hz_ = format_.mSampleRate;
        
        UInt32 buffer_size = 0;
        UInt32 property_size = sizeof(buffer_size);
        result = AudioUnitGetProperty(auTmp,
                                      kAudioDevicePropertyBufferFrameSize,
                                      kAudioUnitScope_Output,
                                      1,
                                      &buffer_size,
                                      &property_size);
        if (result != noErr) {
            error("audio_io_osx: Cannot get callback buffer size for playback AudioUnit \n");
        }
        
        buffer_size = play_fs_hz_/100;
        result = AudioUnitSetProperty(auTmp,
                                      kAudioDevicePropertyBufferFrameSize,
                                      kAudioUnitScope_Output,
                                      1,
                                      &buffer_size,
                                      sizeof(buffer_size));
        if (result != noErr) {
            error("audio_io_osx: Cannot set callback buffer size for playback AudioUnit \n");
        }
        
        if(two_devices_){
            result = AudioUnitInitialize(au_play_);
            if (result) {
                error("audio_io_osx: Cannot initialize playback AudioUnit \n");
            }
        }
        
        return 0;
    }
    
    int32_t audio_io_osx::SetAGC(bool enable) {
        debug("audio_io_osx::SetAGC(enable=%d)", enable);
        
        agc_ = enable;
        
        return 0;
    }
    
    bool audio_io_osx::AGC() const {
        return agc_;
    }
    
    OSStatus audio_io_osx::object_listener_proc(
                                                     AudioObjectID objectId,
                                                     UInt32 numberAddresses,
                                                     const AudioObjectPropertyAddress addresses[],
                                                     void* clientData)
    {
        audio_io_osx *ptrThis = (audio_io_osx *) clientData;
        assert(ptrThis != NULL);
        
        ptrThis->impl_object_listener_proc(objectId, numberAddresses, addresses);
        
        return 0;
    }
    
    // ToDo Move to media manager
    OSStatus audio_io_osx::impl_object_listener_proc(
                                                         const AudioObjectID objectId,
                                                         const UInt32 numberAddresses,
                                                         const AudioObjectPropertyAddress addresses[])
    {
        debug("audio_io_osx:impl_object_listener_proc \n");
        
        for (UInt32 i = 0; i < numberAddresses; i++) {
            if (addresses[i].mSelector == kAudioHardwarePropertyDevices) {
                //HandleDeviceChange();
            } else if (addresses[i].mSelector == kAudioHardwarePropertyDefaultOutputDevice) {
                handle_default_device_change();
            } else if (addresses[i].mSelector == kAudioHardwarePropertyDefaultInputDevice) {
                handle_default_device_change();
            }else if (addresses[i].mSelector == kAudioDevicePropertyStreamFormat) {
                info("audio_io_osx:: stream format changed \n");
            } else if (addresses[i].mSelector == kAudioDevicePropertyDataSource) {
                info("audio_io_osx:: data source changed \n");
            } else if (addresses[i].mSelector == kAudioDeviceProcessorOverload) {
                warning("audio_io_osx:: processsor is overloaded \n");
            }
        }
        
        return 0;
    }

    uint32_t audio_io_osx::get_rec_latency(const AudioTimeStamp* input_time_stamp) {
        UInt64 capture_time_ns = AudioConvertHostTimeToNanos(
                                                             input_time_stamp->mHostTime);
        UInt64 now_ns = AudioConvertHostTimeToNanos(AudioGetCurrentHostTime());
        
        if (capture_time_ns > now_ns){
            return 0;
        }
        
        double delay_ms = static_cast<double>
        (1e-6 * (now_ns - capture_time_ns));
        
        return (uint32_t)delay_ms; // Round here AEC is working with integer ms delay anyways
    }
    
    uint32_t audio_io_osx::get_play_latency(const AudioTimeStamp* output_time_stamp) {
        if ((output_time_stamp->mFlags & kAudioTimeStampHostTimeValid) == 0)
            return 0;
        
        UInt64 output_time_ns = AudioConvertHostTimeToNanos(
                                                            output_time_stamp->mHostTime);
        UInt64 now_ns = AudioConvertHostTimeToNanos(AudioGetCurrentHostTime());
        
        if (now_ns > output_time_ns){
            return 0;
        }
        
        double delay_ms = static_cast<double>
        (1e-6 * (output_time_ns - now_ns));
        
        return (uint32_t)delay_ms;
    }
    
    OSStatus audio_io_osx::rec_process(void *inRefCon,
                                       AudioUnitRenderActionFlags *ioActionFlags,
                                       const AudioTimeStamp *inTimeStamp,
                                       UInt32 inBusNumber,
                                       UInt32 inNumberFrames,
                                       AudioBufferList *ioData) {
        audio_io_osx* ptrThis = static_cast<audio_io_osx*>(inRefCon);
        
        return ptrThis->rec_process_impl(ioActionFlags,
                                          inTimeStamp,
                                          inBusNumber,
                                          inNumberFrames);
        
        assert(0);
    }
    
    
    OSStatus audio_io_osx::rec_process_impl(
                                           AudioUnitRenderActionFlags *ioActionFlags,
                                           const AudioTimeStamp *inTimeStamp,
                                           uint32_t inBusNumber,
                                           uint32_t inNumberFrames) {
        int16_t dataTmp[inNumberFrames];
        memset(dataTmp, 0, sizeof(int16_t)*inNumberFrames);
        AudioBufferList abList;
        abList.mNumberBuffers = 1;
        abList.mBuffers[0].mData = dataTmp;
        abList.mBuffers[0].mDataByteSize = sizeof(int16_t)*inNumberFrames;  // 2 bytes/sample
        abList.mBuffers[0].mNumberChannels = 1;
        
        // Get data from mic
        OSStatus res = AudioUnitRender(au_rec_,
                                       ioActionFlags, inTimeStamp,
                                       inBusNumber, inNumberFrames, &abList);
        
        if (res != 0) {
            warning("audio_io_osx: Error getting rec data, error = %d \n", res);
            return 0;
        }
        
        uint32_t tmp_rec_latency_ms = get_rec_latency(inTimeStamp);
        
        int32_t diff = (int32_t)tmp_rec_latency_ms - (int32_t)prev_rec_latency_ms_;
        if( diff > DELAY_JUMP_FOR_RESET_MS ){
            /* Sudden Jump in latency - AEC will have problems */
            rec_delay_warning_ = 1;
        }
        prev_rec_latency_ms_ = tmp_rec_latency_ms;
        
        rec_latency_ms_ = tmp_rec_latency_ms;
        
        if (is_recording_) {
            const unsigned int noSamp10ms = rec_fs_hz_ / 100;
            unsigned int dataPos = 0;
            uint16_t bufPos = 0;
            int16_t insertPos = -1;
            unsigned int nCopy = 0;  // Number of samples to copy
            
            while (dataPos < inNumberFrames) {
                // Loop over all recording buffers
                bufPos = 0;
                insertPos = -1;
                nCopy = 0;
                while (bufPos < REC_BUFFERS) {
                    if ((rec_length_[bufPos] > 0)
                        && (rec_length_[bufPos] < noSamp10ms)) {
                        insertPos = static_cast<int16_t>(bufPos);
                        bufPos = REC_BUFFERS;
                    } else if ((-1 == insertPos)
                               && (0 == rec_length_[bufPos])) {
                        insertPos = static_cast<int16_t>(bufPos);
                    }
                    ++bufPos;
                }
                
                // Insert data into buffer
                if (insertPos > -1) {
                    unsigned int dataToCopy = inNumberFrames - dataPos;
                    unsigned int currentRecLen = rec_length_[insertPos];
                    unsigned int roomInBuffer = noSamp10ms - currentRecLen;
                    nCopy = (dataToCopy < roomInBuffer ? dataToCopy : roomInBuffer);
                    
                    memcpy(&rec_buffer_[insertPos][currentRecLen],
                           &dataTmp[dataPos], nCopy*sizeof(int16_t));
                    
                    if (0 == currentRecLen) {
                        rec_seq_[insertPos] = rec_current_seq_;
                        ++rec_current_seq_;
                        
                    }
                    rec_buffer_total_size_ += nCopy;
                    rec_length_[insertPos] += nCopy;
                    dataPos += nCopy;
                } else {
                    error("audio_io_osx: Could not insert into recording buffer. Buffer is full \n");
                    dataPos = inNumberFrames;  // Don't try to insert more
                }
            }
        }
        
        /* wakeup the waiting thread */
        pthread_cond_signal(&cond_);
        
        return 0;
    }
    
    OSStatus audio_io_osx::play_process(void *inRefCon,
                                        AudioUnitRenderActionFlags *ioActionFlags,
                                        const AudioTimeStamp *inTimeStamp,
                                        UInt32 inBusNumber,
                                        UInt32 inNumberFrames,
                                        AudioBufferList *ioData) {
        audio_io_osx* ptrThis = static_cast<audio_io_osx*>(inRefCon);
        
        return ptrThis->play_process_impl(inNumberFrames, inTimeStamp, ioData);
    }
    
    OSStatus audio_io_osx::play_process_impl(uint32_t inNumberFrames,
                                            const AudioTimeStamp *output_time_stamp,
                                            AudioBufferList *ioData) {
        
        int n_channels = 1;
        
        uint32_t tmp_play_latency_ms = get_play_latency(output_time_stamp);
        
        int32_t diff = (int32_t)tmp_play_latency_ms - (int32_t)prev_play_latency_ms_;
        if( diff > DELAY_JUMP_FOR_RESET_MS ){
            /* Sudden Jump in latency - AEC will have problems */
            play_delay_warning_ = 1;
        }
        
        prev_play_latency_ms_ = tmp_play_latency_ms;
        
        play_latency_ms_ = tmp_play_latency_ms;
        
        int16_t* data;
        unsigned int dataSizeBytes;
        if(ioData->mNumberBuffers == 1){
            data = static_cast<int16_t*>(ioData->mBuffers[0].mData); // 0 is left 1 is right channel
            dataSizeBytes = ioData->mBuffers[0].mDataByteSize;
        } else {
            data = static_cast<int16_t*>(ioData->mBuffers[1].mData); // 0 is left 1 is right channel
            dataSizeBytes = ioData->mBuffers[1].mDataByteSize;
        }
        unsigned int dataSize = dataSizeBytes/2;  // Number of samples
        
        if(dataSize == 2*inNumberFrames){
            n_channels = 2;
            dataSize = inNumberFrames;
        }
        
        if (dataSize != inNumberFrames) {  // Should always be the same
            warning("audio_io_osx: dataSize (%u) != inNumberFrames (%u)",
                         dataSize, (unsigned int)inNumberFrames);
        }
        memset(data, 0, dataSizeBytes);  // Start with empty buffer
        
        if (is_playing_) {
            unsigned int noSamp10ms = play_fs_hz_ / 100;
            int16_t dataTmp[noSamp10ms*2];
            unsigned int dataPos = 0;
            uint32_t noSamplesOut = 0;
            unsigned int nCopy = 0;
            
            // First insert data from playout buffer if any
            if (play_buffer_used_ > 0) {
                nCopy = (dataSize < play_buffer_used_) ?
                dataSize : play_buffer_used_;
                if (nCopy != play_buffer_used_) {
                    warning("audio_io_osx: nCopy (%u) != play_buffer_used_ (%u) \n",
                                 nCopy, play_buffer_used_);
                }
                
                // Should never end here as Callback asks for 10 ms which is ACM's frame size
                debug("audio_io_osx: playout not asking for 10 ms ? \n");
                
                memcpy(data, play_buffer_, nCopy * n_channels * sizeof(int16_t));
                dataPos = nCopy;
                memset(play_buffer_, 0, sizeof(play_buffer_));
                play_buffer_used_ = 0;
            }
            
            // Now get the rest from Audio Device Buffer
            while (dataPos < dataSize) {
                // Update playout delay
                update_play_delay();
                
                if(audioCallback_){
                    int64_t elapsed_time_ms, ntp_time_ms;
                    
                    int32_t ret = audioCallback_->NeedMorePlayData(noSamp10ms, 2, n_channels, play_fs_hz_,
                                                                   (void*)dataTmp, noSamplesOut,
                                                                   &elapsed_time_ms, &ntp_time_ms);
                }
                
                // Cast OK since only equality comparison
                if (noSamp10ms != (unsigned int)noSamplesOut) {
                    // Should never happen
                    warning("audio_io_osx: noSamp10ms (%u) != noSamplesOut (%d)",
                                 noSamp10ms, noSamplesOut);
                }
                
                // Insert as much as fits in data buffer
                nCopy = (dataSize-dataPos) > noSamp10ms ?
                noSamp10ms : (dataSize-dataPos);
                
                 memcpy(data, dataTmp, nCopy * n_channels * sizeof(int16_t));

                // Save rest in playout buffer if any
                if (nCopy < noSamp10ms) {
                    memcpy( play_buffer_, &dataTmp[nCopy], sizeof(int16_t)*(noSamp10ms-nCopy));
                    play_buffer_used_ = noSamp10ms - nCopy;
                }
                
                // Update loop/index counter, if we copied less than noSamp10ms
                // samples we shall quit loop anyway
                dataPos += noSamp10ms;
            }
        }
        //_numRenderCalls+=1;
        
        return 0;
    }
    
    void audio_io_osx::update_play_delay() {
        ++play_delay_measurement_counter_;
        
        if (play_delay_measurement_counter_ >= 100) {
            // Update HW and OS delay every second, unlikely to change
            AudioUnit auTmp = au_rec_;
            play_delay_HWandOS_ = 0;
            
            // Get audio unit latency.
            if(au_play_){
                auTmp = au_play_;
            }
            Float64 f64(0);
            UInt32 size = sizeof(f64);
            OSStatus result = AudioUnitGetProperty(
                                                   auTmp,
                                                   kAudioUnitProperty_Latency,
                                                   kAudioUnitScope_Global,
                                                   0,
                                                   &f64,
                                                   &size);
            
            if (0 != result) {
                error("audio_io_osx: error AU latency (result=%d) \n", result);
            }
            
            play_delay_HWandOS_ += static_cast<int>(f64 * 1000000);
            
            // Getoutput audio device latency.
            AudioObjectPropertyAddress property_address = {
                kAudioDevicePropertyLatency,
                kAudioDevicePropertyScopeInput,
                kAudioObjectPropertyElementMaster
            };
            UInt32 device_latency_frames = 0;
            size = sizeof(device_latency_frames);
            result = AudioObjectGetPropertyData(output_device_id_,
                                                &property_address,
                                                0,
                                                NULL,
                                                &size,
                                                &device_latency_frames);
            
            if (0 != result) {
                error("audio_io_osx: error Device latency (result=%d) \n", result);
            }
            
            play_delay_HWandOS_ += static_cast<int>((device_latency_frames * 1000000) / play_fs_hz_);
            
            // To ms
            play_delay_HWandOS_ = (uint32_t)(((int32_t)play_delay_HWandOS_ - 500) / 1000); // Below 0.5 ms this will wrap around because of using unsigned
            
            // Reset counter
            play_delay_measurement_counter_ = 0;
        }
        
        uint32_t tmp_play_latency_ms;
        {
            
            tmp_play_latency_ms = play_latency_ms_;
        }
        
        play_delay_ = play_delay_HWandOS_ + tmp_play_latency_ms;
    }
    
    void audio_io_osx::update_rec_delay() {
        ++rec_delay_measurement_counter_;
        
        if (rec_delay_measurement_counter_ >= 100) {
            rec_delay_HWandOS_ = 0;
            
            // Get audio unit latency.
            Float64 f64(0);
            UInt32 size = sizeof(f64);
            OSStatus result = AudioUnitGetProperty(au_rec_,
                                                   kAudioUnitProperty_Latency,
                                                   kAudioUnitScope_Global,
                                                   0,
                                                   &f64,
                                                   &size);
            
            if (0 != result) {
                error("audio_io_osx: error AU latency (result=%d) \n", result);
            }
            
            rec_delay_HWandOS_ += static_cast<int>(f64 * 1000000);
            
            // Get input audio device latency.
            AudioObjectPropertyAddress property_address = {
                kAudioDevicePropertyLatency,
                kAudioDevicePropertyScopeInput,
                kAudioObjectPropertyElementMaster
            };
            UInt32 device_latency_frames = 0;
            size = sizeof(device_latency_frames);
            result = AudioObjectGetPropertyData(input_device_id_,
                                                &property_address,
                                                0,
                                                NULL,
                                                &size,
                                                &device_latency_frames);
            
            if (0 != result) {
                error("audio_io_osx: error Device latency (result=%d) \n", result);
            }
            
            if(!rec_fs_hz_){
                printf("rec_fs_hz_ = %d \n" , rec_fs_hz_);
            }
                
            rec_delay_HWandOS_ += static_cast<int>((device_latency_frames * 1000000) / rec_fs_hz_);
            
            // To ms
            rec_delay_HWandOS_ = (uint32_t)(((int32_t)rec_delay_HWandOS_ - 500) / 1000); // Below 0.5 ms this will wrap around because of using unsigned
            
            // Reset counter
            rec_delay_measurement_counter_ = 0;
        }
        
        uint32_t tmp_rec_latency_ms;
        {
            tmp_rec_latency_ms = rec_latency_ms_;
        }
        
        rec_delay_ = rec_delay_HWandOS_ + tmp_rec_latency_ms;
        
        // Don't count the one next 10 ms to be sent, then convert samples => ms
        const uint32_t noSamp10ms = rec_fs_hz_ / 100;
        if (rec_buffer_total_size_ > noSamp10ms) {
            debug("audio_io_osx: error audio device mac AUHAL has buffered %d samples ", rec_buffer_total_size_ - noSamp10ms);
            rec_delay_ += (rec_buffer_total_size_ - noSamp10ms) / (rec_fs_hz_ / 1000);
        }
    }
}
