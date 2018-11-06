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

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <re.h>
#include "audio_io_ios.h"
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
static void *rec_thread(void *arg) {
	return static_cast<audio_io_ios*>(arg)->record_thread();
}
    
audio_io_ios::audio_io_ios() :
        audioCallback_(nullptr),
        au_(nullptr),
	mq_(nullptr),
        initialized_(false),
        is_shut_down_(false),
        is_recording_initialized_(false),
        is_playing_initialized_(false),
        rec_fs_hz_(0),
        play_fs_hz_(0),
        rec_delay_(0),
        num_capture_worker_calls_(0),
        tot_rec_delivered_(0),
        is_running_(false),
	can_rec_(false),	
        rec_tid_(0),
        dig_mic_gain_(0),
        want_stereo_playout_(false),
        using_stereo_playout_(false)
{
	rec_buffer_size_ = sizeof(uint16_t) * MAX_FS_REC_HZ * 2;
	rec_buffer_ = (uint8_t *)mem_zalloc(rec_buffer_size_, NULL);
	rec_avail_ = 0;
	rec_in_pos_ = 0;
	rec_out_pos_ = 0;

	play_buffer_size_ = MAX_FS_PLAY_HZ * 2;
	play_buffer_ = (int16_t *)mem_zalloc(sizeof(*play_buffer_)
					     * play_buffer_size_,
					     NULL);
	play_avail_ = 0;
	play_in_pos_ = 0;
	play_out_pos_ = 0;
			    
	is_recording_.store(false);
	is_playing_.store(false);

	pthread_mutex_init(&cond_mutex_,NULL);
	pthread_mutex_init(&lock_, NULL);

#if 1
	mqueue_alloc(&mq_, mq_callback, this);
#endif	
}

audio_io_ios::~audio_io_ios()
{
        StopPlayoutInternal();
        StopRecordingInternal();

	TerminateInternal();
        
	pthread_mutex_destroy(&cond_mutex_);

	rec_buffer_ = (uint8_t *)mem_deref(rec_buffer_);
	play_buffer_ = (int16_t *)mem_deref(play_buffer_);

	mem_deref(mq_);
}
    
int32_t audio_io_ios::RegisterAudioCallback(AudioTransport* audioCallback)
{
	StopPlayout();
	StopRecording(); // Stop the threads that uses audioCallback
	audioCallback_ = audioCallback;
	return 0;
}

int32_t audio_io_ios::Init() {
        if (initialized_) {
		return 0;
        }
	else {
		return -1;
        }
}
    
