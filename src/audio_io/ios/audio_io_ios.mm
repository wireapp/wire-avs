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
    static void *rec_thread(void *arg){
        return static_cast<audio_io_ios*>(arg)->record_thread();
    }
    
    audio_io_ios::audio_io_ios() :
        audioCallback_(nullptr),
        au_(nullptr),
        initialized_(false),
        is_shut_down_(false),
        is_recording_(false),
        is_playing_(false),
        is_recording_initialized_(false),
        is_playing_initialized_(false),
        rec_fs_hz_(0),
        play_fs_hz_(0),
        rec_delay_(0),
        play_buffer_used_(0),
        rec_current_seq_(0),
        rec_buffer_total_size_(0),
        num_capture_worker_calls_(0),
        tot_rec_delivered_(0),
        is_running_(false),
        rec_tid_(0),
        dig_mic_gain_(0),
        want_stereo_playout_(false),
        using_stereo_playout_(false),
        cmdh_(NULL),
        arg_(NULL){
            memset(play_buffer_, 0, sizeof(play_buffer_));
            memset(rec_buffer_, 0, sizeof(rec_buffer_));
            memset(rec_length_, 0, sizeof(rec_length_));
            memset(rec_seq_, 0, sizeof(rec_seq_));

            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&mutex_, &attr);
            
            pthread_mutex_init(&cond_mutex_,NULL);
            pthread_cond_init(&cond_, NULL);
    }

	audio_io_ios::~audio_io_ios() {
		Terminate();
        
		pthread_mutex_destroy(&mutex_);
		pthread_mutex_destroy(&cond_mutex_);
		pthread_cond_destroy(&cond_);
	}
    
	int32_t audio_io_ios::RegisterAudioCallback(AudioTransport* audioCallback) {
		StopPlayout();
		StopRecording(); // Stop the threads that uses audioCallback
		audioCallback_ = audioCallback;
		return 0;
	}

    int32_t audio_io_ios::Init() {
        if (initialized_) {
            return 0;
        } else {
            return -1;
        }
    }
    
    int32_t audio_io_ios::RegisterCommandHandler(audio_io_command_h *cmdh, void *arg){
        cmdh_ = cmdh;
        arg_ = arg;
        
        return 0;
    }
    
    int32_t audio_io_ios::HandleCommand(enum audio_io_command cmd) {
        switch(cmd){
        	case AUDIO_IO_COMMAND_START_PLAYOUT:
                StartPlayoutInternal();
        	break;

            case AUDIO_IO_COMMAND_STOP_PLAYOUT:
                StopPlayoutInternal();
            break;

            case AUDIO_IO_COMMAND_START_RECORDING:
                StartRecordingInternal();
            break;

            case AUDIO_IO_COMMAND_STOP_RECORDING:
                StopRecordingInternal();
            break;

            case AUDIO_IO_COMMAND_RESET:
                ResetAudioDeviceInternal();
            break;
                
        	default:
        	break;
        }
        
        return 0;
    }
    
    int32_t audio_io_ios::InitInternal() {
        int ret = 0;
        pthread_mutex_lock(&mutex_);
        if (initialized_) {
            goto out;
        }
        
        is_shut_down_ = false;
        initialized_ = true;
        
        ret = init_play_or_record();
out:
        pthread_mutex_unlock(&mutex_);
            
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
        pthread_mutex_lock(&mutex_);
        if(!is_playing_initialized_){
            goto out;
        }
        assert(!is_playing_);
        
        memset(play_buffer_, 0, sizeof(play_buffer_));
        play_buffer_used_ = 0;
        
        if (!is_recording_) {
            OSStatus result = AudioOutputUnitStart(au_);
            if (result != noErr) {
                error("audio_io_ios: AudioOutputUnitStart failed: \n", result);
                pthread_mutex_unlock(&mutex_);
                return -1;
            }
        }
        is_playing_ = true;
        out:
        pthread_mutex_unlock(&mutex_);
        return 0;
    }
    
    
	int32_t audio_io_ios::StartPlayout() {
        info("audio_io_ios: StartPlayout \n");
        if(cmdh_){
            cmdh_(AUDIO_IO_COMMAND_START_PLAYOUT, arg_);
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
        pthread_mutex_lock(&mutex_);
        if(!is_playing_initialized_){
            goto out;
        }
        assert(!is_recording_);
        
        memset(rec_buffer_, 0, sizeof(rec_buffer_));
        memset(rec_length_, 0, sizeof(rec_length_));
        memset(rec_seq_, 0, sizeof(rec_seq_));
        
        rec_current_seq_ = 0;
        rec_buffer_total_size_ = 0;
        rec_delay_ = 0;
        
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
        
        if (!is_playing_) {
            OSStatus result = AudioOutputUnitStart(au_);
            if (result != noErr) {
                error("audio_io_ios: AudioOutputUnitStart failed: %d \n", result);
                pthread_mutex_unlock(&mutex_);
                return -1;
            }
        }
		is_recording_ = true;
    out:
        pthread_mutex_unlock(&mutex_);
		return 0;
    }
    
    int32_t audio_io_ios::StartRecording() {
        if(cmdh_){
            cmdh_(AUDIO_IO_COMMAND_START_RECORDING, arg_);
        } else {
            StartRecordingInternal();
        }
        return 0;
    }
    
	bool audio_io_ios::Recording() const {
		return is_recording_;
	}
    
	int32_t audio_io_ios::StopRecordingInternal() {
        pthread_mutex_lock(&mutex_);
        if (!is_recording_) {
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
        
        if (!is_playing_) {
            // Both playout and recording has stopped, shutdown the device.
            if (nullptr != au_) {
                OSStatus result = -1;
                result = AudioOutputUnitStop(au_);
                if (0 != result) {
                    error("audio_io_ios: AudioOutputUnitStop failed: %d ", result);
                }
            }
        }
        is_recording_ = false;
    out:
        pthread_mutex_unlock(&mutex_);
        return 0;
	}
    
    int32_t audio_io_ios::StopRecording() {
        info("audio_io_ios: StopRecording \n");
        if(cmdh_){
            cmdh_(AUDIO_IO_COMMAND_STOP_RECORDING, arg_);
        } else {
            StopRecordingInternal();
        }
        int max_wait = 0;
        int err = 0;
        while(is_recording_){
            usleep(1000);
            max_wait++;
            info("Waiting for recording to stop !! \n");
            if(max_wait > 1000){
                err = -1;
                break;
            }
        }
        return err;
    }
    
	int32_t audio_io_ios::StopPlayoutInternal() {
        pthread_mutex_lock(&mutex_);
        if (!is_playing_) {
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        info("audio_io_ios: StopPlayoutInternal \n");
        
        if (!is_recording_) {
            // Both playout and recording has stopped, shutdown the device.
            if (nullptr != au_) {
                OSStatus result = -1;
                result = AudioOutputUnitStop(au_);
                if (0 != result) {
                    error("audio_io_ios: AudioOutputUnitStop failed: %d ", result);
                }
            }
        }
        is_playing_ = false;
        pthread_mutex_unlock(&mutex_);
		return 0;
	}
    
    int32_t audio_io_ios::StopPlayout() {
        info("audio_io_ios: StopPlayout \n");
        if(cmdh_){
            cmdh_(AUDIO_IO_COMMAND_STOP_PLAYOUT, arg_);
        } else {
            StopPlayoutInternal();
        }
        int max_wait = 0;
        int err = 0;
        while(is_playing_){
            usleep(1000);
            max_wait++;
            info("Waiting for playout to stop !! \n");
            if(max_wait > 1000){
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
        info("audio_io_ios: Terminate \n");
        pthread_mutex_lock(&mutex_);
        if (!initialized_) {
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        shutdown_play_or_record();
        
        AVAudioSession* session = [AVAudioSession sharedInstance];
        [session setPreferredSampleRate:used_sample_rate_ error:nil];
        
        is_shut_down_ = true;
        initialized_ = false;
        pthread_mutex_unlock(&mutex_);
        return 0;
	}

    int32_t audio_io_ios::ResetAudioDevice() {
        info("audio_io_ios: ResetAudioDevice \n");
        if(cmdh_){
            cmdh_(AUDIO_IO_COMMAND_RESET, arg_);
        } else {
            ResetAudioDeviceInternal();
        }
        return 0;
    }
    
    int32_t audio_io_ios::ResetAudioDeviceInternal() {
        pthread_mutex_lock(&mutex_);
        
        info("audio_io_ios: ResetAudioDeviceInternal \n");
        
        if (!is_playing_initialized_ && !is_recording_initialized_) {
            info("audio_io_ios: Playout or recording not initialized, doing nothing \n");
            pthread_mutex_unlock(&mutex_);
            return 0;
        }
        
        int res(0);

        // Store the states we have before stopping to restart below
        bool initPlay = is_playing_initialized_;
        bool play = is_playing_;
        bool initRec = is_recording_initialized_;
        bool rec = is_recording_;
        
        // Stop playout and recording
        res += StopPlayoutInternal();
        res += StopRecordingInternal();
        
        shutdown_play_or_record();
        init_play_or_record();
        
        // Restart
        if (initPlay) res += InitPlayout();
        if (initRec)  res += InitRecording();
        if (play)     res += StartPlayoutInternal();
        if (rec)      res += StartRecordingInternal();
        
        pthread_mutex_unlock(&mutex_);
        return res;
    }
    
    int32_t audio_io_ios::StereoPlayoutIsAvailable(bool* available) const{
        info("audio_io_ios: StereoPlayoutIsAvailable: \n");
        
#ifdef ZETA_IOS_STEREO_PLAYOUT
        // Get array of current audio outputs (there should only be one)
        NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
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
        AVAudioSessionPortDescription *outPortDesc = [outputs objectAtIndex:0];
        
        if ([outPortDesc.portType isEqualToString:AVAudioSessionPortHeadphones]){
            sprintf(name, "headset");
        } else if ([outPortDesc.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]){
            sprintf(name, "speaker");
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
        
        bool use_stereo_playout = false; // NB Only when a HS is plugged in
#ifdef ZETA_IOS_STEREO_PLAYOUT
        // Get array of current audio outputs (there should only be one)
        NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
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
        } else {
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
        
        [session setPreferredSampleRate:preferredSampleRate error:&err];
        if (err != nil) {
            const char* errorString = [[err localizedDescription] UTF8String];
            error("audio_io_ios: setPreferredSampleRate failed %s \n", errorString);
        }
        
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
        result = AudioUnitSetProperty(au_, kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global, 1, &auCbS, sizeof(auCbS));
        if (0 != result) {
            error("audio_io_ios: Failed to set AU record callback \n ");
        }
        
        // Set playout callback.
        memset(&auCbS, 0, sizeof(auCbS));
        auCbS.inputProc = play_process;
        auCbS.inputProcRefCon = this;
        result = AudioUnitSetProperty(au_, kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Global, 0, &auCbS, sizeof(auCbS));
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
    
    OSStatus audio_io_ios::play_process(void *inRefCon,
                                        AudioUnitRenderActionFlags *ioActionFlags,
                                        const AudioTimeStamp *inTimeStamp,
                                        UInt32 inBusNumber,
                                        UInt32 inNumberFrames,
                                        AudioBufferList *ioData) {
        audio_io_ios* ptrThis = static_cast<audio_io_ios*>(inRefCon);
        
        return ptrThis->play_process_impl(inNumberFrames, ioData);
    }
    
    OSStatus audio_io_ios::play_process_impl(uint32_t inNumberFrames, AudioBufferList* ioData) {
        int16_t* data = static_cast<int16_t*>(ioData->mBuffers[0].mData);
        unsigned int dataSizeBytes = ioData->mBuffers[0].mDataByteSize;
        unsigned int dataSize = dataSizeBytes / 2;  // Number of samples
        assert(dataSize == inNumberFrames);
        memset(data, 0, dataSizeBytes);  // Start with empty buffer
        
        int16_t* data2 = NULL;
        if(ioData->mNumberBuffers == 2){
            data2 = static_cast<int16_t*>(ioData->mBuffers[1].mData);
            unsigned int dataSizeBytes2 = ioData->mBuffers[1].mDataByteSize;
            unsigned int dataSize2 = dataSizeBytes2/2;  // Number of samples
            assert(dataSize2 == inNumberFrames);
            assert(dataSize ==  dataSize2);
            memset(data2, 0, dataSizeBytes);
        }
        
        // Get playout data from Audio Device Buffer
        if (is_playing_) {
            unsigned int noSamp10ms = play_fs_hz_ / 100;
            int16_t dataTmp[2*noSamp10ms];
            memset(dataTmp, 0, 2 * noSamp10ms);
            unsigned int dataPos = 0;
            size_t nSamplesOut = 0;
            unsigned int nCopy = 0;
            
            // First insert data from playout buffer if any
            if (play_buffer_used_ > 0) {
                nCopy = (dataSize < play_buffer_used_) ? dataSize : play_buffer_used_;
                assert(nCopy == play_buffer_used_);
                if(ioData->mNumberBuffers == 2){
                    for( unsigned int i = 0 ; i < nCopy; i++){
                        data[i] = play_buffer_[2*i];
                        data2[i] = play_buffer_[2*i+1];
                    }
                } else {
                    memcpy(data, play_buffer_, 2 * nCopy);
                }
                dataPos = nCopy;
                memset(play_buffer_, 0, sizeof(play_buffer_));
                play_buffer_used_ = 0;
            }
            
            // Now get the rest from Audio Device Buffer.
            while (dataPos < dataSize) {
                
                if(audioCallback_){
                    int64_t elapsed_time_ms, ntp_time_ms;
                    int32_t ret = audioCallback_->NeedMorePlayData(noSamp10ms, 2, ioData->mNumberBuffers, play_fs_hz_,
                                                                   (void*)dataTmp, nSamplesOut,
                                                                   &elapsed_time_ms, &ntp_time_ms);
                    
                    assert(noSamp10ms == (unsigned int)nSamplesOut);
                }
                // Insert as much as fits in data buffer
                nCopy =
                (dataSize - dataPos) > noSamp10ms ? noSamp10ms : (dataSize - dataPos);
                if(ioData->mNumberBuffers == 2){
                    for( unsigned int i = 0 ; i < nCopy; i++){
                        data[dataPos + i] = dataTmp[2*i];
                        data2[dataPos + i] = dataTmp[2*i+1];
                    }
                }else{
                    memcpy(&data[dataPos], dataTmp, 2*nCopy);
                }
                
                // Save rest in playout buffer if any
                if (nCopy < noSamp10ms) {
                    if(ioData->mNumberBuffers == 2){
                        memcpy(play_buffer_, &dataTmp[2*nCopy], sizeof(int16_t) * 2 * (noSamp10ms-nCopy));
                    } else {
                        memcpy(play_buffer_, &dataTmp[nCopy], sizeof(int16_t) * (noSamp10ms-nCopy));
                    }
                    play_buffer_used_ = noSamp10ms - nCopy;
                }
                
                // Update loop/index counter, if we copied less than noSamp10ms
                // samples we shall quit loop anyway
                dataPos += noSamp10ms;
            }
        }
        return 0;
    }
    
    void audio_io_ios::update_rec_delay() {
        const uint32_t noSamp10ms = rec_fs_hz_ / 100;
        if (rec_buffer_total_size_ > noSamp10ms) {
            rec_delay_ = (rec_buffer_total_size_ - noSamp10ms) / (rec_fs_hz_ / 1000);
        }
    }

    OSStatus audio_io_ios::rec_process(
                                           void* inRefCon,
                                           AudioUnitRenderActionFlags* ioActionFlags,
                                           const AudioTimeStamp* inTimeStamp,
                                           UInt32 inBusNumber,
                                           UInt32 inNumberFrames,
                                           AudioBufferList* ioData) {
        audio_io_ios* ptrThis = static_cast<audio_io_ios*>(inRefCon);
        return ptrThis->rec_process_impl(ioActionFlags, inTimeStamp, inBusNumber,
                                          inNumberFrames);
    }
    
    OSStatus audio_io_ios::rec_process_impl(
                                               AudioUnitRenderActionFlags* ioActionFlags,
                                               const AudioTimeStamp* inTimeStamp,
                                               uint32_t inBusNumber,
                                               uint32_t inNumberFrames) {
        int16_t dataTmp[inNumberFrames];
        memset(dataTmp, 0, 2 * inNumberFrames);
        
        AudioBufferList abList;
        abList.mNumberBuffers = 1;
        abList.mBuffers[0].mData = dataTmp;
        abList.mBuffers[0].mDataByteSize = 2 * inNumberFrames;  // 2 bytes/sample
        abList.mBuffers[0].mNumberChannels = 1;
        
        // Get data from mic
        OSStatus res = AudioUnitRender(au_, ioActionFlags, inTimeStamp,
                                       inBusNumber, inNumberFrames, &abList);
        if (res != 0) {
            return 0;
        }
        
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
                    if ((rec_length_[bufPos] > 0) &&
                        (rec_length_[bufPos] < noSamp10ms)) {
                        insertPos = static_cast<int16_t>(bufPos);
                        bufPos = REC_BUFFERS;
                    } else if ((-1 == insertPos) && (0 == rec_length_[bufPos])) {
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
                    
                    if(dig_mic_gain_ > 0){
                        for(int i = 0; i < nCopy; i++){
                            rec_buffer_[insertPos][currentRecLen + i] = dataTmp[dataPos + i] << dig_mic_gain_;
                        }
                    } else {
                        memcpy(&rec_buffer_[insertPos][currentRecLen], &dataTmp[dataPos],
                               nCopy * sizeof(int16_t));
                    }
                    if (0 == currentRecLen) {
                        rec_seq_[insertPos] = rec_current_seq_;
                        ++rec_current_seq_;
                    }
                    rec_buffer_total_size_ += nCopy;
                    rec_length_[insertPos] += nCopy;
                    dataPos += nCopy;
                } else {
                    dataPos = inNumberFrames;  // Don't try to insert more
                }
            }
        }
        
        /* wakeup the waiting thread */
        pthread_cond_signal(&cond_);
        
        return 0;
    }
    
    void* audio_io_ios::record_thread(){
        int16_t audio_buf[MAX_FS_REC_HZ*FRAME_LEN_MS] = {0};
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
            
            num_capture_worker_calls_+=1;
            
            int bufPos = 0;
            unsigned int lowestSeq = 0;
            int lowestSeqBufPos = 0;
            bool foundBuf = is_recording_;
            const unsigned int noSamp10ms = rec_fs_hz_ / 100;
            
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
                    update_rec_delay();
                    
                    if(audioCallback_){
                        int32_t ret = audioCallback_->RecordedDataIsAvailable(
                                            (void*)rec_buffer_[lowestSeqBufPos],
                                            rec_length_[lowestSeqBufPos], 2, 1,
                                            rec_fs_hz_, rec_delay_, 0,
                                            currentMicLevel, false, newMicLevel);
                        
                        tot_rec_delivered_ += noSamp10ms;
                    }
                    
                    rec_seq_[lowestSeqBufPos] = 0;
                    rec_buffer_total_size_ -= rec_length_[lowestSeqBufPos];
                    rec_length_[lowestSeqBufPos] = 0;
                }
            }
            if( rec_fs_hz_ > 0 && tot_rec_delivered_ >= rec_fs_hz_){
                // every second check how many
                #define THRES_MAX_CALLS_PER_MS_Q10 154 // 150/sec
                int32_t thres = ((int32_t)((1000*tot_rec_delivered_)/rec_fs_hz_) * THRES_MAX_CALLS_PER_MS_Q10) >> 10;
                if(num_capture_worker_calls_ > thres){
                    if(cmdh_){
                        error("audio_io_ios: %d captureworker calls in %d s run time error! \n",
                              num_capture_worker_calls_, tot_rec_delivered_/rec_fs_hz_);
                      
                        cmdh_(AUDIO_IO_COMMAND_RESET, arg_);
                    }
                }
                tot_rec_delivered_ = 0;
                num_capture_worker_calls_ = 0;
            }
        }
        return NULL;
    }
}
