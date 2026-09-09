/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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
#include <rem.h>
#include "pstn_audiodevice.h"
#include <sys/time.h>
#include <string.h>
#include <math.h>

#include <avs_string.h>
#include <avs_pstn.h>


#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

#define AUBUF_MIN   3
#define AUBUF_MAX   6

namespace webrtc {
static void *rec_thread(void *arg)
{
	//return static_cast<pstn_audiodevice*>(arg)->record_thread();
	return NULL;
}

static void *play_thread(void *arg)
{
	//return static_cast<pstn_audiodevice*>(arg)->playout_thread();
	return NULL;
}

pstn_audiodevice::pstn_audiodevice(bool realtime)
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

	aubuf_alloc(&aubuf_play_, AUBUF_MIN, AUBUF_MAX);
	aubuf_alloc(&aubuf_rec_, AUBUF_MIN, AUBUF_MAX);
}

pstn_audiodevice::~pstn_audiodevice()
{
	Terminate();
}

int32_t pstn_audiodevice::RegisterAudioCallback(AudioTransport* audioCallback)
{
	bool is_playing = is_playing_;
	bool is_recording = is_recording_;

	info("audio_io_pstn: Register\n");
 
	StopPlayout();
	StopRecording(); // Stop the threads that uses audioCallback

	audioCallback_ = audioCallback;
	if(is_playing)
		StartPlayout();
	if(is_recording)
		StartRecording();

	return 0;
}

int32_t pstn_audiodevice::InitPlayout()
{
	info("audio_io_pstn: InitPlayout\n");

	play_is_initialized_ = true;
	return 0;
}

bool pstn_audiodevice::PlayoutIsInitialized() const
{
	info("audio_io_pstn: PlayoutIsInitialized: %d\n",
	     play_is_initialized_);

	return play_is_initialized_;
}

int32_t pstn_audiodevice::InitRecording()
{
	info("audio_io_pstn: InitRecording\n");

	rec_is_initialized_ = true;
	return 0;
}

bool pstn_audiodevice::RecordingIsInitialized() const
{
	info("audio_io_pstn: RecordingIsIntialized: %d\n",
	     rec_is_initialized_);

	return rec_is_initialized_;
}

int32_t pstn_audiodevice::StartPlayout()
{
	info("audio_io_pstn: StartPlayout\n");

	if(!is_playing_) {
		pthread_create(&play_tid_, NULL, play_thread, this);
	}

	is_playing_ = true;
	return 0;
}

bool pstn_audiodevice::Playing() const
{
	info("audio_io_pstn: Playing: %d\n", is_playing_);

	return is_playing_;
}

int32_t pstn_audiodevice::StartRecording()
{
	info("audio_io_pstn: StartRecording\n");

	if (!is_recording_) {
		is_recording_ = true;
		//pthread_create(&rec_tid_, NULL, rec_thread, this);
	}

	return 0;
}

bool pstn_audiodevice::Recording() const
{
	info("audio_io_pstn: Recording: %d\n", is_recording_);

	return is_recording_;
}

int32_t pstn_audiodevice::StopRecording()
{
	info("audio_io_pstn: StopRecording\n");

	if (rec_tid_ && is_recording_) {
		void* thread_ret;
		is_recording_ = false;
		//pthread_join(rec_tid_, &thread_ret);
		//rec_tid_ = 0;
	}
	rec_is_initialized_ = false;

	return 0;
}

int32_t pstn_audiodevice::StopPlayout()
{
	info("audio_io_pstn: StopPlayout\n");

	if (play_tid_ && is_playing_) {
		void *thread_ret;

		is_playing_ = false;
		pthread_join(play_tid_, &thread_ret);
		play_tid_ = 0;
	}
	play_is_initialized_ = false;

	return 0;
}

int32_t pstn_audiodevice::Terminate()
{
	void *thread_ret;

	info("audio_io_pstn: Terminate\n");

	StopRecording();
	StopPlayout();

	return 0;
}

