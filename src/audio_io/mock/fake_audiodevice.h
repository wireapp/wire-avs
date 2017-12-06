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

#include "../audio_io_class.h"
#include <pthread.h>
#include <string.h>

#define FRAME_LEN_MS 10
#define FS_KHZ 16
#define FRAME_LEN (FRAME_LEN_MS*FS_KHZ)

namespace webrtc {
    class fake_audiodevice : public audio_io_class {
    public:
        fake_audiodevice(bool realtime = false);
        ~fake_audiodevice();
        int32_t AddRef() const { return 0; }
        int32_t Release() const { return 0; }
        int32_t RegisterEventObserver(AudioDeviceObserver* eventCallback) {
            return 0;
        }
        int32_t RegisterAudioCallback(AudioTransport* audioCallback);
        int32_t Init() { return 0; }
        int32_t InitInternal() { return 0; }
        int32_t InitSpeaker() { return 0; }
        int32_t SetPlayoutDevice(uint16_t index) { return -1; }
        int32_t SetPlayoutDevice(WindowsDeviceType device) { return -1; }
        int32_t SetStereoPlayout(bool enable) { return 0; }
        int32_t StopPlayout();
        int32_t InitMicrophone() { return 0; }
        int32_t SetRecordingDevice(uint16_t index) { return -1; }
        int32_t SetRecordingDevice(WindowsDeviceType device) { return -1; }
        int32_t SetStereoRecording(bool enable) { return 0; }
        int32_t SetAGC(bool enable) { return 0; }
        int32_t StopRecording();
        int64_t TimeUntilNextProcess() { return 0; }
        void Process() { return; }
        int32_t Terminate();
        int32_t TerminateInternal() { return 0; }
        
        int32_t EnableSine();
        
