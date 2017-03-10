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

#include <re.h>
#include "fake_audiodevice.h"
#include <sys/time.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif
    
    
namespace webrtc {
	static void *rec_thread(void *arg){
		return static_cast<fake_audiodevice*>(arg)->record_thread();
	}

	static void *play_thread(void *arg){
		return static_cast<fake_audiodevice*>(arg)->playout_thread();
	}
    
	fake_audiodevice::fake_audiodevice(bool realtime) {
		audioCallback_ = NULL;
		is_recording_ = false;
		is_playing_ = false;
		rec_is_initialized_ = false;
		play_is_initialized_ = false;
		rec_tid_ = 0;
		play_tid_ = 0;
		realtime_ = realtime;
		delta_omega_ = 0.0f;
		omega_ = 0.0f;
    }

	fake_audiodevice::~fake_audiodevice() {
		Terminate();
	}
    
	int32_t fake_audiodevice::RegisterAudioCallback(AudioTransport* audioCallback) {
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
    
	int32_t fake_audiodevice::InitPlayout() {
		play_is_initialized_ = true;
		return 0;
	}
    
	bool fake_audiodevice::PlayoutIsInitialized() const {
		return play_is_initialized_;
	}
    
	int32_t fake_audiodevice::InitRecording() {
		rec_is_initialized_ = true;
		return 0;
	}
    
	bool fake_audiodevice::RecordingIsInitialized() const {
		return rec_is_initialized_;
	}
    
	int32_t fake_audiodevice::StartPlayout() {
		if(!is_playing_){
			pthread_create(&play_tid_, NULL, play_thread, this);
		}
		is_playing_ = true;
		return 0;
    }
    
	bool fake_audiodevice::Playing() const {
		return is_playing_;
	}
    
	int32_t fake_audiodevice::StartRecording() {
		if(!is_recording_){
			is_recording_ = true;
			pthread_create(&rec_tid_, NULL, rec_thread, this);
		}
		return 0;
    }
    
	bool fake_audiodevice::Recording() const {
		return is_recording_;
	}
    
	int32_t fake_audiodevice::StopRecording() {
		if (rec_tid_ && is_recording_){
			void* thread_ret;
			is_recording_ = false;
			pthread_join(rec_tid_, &thread_ret);
			rec_tid_ = 0;
		}
		rec_is_initialized_ = false;
		return 0;
	}
    
	int32_t fake_audiodevice::StopPlayout() {
		if (play_tid_ && is_playing_){
			void* thread_ret;
			is_playing_ = false;
			pthread_join(play_tid_, &thread_ret);
			play_tid_ = 0;
		}
		play_is_initialized_ = false;
		return 0;
	}
    
	int32_t fake_audiodevice::Terminate() {
		void* thread_ret;
        
		StopRecording();
		StopPlayout();

		return 0;
	}

    int32_t fake_audiodevice::EnableSine() {
		delta_omega_ = 2*3.14*(440.0f/(FS_KHZ*1000));
		omega_ = 0.0f;
        
		return 0;
    }
        
	void* fake_audiodevice::record_thread(){
		int16_t audio_buf[FRAME_LEN] = {0};
		uint32_t currentMicLevel = 10;
		uint32_t newMicLevel = 0;
		struct timeval now, next_io_time, delta, sleep_time;
        
		delta.tv_sec = 0;
		delta.tv_usec = FRAME_LEN_MS * 1000;
        
		gettimeofday(&next_io_time, NULL);
        
		while(1){
			if(!is_recording_){
				break;
			}

			timeradd(&next_io_time, &delta, &next_io_time);

			if(delta_omega_ > 0.0f){
				float tmp;
				for( int i = 0; i < FRAME_LEN; i++){
					tmp = (int16_t)(sinf(omega_) * 8000.0f);
					omega_ += delta_omega_;
					audio_buf[i] = (int16_t)tmp;
				}
				omega_ = fmod(omega_, 2*3.1415926536);
			}
                
			if(audioCallback_){
				int32_t ret = audioCallback_->RecordedDataIsAvailable((void*)audio_buf,
												FRAME_LEN, 2, 1, FS_KHZ*1000, 0, 0,
												currentMicLevel, false, newMicLevel);
			}
            
			gettimeofday(&now, NULL);
			timersub(&next_io_time, &now, &sleep_time);
			if(sleep_time.tv_sec < 0){
				warning("fake_audiodevice::record_thread() not processing data fast enough now = %d.%d next_io_time = %d.%d!! \n", (int32_t)now.tv_sec, now.tv_usec, next_io_time.tv_sec, next_io_time.tv_usec);
				sleep_time.tv_usec = 0;
			}
            
			timespec t;
			t.tv_sec = 0;
			t.tv_nsec = sleep_time.tv_usec*1000;
			if(realtime_){
				nanosleep(&t, NULL);
			}
		}
		return NULL;
	}
    
	void* fake_audiodevice::playout_thread(){
		int16_t audio_buf[FRAME_LEN] = {0};
		size_t nSamplesOut;
		int64_t elapsed_time_ms, ntp_time_ms;
		struct timeval now, next_io_time, delta, sleep_time;
        
		delta.tv_sec = 0;
		delta.tv_usec = FRAME_LEN_MS * 1000;
        
		gettimeofday(&next_io_time, NULL);
        
		while(1){
			if(!is_playing_){
				break;
			}
            
			timeradd(&next_io_time, &delta, &next_io_time);
            
			if(audioCallback_){
				int32_t ret = audioCallback_->NeedMorePlayData(FRAME_LEN, 2, 1, FS_KHZ*1000,
																(void*)audio_buf, nSamplesOut,
																&elapsed_time_ms, &ntp_time_ms);
			}
            
			gettimeofday(&now, NULL);
			timersub(&next_io_time, &now, &sleep_time);
			if(sleep_time.tv_sec < 0){
				warning("fake_audiodevice::playout_thread(): not processing data fast enough now = %d.%d next_io_time = %d.%d!! \n", (int32_t)now.tv_sec, now.tv_usec, next_io_time.tv_sec, next_io_time.tv_usec);
				sleep_time.tv_usec = 0;
			}
            
			timespec t;
			t.tv_sec = 0;
			t.tv_nsec = sleep_time.tv_usec*1000;
			if(realtime_){
				nanosleep(&t, NULL);
			}
		}
		return NULL;
    }
}
