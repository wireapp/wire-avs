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
#include <assert.h>
#include <jni.h>
#include <string.h>

#ifdef ANDROID
#include <android/log.h>
#endif

#include <re/re.h>

#include <avs.h>

/* JNI interface */
#include "com_waz_audioeffect_AudioEffect.h"

#ifdef ANDROID
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AudioEffect C++", __VA_ARGS__))
#else
#define LOGI(...)
#endif

JNIEXPORT jint JNICALL Java_com_waz_audioeffect_AudioEffect_applyEffectWav(JNIEnv* env, jobject self, jstring jfile_name_in, jstring jfile_name_out, jint effect, jboolean reduce_noise)
{
    const char *file_name_in = env->GetStringUTFChars(jfile_name_in, 0);
    const char *file_name_out = env->GetStringUTFChars(jfile_name_out, 0);
    
    enum audio_effect effect_type = AUDIO_EFFECT_CHORUS_MIN;
    if (effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_CHORUS_MIN) {
        effect_type = AUDIO_EFFECT_CHORUS_MIN;
    } else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_CHORUS_MAX){
        effect_type = AUDIO_EFFECT_CHORUS_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MIN){
        effect_type = AUDIO_EFFECT_REVERB_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MED){
        effect_type = AUDIO_EFFECT_REVERB_MID;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MAX){
        effect_type = AUDIO_EFFECT_REVERB_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MIN){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MED){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MAX){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_INSANE){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MED){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_INSANE){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MIN){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MED){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MAX){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MED){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERSE){
        effect_type = AUDIO_EFFECT_REVERSE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_VOCODER_MED){
        effect_type = AUDIO_EFFECT_VOCODER_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MIN){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MED){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MAX){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MED){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_NONE){
        effect_type = AUDIO_EFFECT_NONE;
    }
    
    int ret = apply_effect_to_wav(file_name_in, file_name_out, effect_type, reduce_noise, NULL, NULL);
    
    if (file_name_in)
        env->ReleaseStringUTFChars(jfile_name_in, file_name_in);
    if (file_name_out)
        env->ReleaseStringUTFChars(jfile_name_out, file_name_out);
    
    return ret;
}

JNIEXPORT jint JNICALL Java_com_waz_audioeffect_AudioEffect_applyEffectPCM(JNIEnv* env, jobject self, jstring jfile_name_in, jstring jfile_name_out, jint jfs_hz, jint effect,  jboolean reduce_noise)
{
    const char *file_name_in = env->GetStringUTFChars(jfile_name_in, 0);
    const char *file_name_out = env->GetStringUTFChars(jfile_name_out, 0);
    
    enum audio_effect effect_type = AUDIO_EFFECT_CHORUS_MIN;
    if (effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_CHORUS_MIN) {
        effect_type = AUDIO_EFFECT_CHORUS_MIN;
    } else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_CHORUS_MED){
        effect_type = AUDIO_EFFECT_CHORUS_MED;
    } else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_CHORUS_MAX){
        effect_type = AUDIO_EFFECT_CHORUS_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MIN){
        effect_type = AUDIO_EFFECT_REVERB_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MED){
        effect_type = AUDIO_EFFECT_REVERB_MID;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERB_MAX){
        effect_type = AUDIO_EFFECT_REVERB_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MIN){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MED){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_MAX){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_INSANE){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MED){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_DOWN_INSANE){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MIN){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MED){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_UP_MAX){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MED){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PACE_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_REVERSE){
        effect_type = AUDIO_EFFECT_REVERSE;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_VOCODER_MIN){
        effect_type = AUDIO_EFFECT_VOCODER_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_VOCODER_MED){
        effect_type = AUDIO_EFFECT_VOCODER_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MIN){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MED){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_AUTO_TUNE_MAX){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MIN){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MIN;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MED){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MED;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MAX){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MAX;
    }else if(effect == com_waz_audioeffect_AudioEffect_AVS_AUDIO_EFFECT_NONE){
        effect_type = AUDIO_EFFECT_NONE;
    }
    
    int ret = apply_effect_to_pcm(file_name_in, file_name_out, jfs_hz, effect_type, reduce_noise, NULL, NULL);
    
    if (file_name_in)
        env->ReleaseStringUTFChars(jfile_name_in, file_name_in);
    if (file_name_out)
        env->ReleaseStringUTFChars(jfile_name_out, file_name_out);
    
    return ret;
}
