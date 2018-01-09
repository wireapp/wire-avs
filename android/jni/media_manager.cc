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

#include <stdint.h>
#include <jni.h>
#include <pthread.h>
#ifdef ANDROID
#include <android/log.h>
#include "webrtc/voice_engine/include/voe_base.h"
#endif

#include "avs_mediamgr.h"
#include <re/re.h>
#include <avs.h>

#include "../../src/mediamgr/mm_platform_android.h"
#include <stdlib.h>

#include <pthread.h>

static jfieldID mmfid;

struct jmm {
    struct mediamgr *mm;
    jobject self;
};

struct jmm *self2mm(JNIEnv *env, jobject self)
{
    jlong ptr = env->GetLongField(self, mmfid);
    struct jmm *jmm = (struct jmm *)((void*)ptr);
    
    return jmm;
}

#ifndef ANDROID // Android simulator dosnt include mm_platform_android.c    
int mm_android_jni_init(JNIEnv *env, jobject jobj, jobject ctx){
    return 0;
}
void mm_android_jni_cleanup(JNIEnv *env, jobject jobj){
    return;
}
bool mm_android_jni_java_get_initialized()
{
    return false;
}
void mm_android_jni_on_playback_route_changed(enum mediamgr_auplay new_route, void *arg){
    return;
}
void mm_android_jni_on_media_category_changed(enum mediamgr_state state, void *arg){
    return;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
    
JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_attach
(JNIEnv *env, jobject self, jobject ctx)
{
	struct jmm *jmm;
	int err;

    
    debug("MediaManager_attach called from thread %lu \n", pthread_self());
    
	if (!mm_android_jni_java_get_initialized()) {
		mm_android_jni_init(env, self, ctx);
	}

	jmm = (struct jmm *)mem_zalloc(sizeof(*jmm), NULL);
	if (!jmm) {
		err = ENOMEM;
		goto out;
	}

	jmm->self = env->NewGlobalRef(self);

    err = mediamgr_alloc(&jmm->mm, mm_android_jni_on_media_category_changed, NULL);
	if (err) {
		warning("jni: media_manager: cannot allocate mm: %d\n", err);
		goto out;
	}
    
    debug("mediamgr_alloc allocated at %p \n", jmm->mm);
    
    mediamgr_register_route_change_h(jmm->mm, mm_android_jni_on_playback_route_changed, NULL);
    
    jclass mmClass;
    if ((mmClass= env->FindClass("com/waz/media/manager/MediaManager")) == NULL){
        error("Could not find class com/waz/media/manager/MediaManager \n");
    }
    mmfid = env->GetFieldID(mmClass, "mmPointer", "J");
    if (mmfid == NULL) {
        error("Could not find mm_field \n");
    }
    
	env->SetLongField(self, mmfid, (jlong)((void *)jmm));
    
 out:
	if (err) {
		mem_deref((void *)jmm);
	}
}

JNIEXPORT void Java_com_waz_media_manager_MediaManager_detach
(JNIEnv *env, jobject self)
{
    struct jmm *jmm = self2mm(env, self);
    jobject jobj;
    
    jmm->mm = (struct mediamgr *)mem_deref(jmm->mm);
    
    mm_android_jni_cleanup(env, self);

    jobj = jmm->self;
    jmm->self = NULL;
    env->DeleteGlobalRef(jobj);
        
    mem_deref(jmm);
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_playMedia
(JNIEnv *env, jobject self, jstring jmedia_name)
{
    struct jmm *jmm;
    
    jmm = self2mm(env, self);
    
    const char *media_name = env->GetStringUTFChars(jmedia_name, 0);
    
    debug("call mediamgr_play_media at %p \n", jmm->mm);
    
    mediamgr_play_media(jmm->mm, media_name);
    if (media_name) {
        env->ReleaseStringUTFChars(jmedia_name, media_name);
    }
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_stopMedia
(JNIEnv *env, jobject self, jstring jmedia_name)
{
    struct jmm *jmm;
        
    jmm = self2mm(env, self);
    
    const char *media_name = env->GetStringUTFChars(jmedia_name, 0);
    
    debug("call mediamgr_stop_media at %p \n", jmm->mm);
        
    mediamgr_stop_media(jmm->mm, media_name);
    if (media_name) {
        env->ReleaseStringUTFChars(jmedia_name, media_name);
    }
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_EnableSpeaker
(JNIEnv *env, jobject self, bool enable)
{
    struct jmm *jmm;
        
    jmm = self2mm(env, self);
        
    debug("call mediamgr_enable_speaker at %p \n", jmm->mm);
        
    mediamgr_enable_speaker(jmm->mm, enable);
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_registerMedia
(JNIEnv *env, jobject self, jstring jmedia_name, jobject player_obj, bool mixing, bool incall, int intensity)
{
    struct jmm *jmm;
        
    jmm = self2mm(env, self);
    
    const char *media_name = env->GetStringUTFChars(jmedia_name, 0);
    
    jobject refobj = env->NewGlobalRef(player_obj);

    debug("mediamgr_registerMedia = %s \n", media_name);
    
    int priority = 0;
    if(strlen(media_name) >= strlen("ringing")){
        if( strncmp(media_name, "ringing", strlen("ringing")) == 0){
            priority = 1;
        }
    }
    
    mediamgr_register_media(jmm->mm, media_name, (void*)refobj, mixing, incall, intensity, priority, false);
    if (media_name) {
        env->ReleaseStringUTFChars(jmedia_name, media_name);
    }
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_unregisterMedia
(JNIEnv *env, jobject self, jstring jmedia_name)
{
    struct jmm *jmm;
        
    jmm = self2mm(env, self);
        
    const char *media_name = env->GetStringUTFChars(jmedia_name, 0);
        
    debug("mediamgr_unregisterMedia = %s \n", media_name);
        
    mediamgr_unregister_media(jmm->mm, media_name);
    if (media_name) {
        env->ReleaseStringUTFChars(jmedia_name, media_name);
    }
}
    
JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_setCallState
(JNIEnv *env, jobject self, bool incall)
{
    struct jmm *jmm;
    mediamgr_state state;
        
    jmm = self2mm(env, self);
        
    debug("mediamgr_set_call_state = %d \n", incall);
        
    if(incall){
        state = MEDIAMGR_STATE_INCALL;
    } else {
        state = MEDIAMGR_STATE_NORMAL;
    }
        
    mediamgr_set_call_state(jmm->mm, state);
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_setVideoCallState
(JNIEnv *env, jobject self)
{
    struct jmm *jmm;
    mediamgr_state state = MEDIAMGR_STATE_INVIDEOCALL;
        
    jmm = self2mm(env, self);
        
    debug("mediamgr_set_call_state to video call \n");
    
    mediamgr_set_call_state(jmm->mm, state);
}
    
JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_setIntensityAll
(JNIEnv *env, jobject self)
{
    mediamgr_sound_mode mode = MEDIAMGR_SOUND_MODE_ALL;
    struct jmm *jmm;
    mediamgr_state state;
        
    jmm = self2mm(env, self);
        
    debug("mediamgr_intensity_all \n");
        
    mediamgr_set_sound_mode(jmm->mm, mode);
}

JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_setIntensitySome
(JNIEnv *env, jobject self)
{
    mediamgr_sound_mode mode = MEDIAMGR_SOUND_MODE_SOME;
    struct jmm *jmm;
    mediamgr_state state;
        
    jmm = self2mm(env, self);
        
    debug("mediamgr_intensity_some \n");
    
    mediamgr_set_sound_mode(jmm->mm, mode);
}
    
JNIEXPORT void JNICALL Java_com_waz_media_manager_MediaManager_setIntensityNone
(JNIEnv *env, jobject self)
{
    mediamgr_sound_mode mode = MEDIAMGR_SOUND_MODE_NONE;
    struct jmm *jmm;
    mediamgr_state state;
        
    jmm = self2mm(env, self);
        
    debug("mediamgr_intensity_none \n");
        
    mediamgr_set_sound_mode(jmm->mm, mode);
}
    
#ifdef __cplusplus
}
#endif