        int32_t ActiveAudioLayer(AudioLayer* audioLayer) const { return -1; }
        ErrorCode LastError() const { return kAdmErrNone; }
        bool Initialized() const { return true; }
        int16_t PlayoutDevices() { return -1; }
        int16_t RecordingDevices() { return -1; }
        int32_t PlayoutDeviceName(uint16_t index,
                                          char name[kAdmMaxDeviceNameSize],
                                          char guid[kAdmMaxGuidSize]) {
		memcpy(name, "fake", 4);
		name[4] = '\0';
		return 0;
        }
        int32_t RecordingDeviceName(uint16_t index,
                                            char name[kAdmMaxDeviceNameSize],
                                            char guid[kAdmMaxGuidSize]) {
            return -1;
        }
        int32_t PlayoutIsAvailable(bool* available) { return 0; }
        int32_t InitPlayout();
        bool PlayoutIsInitialized() const;
        int32_t RecordingIsAvailable(bool* available) { return 0; }
        int32_t InitRecording();
        bool RecordingIsInitialized() const;
        int32_t StartPlayout();
        bool Playing() const;
        int32_t StartRecording();
        bool Recording() const;
        bool AGC() const { return true; }
        int32_t SetWaveOutVolume(uint16_t volumeLeft,
                                         uint16_t volumeRight) {
            return -1;
        }
        int32_t WaveOutVolume(uint16_t* volumeLeft,
                                      uint16_t* volumeRight) const {
            return -1;
        }
        bool SpeakerIsInitialized() const { return true; }
        bool MicrophoneIsInitialized() const { return true; }
        int32_t SpeakerVolumeIsAvailable(bool* available) { return 0; }
        int32_t SetSpeakerVolume(uint32_t volume) { return 0; }
        int32_t SpeakerVolume(uint32_t* volume) const { return 0; }
        int32_t MaxSpeakerVolume(uint32_t* maxVolume) const { return 0; }
        int32_t MinSpeakerVolume(uint32_t* minVolume) const { return 0; }
        int32_t SpeakerVolumeStepSize(uint16_t* stepSize) const { return 0; }
        int32_t MicrophoneVolumeIsAvailable(bool* available) { return 0; }
        int32_t SetMicrophoneVolume(uint32_t volume) { return 0; }
        int32_t MicrophoneVolume(uint32_t* volume) const { return 0; }
        int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const { return 0; }
        int32_t MinMicrophoneVolume(uint32_t* minVolume) const { return 0; }
        int32_t MicrophoneVolumeStepSize(uint16_t* stepSize) const {
            return -1;
        }
        int32_t SpeakerMuteIsAvailable(bool* available) { return 0; }
        int32_t SetSpeakerMute(bool enable) { return 0; }
        int32_t SpeakerMute(bool* enabled) const { return 0; }
        int32_t MicrophoneMuteIsAvailable(bool* available) { return 0; }
        int32_t SetMicrophoneMute(bool enable) { return 0; }
        int32_t MicrophoneMute(bool* enabled) const { return 0; }
        int32_t MicrophoneBoostIsAvailable(bool* available) { return 0; }
        int32_t SetMicrophoneBoost(bool enable) { return 0; }
        int32_t MicrophoneBoost(bool* enabled) const { return 0; }
        int32_t StereoPlayoutIsAvailable(bool* available) const {
            *available = false;
            return 0;
        }
        int32_t StereoPlayout(bool* enabled) const { return 0; }
        int32_t StereoRecordingIsAvailable(bool* available) const {
            *available = false;
            return 0;
        }
        int32_t StereoRecording(bool* enabled) const { return 0; }
        int32_t SetRecordingChannel(const ChannelType channel) { return 0; }
        int32_t RecordingChannel(ChannelType* channel) const { return 0; }
        int32_t SetPlayoutBuffer(const BufferType type,
                                         uint16_t sizeMS = 0) {
            return 0;
        }
        int32_t PlayoutBuffer(BufferType* type, uint16_t* sizeMS) const {
            return 0;
        }
        int32_t PlayoutDelay(uint16_t* delayMS) const { return 0; }
        int32_t RecordingDelay(uint16_t* delayMS) const { return 0; }
        int32_t CPULoad(uint16_t* load) const { return 0; }
        int32_t StartRawOutputFileRecording(
                                                    const char pcmFileNameUTF8[kAdmMaxFileNameSize]) {
            return 0;
        }
        int32_t StopRawOutputFileRecording() { return 0; }
        int32_t StartRawInputFileRecording(
                                                   const char pcmFileNameUTF8[kAdmMaxFileNameSize]) {
            return 0;
        }
        int32_t StopRawInputFileRecording() { return 0; }
        int32_t SetRecordingSampleRate(const uint32_t samplesPerSec) {
            return 0;
        }
        int32_t RecordingSampleRate(uint32_t* samplesPerSec) const {
            return 0;
        }
        int32_t SetPlayoutSampleRate(const uint32_t samplesPerSec) {
            return 0;
        }
        int32_t PlayoutSampleRate(uint32_t* samplesPerSec) const { return 0; }
        int32_t ResetAudioDevice() { return 0; }
        int32_t SetLoudspeakerStatus(bool enable) { return 0; }
        int32_t GetLoudspeakerStatus(bool* enabled) const { return 0; }
        bool BuiltInAECIsAvailable() const { return false; }
        bool BuiltInNSIsAvailable() const { return false; }
        bool BuiltInAGCIsAvailable() const { return false; }
        int32_t EnableBuiltInAEC(bool enable) { return -1; }
        int32_t EnableBuiltInNS(bool enable) { return -1; }
        int32_t EnableBuiltInAGC(bool enable) { return -1; }
        bool BuiltInAECIsEnabled() const { return false; }
        
        int GetPlayoutAudioParameters(AudioParameters* params) const { return -1;}
        int GetRecordAudioParameters(AudioParameters* params) const { return -1;}
        
        void* record_thread();
        void* playout_thread();
    private:
        AudioTransport* audioCallback_;
        pthread_t rec_tid_ = 0;
        pthread_t play_tid_ = 0;
        volatile bool is_recording_;
        volatile bool is_playing_;
        volatile bool rec_is_initialized_;
        volatile bool play_is_initialized_;
        bool realtime_;
        float delta_omega_;
        float omega_;
    };

}