void audio_io_ios::mq_callback(int id, void *data, void *arg)
{
        audio_io_ios* ptrThis = static_cast<audio_io_ios*>(arg);
        
        switch (id) {
            case AUDIO_IO_COMMAND_START_PLAYOUT:
                ptrThis->StartPlayoutInternal();
                break;
                
            case AUDIO_IO_COMMAND_STOP_PLAYOUT:
                ptrThis->StopPlayoutInternal();
                break;
                
            case AUDIO_IO_COMMAND_START_RECORDING:
                ptrThis->StartRecordingInternal();
                break;
                
            case AUDIO_IO_COMMAND_STOP_RECORDING:
                ptrThis->StopRecordingInternal();
                break;
                
            case AUDIO_IO_COMMAND_RESET:
                ptrThis->ResetAudioDeviceInternal();
                break;
                
            default:
                break;
        }
    }
    
    int32_t audio_io_ios::InitInternal() {
        int ret = 0;
        if (initialized_) {
            goto out;
        }
        
        is_shut_down_ = false;
        initialized_ = true;
        
        ret = init_play_or_record();
out:
        return ret;
    }
    
	bool audio_io_ios::PlayoutIsInitialized() const {
		return is_playing_initialized_;
	}
    
	bool audio_io_ios::RecordingIsInitialized() const {
		return is_recording_initialized_;
	}

    int32_t audio_io_ios::StartPlayoutInternal() {
        info("audio_io_ios: StartPlayoutInternal \n");
        if(!is_playing_initialized_){
            goto out;
        }
        assert(!is_playing_.load());
        
	memset(play_buffer_, 0, sizeof(*play_buffer_) * play_buffer_size_);
	play_avail_ = 0;
	play_in_pos_ = 0;
	play_out_pos_ = 0;
        
        if (!is_recording_.load()) {
            OSStatus result = AudioOutputUnitStart(au_);
            if (result != noErr) {
                error("audio_io_ios: AudioOutputUnitStart failed: \n", result);
                return -1;
            }
        }
        is_playing_.store(true);
out:
        return 0;
    }
    
    
	int32_t audio_io_ios::StartPlayout() {
        info("audio_io_ios: StartPlayout \n");
        if(mq_){
            mqueue_push(mq_, AUDIO_IO_COMMAND_START_PLAYOUT, NULL);
        } else {
            StartPlayoutInternal();
        }
		return 0;
    }
    
	bool audio_io_ios::Playing() const {
		return is_playing_;
	}
    
	int32_t audio_io_ios::StartRecordingInternal() {
        info("audio_io_ios: StartRecordingInternal \n");
        if(!is_playing_initialized_){
            goto out;
        }
        assert(!is_recording_.load());
        
        memset(rec_buffer_, 0, rec_buffer_size_);
        rec_avail_ = 0;
	rec_in_pos_ = 0;
	rec_out_pos_ = 0;

        // Create and start capture thread
        if (!rec_tid_) {
            pthread_cond_init(&cond_, NULL);
            
            is_running_ = true;
            
            pthread_create(&rec_tid_, NULL, rec_thread, this);
            
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
            warning("audio_io_ios: Thread already created \n");
        }
        
        if (!is_playing_.load()) {
            OSStatus result = AudioOutputUnitStart(au_);
            if (result != noErr) {
                error("audio_io_ios: AudioOutputUnitStart failed: %d \n", result);
                return -1;
            }
        }
		is_recording_.store(true);
out:
		return 0;
    }
    
    int32_t audio_io_ios::StartRecording() {
        if(mq_){
            mqueue_push(mq_, AUDIO_IO_COMMAND_START_RECORDING, NULL);
        } else {
            StartRecordingInternal();
        }
        return 0;
    }
    
	bool audio_io_ios::Recording() const {
		return is_recording_;
	}
    
	int32_t audio_io_ios::StopRecordingInternal() {
        if (!is_recording_.load()) {
            goto out;
        }
        info("audio_io_ios: StopRecordingInternal \n");
        
        if (rec_tid_){
            void* thread_ret;
            
            is_running_ = false;
            
            pthread_cond_signal(&cond_);
            
            pthread_join(rec_tid_, &thread_ret);
            rec_tid_ = 0;
	    pthread_cond_destroy(&cond_);
        }
        
        if (!is_playing_.load()) {
            // Both playout and recording has stopped, shutdown the device.
            if (nullptr != au_) {
                OSStatus result = -1;
                result = AudioOutputUnitStop(au_);
                if (0 != result) {
                    error("audio_io_ios: AudioOutputUnitStop failed: %d ", result);
                }
            }
        }
        is_recording_.store(false);
out:
        return 0;
	}
    
    int32_t audio_io_ios::StopRecording() {
        info("audio_io_ios: StopRecording \n");
        if(mq_){
            mqueue_push(mq_, AUDIO_IO_COMMAND_STOP_RECORDING, NULL);
        } else {
            StopRecordingInternal();
        }
        int max_wait = 0;
        int err = 0;
        while(is_recording_.load()){
            usleep(2000);
            max_wait++;
            if(max_wait > 1000){
                error("audio_io_ios: Waiting for recording to stop failed \n");
                err = -1;
                break;
            }
        }
        return err;
    }
    
	int32_t audio_io_ios::StopPlayoutInternal() {
        if (!is_playing_.load()) {
            return 0;
        }
        info("audio_io_ios: StopPlayoutInternal\n");
        
        if (!is_recording_.load()) {
            // Both playout and recording has stopped, shutdown the device.
            if (nullptr != au_) {
                OSStatus result = -1;
                result = AudioOutputUnitStop(au_);
                if (0 != result) {
                    error("audio_io_ios: AudioOutputUnitStop failed: %d ", result);
                }
            }
        }
        is_playing_.store(false);
		return 0;
	}
    
    int32_t audio_io_ios::StopPlayout() {
        info("audio_io_ios: StopPlayout\n");
        if(mq_){
            mqueue_push(mq_, AUDIO_IO_COMMAND_STOP_PLAYOUT, NULL);
        } else {
            StopPlayoutInternal();
        }
        int max_wait = 0;
        int err = 0;
        while(is_playing_.load()){
            usleep(2000);
            max_wait++;
            if(max_wait > 1000){
                error("audio_io_ios: Waiting for playout to stop failed \n");
                err = -1;
                break;
            }
        }
        return err;
    }

	int32_t audio_io_ios::Terminate() {
        return 0;
    }
    
	int32_t audio_io_ios::TerminateInternal() {
        info("audio_io_ios: Terminate\n");
        if (!initialized_) {
            return 0;
        }
        shutdown_play_or_record();
        
        AVAudioSession* session = [AVAudioSession sharedInstance];
        [session setPreferredSampleRate:used_sample_rate_ error:nil];
        
        is_shut_down_ = true;
        initialized_ = false;
        return 0;
	}

    int32_t audio_io_ios::ResetAudioDevice() {
        info("audio_io_ios: ResetAudioDevice\n");
        if(mq_){
            mqueue_push(mq_, AUDIO_IO_COMMAND_RESET, NULL);
        } else {
            ResetAudioDeviceInternal();
        }
        return 0;
    }
    
    int32_t audio_io_ios::ResetAudioDeviceInternal() {
        info("audio_io_ios: ResetAudioDeviceInternal\n");
        
        if (!is_playing_initialized_ && !is_recording_initialized_) {
            info("audio_io_ios: Playout or recording not initialized\n");
        }
        
        int res(0);

        // Store the states we have before stopping to restart below
        bool initPlay = is_playing_initialized_;
        bool play = is_playing_.load();
        bool initRec = is_recording_initialized_;
        bool rec = is_recording_.load();
        
        // Stop playout and recording
        res += StopPlayoutInternal();
        res += StopRecordingInternal();
        
        shutdown_play_or_record();
        init_play_or_record();
        
        // Restart
#if 0	
        if (initPlay) res += InitPlayout();
        if (initRec)  res += InitRecording();
        if (play)     res += StartPlayoutInternal();
        if (rec)      res += StartRecordingInternal();
#endif
        res += InitPlayout();
        res += InitRecording();
        res += StartPlayoutInternal();
        res += StartRecordingInternal();

	return res;
    }
    
    int32_t audio_io_ios::StereoPlayoutIsAvailable(bool* available) const{
        info("audio_io_ios: StereoPlayoutIsAvailable: \n");
        
#ifdef ZETA_IOS_STEREO_PLAYOUT
        // Get array of current audio outputs (there should only be one)
        NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
	if (outputs == nil || outputs.count < 1) {
		warning("audio_io_ios: StereoPlayoutIsAvailable: no outputs\n");
		return -1;
	}
		
        AVAudioSessionPortDescription *outPortDesc = [outputs objectAtIndex:0];
        
        *available = false; // NB Only when a HS is plugged in
        if ([outPortDesc.portType isEqualToString:AVAudioSessionPortHeadphones]){
            *   available = true;
        }
#else
        *available = false;
#endif
        return 0;
    }
    
    int32_t audio_io_ios::SetStereoPlayout(bool enable) {
        info("audio_io_ios: SetStereoPlayout to %d: \n", enable);

        if(want_stereo_playout_ != enable){
            want_stereo_playout_ = enable;
            ResetAudioDevice();
        }

        return 0;
    }
    
    bool audio_io_ios::BuiltInAECIsAvailable() const {
        return !want_stereo_playout_;
    }
    
    int32_t audio_io_ios::PlayoutDeviceName(uint16_t index,
                                              char name[kAdmMaxDeviceNameSize],
                                              char guid[kAdmMaxGuidSize]) {
        info("audio_io_ios: PlayoutDeviceName(index=%d)\n", index);
        
        if (index != 0) {
            return -1;
        }
        
        NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
	if (outputs == nil || outputs.count < 1) {
		warning("audio_io_ios: PlayoutDeviceName: no outputs\n");
		return -1;
	}

	AVAudioSessionPortDescription *outPortDesc = [outputs objectAtIndex:0];
        
        if ([outPortDesc.portType isEqualToString:AVAudioSessionPortHeadphones]){
            sprintf(name, "headset");
        } else if ([outPortDesc.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]){
            sprintf(name, "speaker");
        } else if ([outPortDesc.portType isEqualToString:AVAudioSessionPortBluetoothHFP]){
            sprintf(name, "bt");
        } else {
            sprintf(name, "earpiece");
        }
        if (guid != NULL) {
            memset(guid, 0, kAdmMaxGuidSize);
        }
        
        return 0;
    }
    
    int32_t audio_io_ios::init_play_or_record() {
        info("audio_io_ios: AudioDeviceIOS::InitPlayOrRecord \n");
        assert(!au_);
        
        OSStatus result = -1;
	BOOL success;
        
        bool use_stereo_playout = false; // NB Only when a HS is plugged in
#ifdef ZETA_IOS_STEREO_PLAYOUT
        // Get array of current audio outputs (there should only be one)
        NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
	if (outputs == nil || outputs.count < 1) {
		warning("audio_io_ios: init_play_or_record: no outputs\n");
		return -1;
	}
        AVAudioSessionPortDescription *outPortDesc = [outputs objectAtIndex:0];
        if ([outPortDesc.portType isEqualToString:AVAudioSessionPortHeadphones]){
            use_stereo_playout = want_stereo_playout_;
            info("audio_io_ios: A Headset is plugged in \n");
        }
#endif
        using_stereo_playout_ = use_stereo_playout;
      
        info("audio_io_ios: want_stereo_playout_ = %d use_stereo_playout = %d \n",
             want_stereo_playout_, use_stereo_playout);
        
        NSString *cat = [AVAudioSession sharedInstance].category;
        if (cat != AVAudioSessionCategoryPlayAndRecord){
            error("audio_io_ios: catagory not AVAudioSessionCategoryPlayAndRecord \n");
        } else {
            info("audio_io_ios: catagory okay AVAudioSessionCategoryPlayAndRecord \n");
        }
        
        // Create Voice Processing Audio Unit
        AudioComponentDescription desc;
        AudioComponent comp;
        
        desc.componentType = kAudioUnitType_Output;

        if(use_stereo_playout){
            desc.componentSubType = kAudioUnitSubType_RemoteIO;
            info("audio_io_ios: Use kAudioUnitSubType_RemoteIO !! \n");
            dig_mic_gain_ = 3;
        }
	else {
            desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
            info("audio_io_ios: Use kAudioUnitSubType_VoiceProcessingIO !! \n");
            dig_mic_gain_ = 0;
        }
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        desc.componentFlags = 0;
        desc.componentFlagsMask = 0;
        
        comp = AudioComponentFindNext(nullptr, &desc);
        if (nullptr == comp) {
            error("Could not find audio component for Audio Unit \n");
            return -1;
        }
        
        result = AudioComponentInstanceNew(comp, &au_);
        if (0 != result) {
            error("audio_io_ios: Failed to create Audio Unit instance: %d \n", result);
            return -1;
        }
        
        NSError* err = nil;
        AVAudioSession* session = [AVAudioSession sharedInstance];
        Float64 preferredSampleRate(FS_REC_HZ);
        
        // In order to recreate the sample rate after a call
        used_sample_rate_ = session.sampleRate;

	err = nil;
        [session setPreferredSampleRate:preferredSampleRate error:&err];	
        if (err != nil) {
            const char* errorString = [[err localizedDescription] UTF8String];
            error("audio_io_ios: setPreferredSampleRate failed %s \n", errorString);
        }

	err = nil;
	success = [session setPreferredIOBufferDuration:AUDIO_IO_BUF_DUR
						  error:&err];
	if (!success || err != nil) {
            const char* errorString = [[err localizedDescription] UTF8String];
            error("audio_io_ios: setPreferredIOBUfferDuration failed %s\n",
		  errorString);
	}
	info("mm_platform_ios: IOBufferDuration=%dms\n",
	     (int)(session.IOBufferDuration * 1000.0));
        
        UInt32 enableIO = 1;
        result = AudioUnitSetProperty(au_,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input,
                                      1,  // input bus
                                      &enableIO, sizeof(enableIO));
        if (0 != result) {
            error("audio_io_ios: Failed to enable IO on input: %d \n", result);
        }
        
        result = AudioUnitSetProperty(au_,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,  // output bus
                                      &enableIO, sizeof(enableIO));
        if (0 != result) {
            error("audio_io_ios: Failed to enable IO on output: %d \n", result);
        }
        
        // Disable AU buffer allocation for the recorder, we allocate our own.
        UInt32 flag = 0;
        result = AudioUnitSetProperty(au_,
                                      kAudioUnitProperty_ShouldAllocateBuffer,
                                      kAudioUnitScope_Output, 1, &flag, sizeof(flag));
        if (0 != result) {
            warning("audio_io_ios: Failed to disable AU buffer allocation: %d \n", result);
            // Should work anyway
        }
        
        // Set recording callback.
        AURenderCallbackStruct auCbS;
        memset(&auCbS, 0, sizeof(auCbS));
        auCbS.inputProc = rec_process;
        auCbS.inputProcRefCon = this;
        result = AudioUnitSetProperty(
		          au_,
		          kAudioOutputUnitProperty_SetInputCallback,
			  kAudioUnitScope_Global,
			  1,
			  &auCbS,
			  sizeof(auCbS));
        if (0 != result) {
            error("audio_io_ios: Failed to set AU record callback \n ");
        }
        
        // Set playout callback.
        memset(&auCbS, 0, sizeof(auCbS));
        auCbS.inputProc = play_process;
        auCbS.inputProcRefCon = this;
        result = AudioUnitSetProperty(
			  au_,
			  kAudioUnitProperty_SetRenderCallback,
			  kAudioUnitScope_Global,
			  0, &auCbS, sizeof(auCbS));
        if (0 != result) {
            error("audio_io_ios: Failed to set AU output callback:: %d \n", result);
        }
        
        // Get stream format for out/0
        AudioStreamBasicDescription playoutDesc;
        UInt32 size = sizeof(playoutDesc);
        result =
        AudioUnitGetProperty(au_, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Output, 0, &playoutDesc, &size);
        if (0 != result) {
            error("audio_io_ios: Failed to get AU output stream format: %d \n", result);
        }
        
        // Get hardware sample rate for logging (see if we get what we asked for).
        Float64 sampleRate = session.sampleRate;
        info("audio_io_ios: Current HW sample rate is: %f wanted %f \n", sampleRate, preferredSampleRate);
        
        playoutDesc.mSampleRate = (Float64)sampleRate;
        
        rec_fs_hz_ = (uint32_t)sampleRate;
        play_fs_hz_ = rec_fs_hz_;
        
        // Set stream format for out/0.
        playoutDesc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
        kLinearPCMFormatFlagIsPacked |
        kLinearPCMFormatFlagIsNonInterleaved;
        playoutDesc.mBytesPerPacket = 2;
        playoutDesc.mFramesPerPacket = 1;
        playoutDesc.mBytesPerFrame = 2;
        if(use_stereo_playout){
            playoutDesc.mChannelsPerFrame = 2;
        } else {
            playoutDesc.mChannelsPerFrame = 1;
        }
        playoutDesc.mBitsPerChannel = 16;
        result =
        AudioUnitSetProperty(au_, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &playoutDesc, size);
        if (0 != result) {
            error("audio_io_ios: Failed to set AU stream format for out/0 \n");
        }
        
        // Get stream format for in/1.
        AudioStreamBasicDescription recordingDesc;
        size = sizeof(recordingDesc);
        result =
        AudioUnitGetProperty(au_, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 1, &recordingDesc, &size);
        if (0 != result) {
            error("audio_io_ios: Failed to get AU stream format for in/1 \n");
        }
        
        recordingDesc.mSampleRate = (Float64)sampleRate;
        
        // Set stream format for out/1 (use same sampling frequency as for in/1).
        recordingDesc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
        kLinearPCMFormatFlagIsPacked |
        kLinearPCMFormatFlagIsNonInterleaved;
        recordingDesc.mBytesPerPacket = 2;
        recordingDesc.mFramesPerPacket = 1;
        recordingDesc.mBytesPerFrame = 2;
        recordingDesc.mChannelsPerFrame = 1;
        recordingDesc.mBitsPerChannel = 16;
        result =
        AudioUnitSetProperty(au_, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Output, 1, &recordingDesc, size);
        if (0 != result) {
            error("audio_io_ios: Failed to set AU stream format for out/1 \n");
        }
        
        // Initialize here already to be able to get/set stream properties.
        result = AudioUnitInitialize(au_);
        if (0 != result) {
            error("audio_io_ios: AudioUnitInitialize failed: %d \n", result);
            return -1;
        }
        
        is_playing_initialized_ = true;
        is_recording_initialized_ = true;
        
        return 0;
    }
    
    int32_t audio_io_ios::shutdown_play_or_record() {
        info("audio_io_ios: ShutdownPlayOrRecord \n");
        
        // Close and delete AU.
        OSStatus result = -1;
        if (nullptr != au_) {
            result = AudioComponentInstanceDispose(au_);
            if (0 != result) {
                error("audio_io_ios: AudioComponentInstanceDispose failed: %d \n", result);
            }
            au_ = nullptr;
        }
        
        is_recording_initialized_ = false;
        is_playing_initialized_ = false;
        
        return 0;
    }
    
