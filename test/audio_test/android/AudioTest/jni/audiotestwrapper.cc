/*
 * Copyright (C) 2014 Zeta Project
 *
 */

/* This is a JNI example where we use native methods to play sounds
 * using OpenSL ES. See the corresponding Java source file located at:
 *
 *   src/com/audiotest/AudioTest.java
 */

#include <assert.h>
#include <jni.h>
#include <string.h>
#include <android/log.h>

#include "webrtc/voice_engine/include/voe_base.h"
#include "../../../src/AudioTest.h"

#ifdef ANDROID
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "audiotestwrapper C++", __VA_ARGS__))
#else
#define LOGI(...)
#endif

extern "C" {

JNIEXPORT void JNICALL Java_com_audiotest_VoeConfTest_Start
(JNIEnv* env, jclass clazz, jobject context, bool use_hw_aec, int num_channels)
{
    JavaVM* jvm = NULL;
    
    jint res = env->GetJavaVM(&jvm);
    
    webrtc::VoiceEngine::SetAndroidObjects(jvm, context);
    
    LOGI("StartVoeConfTest \n");
    
    {
        voe_conf_test_dec("/sdcard", use_hw_aec, num_channels);
    }
}

JNIEXPORT void JNICALL Java_com_audiotest_StartStopStressTest_Start
(JNIEnv* env, jclass clazz, jobject context)
{
    JavaVM* jvm = NULL;
        
    jint res = env->GetJavaVM(&jvm);
        
    webrtc::VoiceEngine::SetAndroidObjects(jvm, context);
        
    LOGI("StartStartStopStressTest \n");
        
    {
        char *argv[] = {"dummy"};
        int argc = sizeof(argv)/sizeof(char*);
            
        start_stop_stress_test(argc, argv, "/sdcard");
    }
}

JNIEXPORT void JNICALL Java_com_audiotest_LoopbackTest_Start
(JNIEnv* env, jclass clazz, jobject context)
{
    JavaVM* jvm = NULL;
        
    jint res = env->GetJavaVM(&jvm);
        
    webrtc::VoiceEngine::SetAndroidObjects(jvm, context);
        
    LOGI("StartStartStopStressTest \n");
        
    {
        char *argv[] = {"dummy"};
        int argc = sizeof(argv)/sizeof(char*);
            
        voe_loopback_test(argc, argv, "/sdcard");
    }
}
    
}
