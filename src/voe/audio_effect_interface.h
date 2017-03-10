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

#ifndef AVS_EFFECT_INTERFACE
#define AVS_EFFECT_INTERFACE

#include "webrtc/voice_engine/include/voe_external_media.h"
#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include <cmath>

extern "C" {
    #include "avs_audio_effect.h"
}

class VoEAudioEffect : public webrtc::VoEMediaProcess {
public:
    VoEAudioEffect(bool test_mode) {
        fs_hz_ = 32000;
        aueffect_alloc(&normalizer_, AUDIO_EFFECT_NORMALIZER, fs_hz_);
        effect_ = NULL;
        force_reset_ = false;
        test_mode_ = test_mode;
        omega_ = 0.0f;
        delta_omega_ = 0.0f;
    }
    virtual ~VoEAudioEffect() {
        mem_deref(effect_);
        mem_deref(normalizer_);
    }
    virtual void Process(int channel,
                         webrtc::ProcessingTypes type,
                         int16_t audio10ms[],
                         size_t length,
                         int samplingFreq,
                         bool isStereo)
    {
        if(samplingFreq != fs_hz_ || force_reset_){
            aueffect_reset(normalizer_, samplingFreq);
            if(effect_){
                aueffect_reset(effect_, samplingFreq);
            }
            fs_hz_ = samplingFreq;
            if(samplingFreq > 0){
                delta_omega_ = (2*3.14f*400.0f)/(samplingFreq);
            }
            force_reset_ = false;
        }
        if(test_mode_){
            GenerateSine(audio10ms, length);
        } else {
            size_t out_len;
            if(effect_){
                aueffect_process(effect_, audio10ms, audio10ms, length, &out_len);
            }
            aueffect_process(normalizer_, audio10ms, audio10ms, length, &out_len);
        }
    }
    void AddEffect(enum audio_effect effect_type)
    {
        mem_deref(effect_);
        effect_ = NULL;
        if(effect_type != AUDIO_EFFECT_NONE){
            aueffect_alloc(&effect_, effect_type, fs_hz_);
        }
    }
    void ResetNormalizer()
    {
        force_reset_ = true;
    }
    
private:
    void GenerateSine(int16_t buf[], size_t length)
    {
        float tmp;
        for( int i = 0; i < length; i++){
            tmp = (int16_t)(sinf(omega_) * 8000.0f);
            omega_ += delta_omega_;
            buf[i] = (int16_t)tmp;
        }
        omega_ = fmod(omega_, 2*3.1415926536);
    }
    
    struct aueffect *normalizer_;
    struct aueffect *effect_;
    int fs_hz_;
    bool force_reset_;
    bool test_mode_;
    float omega_;
    float delta_omega_;
};

#endif