OSStatus audio_io_ios::play_process(
		void *inRefCon,
		AudioUnitRenderActionFlags *ioActionFlags,
		const AudioTimeStamp *inTimeStamp,
		UInt32 inBusNumber,
		UInt32 inNumberFrames,
		AudioBufferList *ioData)
{
	audio_io_ios* ptrThis = static_cast<audio_io_ios*>(inRefCon);
        
	return ptrThis->play_process_impl(inNumberFrames, ioData);
}


OSStatus audio_io_ios::play_process_impl(
		uint32_t inNumberFrames,
		AudioBufferList* ioData)
{
	unsigned int nbytes = ioData->mBuffers[0].mDataByteSize;
	unsigned int nsamps  = nbytes / 2;
	unsigned int nsamp10ms = play_fs_hz_ / 100;
	size_t nchans = ioData->mNumberBuffers;
	size_t avail;
	int64_t elapsed_time_ms;
	int64_t ntp_time_ms;
	int16_t* data;
	int pos;
	size_t needed = nchans * nsamps;
	size_t i;
	size_t j;
#if 0
	static uint64_t tt = tmr_jiffies();
	uint64_t now;

	now = tmr_jiffies();
	info("audio_io_ios: srate=%d %llums play_process: nchans=%d "
	     "needed=%dsamps have=%dsamps at pos=%d\n",
	     (int)play_fs_hz_, now - tt,
	     (int)nchans, (int)needed, (int)play_avail_,
		(int)play_in_pos_);
	tt = now;
#endif
	
	while (play_avail_ < needed) {
		avail = 0;
		if (audioCallback_) {
			audioCallback_->NeedMorePlayData(
				nsamp10ms,
				2,
				nchans,
				play_fs_hz_,
				(void*)&play_buffer_[play_in_pos_],
				avail,
				&elapsed_time_ms,
				&ntp_time_ms);

			avail *= nchans;		
		}
#if 0
		info("audio_io_ios: playdata avail=%dsamps\n", (int)avail);
#endif
		if (avail == 0) {
			warning("audio_io_ios: filling in with silence\n");
			memset(&play_buffer_[play_in_pos_],
			       0,
			       nsamp10ms * sizeof(int16_t) * nchans);
			avail = nsamp10ms * nchans;
		}

		play_in_pos_ += avail;
		play_avail_ += avail;
	}

	pos = 0;
	for (i = 0; i < nsamps; ++i) {
		for (j = 0; j < nchans; ++j) {
			data = (int16_t*)ioData->mBuffers[j].mData;
			data[pos] = play_buffer_[play_out_pos_];
			++play_out_pos_;
		}
		++pos;
	}
	play_avail_ -= needed;
	if (play_avail_ > 0) {
		memmove(play_buffer_,
			&play_buffer_[play_out_pos_],
			play_avail_ * sizeof(int16_t));
	}
	play_in_pos_ = play_avail_;
	play_out_pos_ = 0;

	return 0;
}