void *pstn_audiodevice::record_thread()
{
	int16_t audio_buf[FRAME_LEN];
	uint32_t currentMicLevel = 10;
	uint32_t newMicLevel = 0;
	struct timeval now, next_io_time, delta, sleep_time;

	info("audio_io_pstn: record_thread: started\n");

	memset(audio_buf, 0, sizeof(audio_buf));

	delta.tv_sec = 0;
	delta.tv_usec = FRAME_LEN_MS * 1000;

	gettimeofday(&next_io_time, NULL);

	while(is_recording_) {
		timeradd(&next_io_time, &delta, &next_io_time);
		aubuf_read(aubuf_rec_,
			   (uint8_t *)audio_buf,
			   FRAME_LEN * sizeof(int16_t));

		if(audioCallback_) {
			audioCallback_->RecordedDataIsAvailable(
					(void*)audio_buf,
					FRAME_LEN, 2, 1, FS_KHZ*1000, 0, 0,
					currentMicLevel, false, newMicLevel);
		}

		gettimeofday(&now, NULL);
		timersub(&next_io_time, &now, &sleep_time);
		if(sleep_time.tv_sec < 0){
			warning("pstn_audiodevice::record_thread() "
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

void *pstn_audiodevice::playout_thread()
{
	int16_t audio_buf[FRAME_LEN] = {0};
	size_t nSamplesOut;
	int64_t elapsed_time_ms, ntp_time_ms;
	struct timeval now, next_io_time, delta, sleep_time;

	info("audio_io_pstn: playout_thread: started\n");

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
		}
		aubuf_write(aubuf_play_,
			    (const uint8_t *)audio_buf,
			    nSamplesOut * sizeof(int16_t));
	}
	return NULL;
}

int32_t pstn_audiodevice::MicrophoneMuteIsAvailable(bool* available)
{
	info("pstn_audiodevice: MicrophoneMuteIsAvailable: available=%p\n", available);
	if (available)
		*available = true;

	return 0;
}

int32_t pstn_audiodevice::SetMicrophoneMute(bool enable)
{
	muted_ = enable;

	return 0;
}

struct aubuf *pstn_audiodevice::get_aubuf_play(void)
{
	return aubuf_play_;
}

struct aubuf *pstn_audiodevice::get_aubuf_rec(void)
{
	return aubuf_rec_;
}

void pstn_audiodevice::play_read(int16_t *sampv, size_t sampc)
{
	size_t nSamplesOut;
	int64_t elapsed_time_ms;
	int64_t ntp_time_ms;

#if 0
	info("pstn(%p): play_read of size=%zu playing=%d ac=%p\n",
	     this, sampc, is_playing_, audioCallback_);
#endif

	if(is_playing_ && audioCallback_) {
		audioCallback_->NeedMorePlayData(
			sampc/2, 2, 1, FS_KHZ*1000,
			(void*)sampv, nSamplesOut,
			&elapsed_time_ms, &ntp_time_ms);

		audioCallback_->NeedMorePlayData(
			sampc/2, 2, 1, FS_KHZ*1000,
			(void*)&sampv[sampc/2], nSamplesOut,
			&elapsed_time_ms, &ntp_time_ms);
	}
}
	
void pstn_audiodevice::rec_write(const int16_t *sampv, size_t sampc)
{
	uint32_t currentMicLevel = 10;
	uint32_t newMicLevel = 0;

	if (is_recording_ && audioCallback_) {
		audioCallback_->RecordedDataIsAvailable(
			(void*)sampv,
			sampc/2, 2, 1, FS_KHZ*1000, 0, 0,
			currentMicLevel, false, newMicLevel);
		audioCallback_->RecordedDataIsAvailable(
			(void*)&sampv[sampc/2],
			sampc/2, 2, 1, FS_KHZ*1000, 0, 0,
			currentMicLevel, false, newMicLevel);
	}
}

	

} // namespace webrtc


struct {
	struct list adml;
	struct list handlerl;
} pstn = {
	.adml = LIST_INIT,
	.handlerl = LIST_INIT
};

struct adm_entry {
	void *adm;
	char *convid;

	struct le le;
};

struct adm_handler {
	pstn_adm_h *admh;
	void *arg;

	struct le le;
};

static void ae_destructor(void *arg)
{
	struct adm_entry *ae = (struct adm_entry *)arg;

	list_unlink(&ae->le);
	mem_deref(ae->convid);
}

static struct adm_entry *adm_find(const char *convid)
{
	struct le *le;
	bool found = false;
	struct adm_entry *ae;
	
	for(le = pstn.adml.head; le && !found; le = le->next) {
		ae = (struct adm_entry *)le->data;
		if (!ae)
			continue;

		found = streq(ae->convid, convid);
	}

	return found ? ae : NULL;
	
}

static void ah_destructor(void *arg)
{
	struct adm_handler *ah = (struct adm_handler *)arg;

	list_unlink(&ah->le);
}

int pstn_adm_handler_register(pstn_adm_h *admh, void *arg)
{
	struct adm_handler *ah;
	
	if (!admh)
		return EINVAL;

	ah = (struct adm_handler *)mem_zalloc(sizeof(*ah), ah_destructor);
	if (!ah)
		return ENOMEM;

	ah->admh = admh;
	ah->arg = arg;

	list_append(&pstn.handlerl, &ah->le, ah);

	return 0;
}

void pstn_adm_handler_unregister(pstn_adm_h *admh, void *arg)	
{
	struct adm_handler *ah;
	struct le *le;
	bool found = false;

	for(le = pstn.handlerl.head; le && !found; le = le->next) {
		ah = (struct adm_handler *)le->data;
		if (!ah)
			continue;
		found = ah->admh == admh && ah->arg == arg;
	}

	if (found) {
		mem_deref(ah);
	}
}


int pstn_adm_register(const char *convid, void *adm)
{
	struct adm_entry *ae = adm_find(convid);
	struct le *le;

	info("pstn_adm_register: convid=%s adm=%p\n", convid, adm);
	
	if (ae)
		return EALREADY;

	ae = (struct adm_entry *)mem_zalloc(sizeof(*ae), ae_destructor);
	if (!ae)
		return ENOMEM;

	ae->adm = adm;
	str_dup(&ae->convid, convid);

	
	list_append(&pstn.adml, &ae->le, ae);

	printf("+++++ registered adm, calling handlers\n");
	LIST_FOREACH(&pstn.handlerl, le) {
		struct adm_handler *ah = (struct adm_handler *)le->data;

		printf("+++++ registered adm calling handler(%p)\n", ah);
	
		if (!ah)
			continue;

		if (ah->admh) {
			ah->admh(ae->convid, ae->adm, true, ah->arg);
		}
	}

	return 0;
}

int pstn_adm_unregister(const char *convid)
{
	struct adm_entry *ae = adm_find(convid);

	if (!ae)
		return ENOENT;

	mem_deref((void *)ae);

	return 0;
}

void *pstn_adm_find(const char *convid)
{
	struct adm_entry *ae = adm_find(convid);

	return ae ? ae->adm : NULL;
}

struct aubuf *pstn_get_aubuf_play(void *adm)
{
	return ((webrtc::pstn_audiodevice *)adm)->get_aubuf_play();
}

struct aubuf *pstn_get_aubuf_rec(void *adm)
{
	return ((webrtc::pstn_audiodevice *)adm)->get_aubuf_rec();
}

void pstn_rec_write(void *adm, const int16_t *sampv, size_t sampc)
{
	((webrtc::pstn_audiodevice *)adm)->rec_write(sampv, sampc);
}

void pstn_play_read(void *adm, int16_t *sampv, size_t sampc)
{
	((webrtc::pstn_audiodevice *)adm)->play_read(sampv, sampc);
}
