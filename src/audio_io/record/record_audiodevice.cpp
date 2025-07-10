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
#include "record_audiodevice.h"
#include <sys/time.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_base.h"
#include "avs_log.h"
#ifdef __cplusplus
}
#endif
    
    
namespace webrtc {
static void *rec_thread(void *arg)
{
	return static_cast<record_audiodevice*>(arg)->record_thread();
}

static void *play_thread(void *arg)
{
	return static_cast<record_audiodevice*>(arg)->playout_thread();
}
    
record_audiodevice::record_audiodevice(bool realtime)
{
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
	muted_ = false;
	noise_ = false;
	fp_ = NULL;
}

record_audiodevice::~record_audiodevice()
{
	Terminate();
}
    
int32_t record_audiodevice::RegisterAudioCallback(AudioTransport* audioCallback)
{
	bool is_playing = is_playing_;
	bool is_recording = is_recording_;

	info("audio_io_record: Register\n");
	
	StopPlayout();
	StopRecording(); // Stop the threads that uses audioCallback
	
	audioCallback_ = audioCallback;
	if(is_playing)
		StartPlayout();
	if(is_recording)
		StartRecording();

	return 0;
}
    
int32_t record_audiodevice::InitPlayout()
{
	info("audio_io_record: InitPlayout\n");
	
	play_is_initialized_ = true;
	return 0;
}
    
bool record_audiodevice::PlayoutIsInitialized() const
{
	info("audio_io_record: PlayoutIsInitialized: %d\n",
	     play_is_initialized_);
	
	return play_is_initialized_;
}
    
int32_t record_audiodevice::InitRecording()
{
	info("audio_io_record: InitRecording\n");
	
	rec_is_initialized_ = true;
	return 0;
}
    
bool record_audiodevice::RecordingIsInitialized() const
{
	info("audio_io_record: RecordingIsIntialized: %d\n",
	     rec_is_initialized_);
	
	return rec_is_initialized_;
}
    
int32_t record_audiodevice::StartPlayout()
{
	info("audio_io_record: StartPlayout\n");
	
	if(!is_playing_) {
		pthread_create(&play_tid_, NULL, play_thread, this);
	}

	is_playing_ = true;
	return 0;
}
    
bool record_audiodevice::Playing() const
{
	info("audio_io_record: Playing: %d\n", is_playing_);
	
	return is_playing_;
}
    
int32_t record_audiodevice::StartRecording()
{
	info("audio_io_record: StartRecording\n");

	if (!is_recording_) {
		is_recording_ = true;
		pthread_create(&rec_tid_, NULL, rec_thread, this);
	}

	return 0;
}
    
bool record_audiodevice::Recording() const
{
	info("audio_io_record: Recording: %d\n", is_recording_);

	return is_recording_;
}
    
int32_t record_audiodevice::StopRecording()
{
	info("audio_io_record: StopRecording\n");

	if (rec_tid_ && is_recording_) {
		void* thread_ret;
		is_recording_ = false;
		pthread_join(rec_tid_, &thread_ret);
		rec_tid_ = 0;
	}
	rec_is_initialized_ = false;

	return 0;
}
    
int32_t record_audiodevice::StopPlayout()
{
	info("audio_io_record: StopPlayout\n");

	if (play_tid_ && is_playing_) {
		void *thread_ret;
		
		is_playing_ = false;
		pthread_join(play_tid_, &thread_ret);
		play_tid_ = 0;
	}
	if (fp_) {
	  fclose(fp_);
	}
	play_is_initialized_ = false;

	return 0;
}
    
int32_t record_audiodevice::Terminate()
{
	void *thread_ret;

	info("audio_io_record: Terminate\n");

	StopRecording();
	StopPlayout();

	return 0;
}

int32_t record_audiodevice::EnableNoise()
{
	info("audio_io_record: EnableNoise\n");

	noise_ = true;

	return 0;
}

int32_t record_audiodevice::EnableSine()
{
	info("audio_io_record: EnableSine\n");

	delta_omega_ = 2*3.14*(440.0f/(FS_KHZ*1000));
	omega_ = 0.0f;
        
	return 0;
}
        
void *record_audiodevice::record_thread()
{
	int16_t audio_buf[FRAME_LEN];
	uint32_t currentMicLevel = 10;
	uint32_t newMicLevel = 0;
	struct timeval now, next_io_time, delta, sleep_time;

	info("audio_io_record: record_thread: started\n");
	
	memset(audio_buf, 0, sizeof(audio_buf));

	delta.tv_sec = 0;
	delta.tv_usec = FRAME_LEN_MS * 1000;
        
	gettimeofday(&next_io_time, NULL);
        
	while(is_recording_) {
		if(audioCallback_) {
			audioCallback_->RecordedDataIsAvailable(
					(void*)audio_buf,
					FRAME_LEN, 2, 1, FS_KHZ*1000, 0, 0,
					currentMicLevel, false, newMicLevel);
		}

		gettimeofday(&now, NULL);
		timersub(&next_io_time, &now, &sleep_time);
		if(sleep_time.tv_sec < 0){
			warning("record_audiodevice::record_thread() "
				"not processing data fast enough "
				"now = %d.%d next_io_time = %d.%d\n",
				(int32_t)now.tv_sec, now.tv_usec,
				next_io_time.tv_sec, next_io_time.tv_usec);
			sleep_time.tv_usec = 0;
		}
		timespec t;
		t.tv_sec = 0;
		t.tv_nsec = sleep_time.tv_usec*1000;
		if (realtime_) {
			nanosleep(&t, NULL);
		}
	}

	return NULL;
}
    
void *record_audiodevice::playout_thread()
{
	int16_t audio_buf[FRAME_LEN] = {0};
	size_t nSamplesOut;
	int64_t elapsed_time_ms, ntp_time_ms;
	struct timeval now, next_io_time, delta, sleep_time;
	const char *path;

	path = avs_get_audio_record();
	if (path) {
	  if (fp_) {
	    fclose(fp_);
	    fp_ = NULL;
	  }
	  fp_ = fopen(path, "wb");
	}

	info("audio_io_record: playout_thread: started path=%s\n", path);

	delta.tv_sec = 0;
	delta.tv_usec = FRAME_LEN_MS * 1000;

	gettimeofday(&next_io_time, NULL);

	while(is_playing_) {
		timeradd(&next_io_time, &delta, &next_io_time);

		if(audioCallback_) {
			audioCallback_->NeedMorePlayData(
					FRAME_LEN, 2, 1, FS_KHZ*1000,
					(void*)audio_buf, nSamplesOut,
					&elapsed_time_ms, &ntp_time_ms);
			if (fp_) {
			  re_printf("audio_record: frame_len=%d nsamples: %zu\n", FRAME_LEN, nSamplesOut);
			  fwrite(audio_buf, 2, nSamplesOut, fp_);
			}
		}
            
		gettimeofday(&now, NULL);
		timersub(&next_io_time, &now, &sleep_time);
		if(sleep_time.tv_sec < 0) {
			warning("record_audiodevice::playout_thread(): "
				"not processing data fast enough "
				"now = %d.%d next_io_time = %d.%d\n",
				(int32_t)now.tv_sec, now.tv_usec,
				next_io_time.tv_sec, next_io_time.tv_usec);
			sleep_time.tv_usec = 0;
		}

		timespec t;
		t.tv_sec = 0;
		t.tv_nsec = sleep_time.tv_usec*1000;
		if(realtime_) {
			nanosleep(&t, NULL);
		}
	}
	return NULL;
}

int32_t record_audiodevice::MicrophoneMuteIsAvailable(bool* available)
{
	info("record_audiodevice: MicrophoneMuteIsAvailable: available=%p\n", available);
	if (available)
		*available = true;

	return 0;
}
int32_t record_audiodevice::SetMicrophoneMute(bool enable)
{
	muted_ = enable;

	return 0;
}


} // namespace webrtc