OSStatus audio_io_ios::rec_process(
		void* inRefCon,
		AudioUnitRenderActionFlags* ioActionFlags,
		const AudioTimeStamp* inTimeStamp,
		UInt32 inBusNumber,
		UInt32 inNumberFrames,
		AudioBufferList* ioData)
{
	audio_io_ios* ptrThis = static_cast<audio_io_ios*>(inRefCon);

	return ptrThis->rec_process_impl(ioActionFlags, inTimeStamp,
					 inBusNumber,
					 inNumberFrames);
}


static int16_t inline gain(int16_t d, int g)
{
	int32_t t = ((int32_t)d) << g;
	    
	return (int16_t) ((t > 32767) ? 32767 : ((t < -32767) ? -32767 : t));
}
	    
    
OSStatus audio_io_ios::rec_process_impl(
		AudioUnitRenderActionFlags* ioActionFlags,
		const AudioTimeStamp* inTimeStamp,
		uint32_t inBusNumber,
		uint32_t inNumberFrames)
{
	uint32_t fsize = sizeof(uint16_t)*inNumberFrames;
	uint32_t avail;
	uint32_t space;
	int fpos = 0;
#if 0
	static uint64_t tt = tmr_jiffies();
	uint64_t now;

	now = tmr_jiffies();

	info("audio_ios: rec_process: %llums can_rec=%d fs=%d avail=%d "
	     "inpos=%d outpos=%d rec_avail=%d\n",
	     now - tt,
	     can_rec_, rec_fs_hz_, avail,
	     rec_in_pos_, rec_out_pos_, rec_avail_);
#endif
	
	
	if (!is_recording_)
		return 0;	    

	uint8_t *rec_data = (uint8_t *)malloc(fsize);
	
	AudioBufferList abList;
	abList.mNumberBuffers = 1;
	abList.mBuffers[0].mData = rec_data;
	abList.mBuffers[0].mDataByteSize = fsize;
	abList.mBuffers[0].mNumberChannels = 1;

	// Get data from mic
	OSStatus res = AudioUnitRender(au_, ioActionFlags, inTimeStamp,
				       inBusNumber, inNumberFrames,
				       &abList);
	if (res != 0) {
		goto out;
	}

	avail = fsize;
	
	while(avail > 0) {
		
		if (rec_in_pos_ + avail >= rec_buffer_size_)
			space = rec_buffer_size_ - rec_in_pos_;
		else
			space = avail;

		if (dig_mic_gain_ > 0) {
			for(int i = 0; i < space; i += sizeof(int16_t)) {
				*(int16_t *)(&rec_buffer_[rec_in_pos_ + i]) =
					gain(*(int16_t *)(&rec_data[fpos + i]),
					     dig_mic_gain_);
			}
		}
		else {
			memcpy(&rec_buffer_[rec_in_pos_],
			       &rec_data[fpos],
			       space);
		}
		fpos += space;
		if (!can_rec_)
			rec_out_pos_ = rec_in_pos_;
		
		rec_in_pos_ += space;
		if (rec_in_pos_ >= rec_buffer_size_)
			rec_in_pos_ = 0;
		avail -= space;
        }

	pthread_mutex_lock(&lock_);
	rec_avail_ = can_rec_ ? rec_avail_ + fsize : fsize;
	pthread_mutex_unlock(&lock_);

	pthread_cond_signal(&cond_);

 out:
	free(rec_data);
        
        return 0;
}
    
void* audio_io_ios::record_thread()
{
	uint32_t currentMicLevel = 10;
	uint32_t newMicLevel = 0;
	int32_t ret;
	uint32_t duration;
	int tot_avail;
	int avail;
	int handled;
	uint32_t nbytes10ms = (rec_fs_hz_ * sizeof(uint16_t)) / 100;
	
	while(is_running_) {
		can_rec_ = true;

		pthread_mutex_lock(&cond_mutex_);
		pthread_cond_wait(&cond_, &cond_mutex_);
		pthread_mutex_unlock(&cond_mutex_);

		if(!is_running_)
			break;
            
		pthread_mutex_lock(&lock_);
		tot_avail = rec_avail_;
		pthread_mutex_unlock(&lock_);

		handled = 0;
		while (is_running_ && tot_avail >= nbytes10ms) {
			uint32_t nsamps;

			if (rec_out_pos_ + tot_avail >= rec_buffer_size_)
				avail = rec_buffer_size_ - rec_out_pos_;
			else
				avail = tot_avail;
			
			if (avail > nbytes10ms)
				avail = nbytes10ms;

			nsamps = avail >> 1;
			duration = (nsamps * 1000) / rec_fs_hz_;		
#if 0
			 info("audio_io_ios: record_thread: avail=%d "
			      "handling=%d duration=%dms\n",
			      tot_avail, avail, duration);
#endif

			 if (audioCallback_) {
				 ret = audioCallback_->RecordedDataIsAvailable(
					(void*)&rec_buffer_[rec_out_pos_],
					nsamps,
					2,
					1,
					rec_fs_hz_,
					duration,
					0,
					currentMicLevel,
					false,
					newMicLevel);
			 }

			 rec_out_pos_ += avail;
			 if (rec_out_pos_ >= rec_buffer_size_)
				 rec_out_pos_ = 0;
			 
			 tot_avail -= avail;
			 handled += avail;			
		 }

		 if (handled > 0) {
			 pthread_mutex_lock(&lock_);
			 rec_avail_ -= handled;
			 pthread_mutex_unlock(&lock_);
		 }
	 }

	 can_rec_ = false;
	 return NULL;
}
}
