/*
 * Copyright (C) 2014 Zeta Project
 *
 */

/* This is a JNI example where we use native methods to play sounds
 * using OpenSL ES. See the corresponding Java source file located at:
 *
 *   src/com/soundlinkdemo/SoundLinkDemo.java
 */

#include <assert.h>
#include <jni.h>
#include <string.h>

#ifdef ANDROID
#include <android/log.h>
#endif

#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/system_wrappers/interface/trace.h"

#include "soundlink/src/include/SoundLinkImpl.h"
#include "settings.h"

#ifdef ANDROID
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "SoundLink C++", __VA_ARGS__))
#else
#define LOGI(...)
#endif

/**********************************************************************/
/* Helper functionality to call java from c/c+                        */
/**********************************************************************/
static struct {
    bool initialized;
    JavaVM *vm;
    jmethodID sldmid;
    jobject self;
} c2java = {
    false,
    NULL,
    NULL,
    NULL,
};

struct jni_env {
    bool attached;
    JNIEnv *env;
};

static int c2java_init(JNIEnv *env, jobject jobj, jobject ctx);
static void c2java_call_method( const char* tmp_time, const char* tmp_msg, int msg_len, int deviceRoundTripLatencyMs );
/**********************************************************************/

/**********************************************************************/
/* SoundLinkAudioTransport class implements methods to process mic    */
/* data and deliver speaker data using SoundLink algorithm.           */
/* The methods are called from webrtc Android Audio device            */
/**********************************************************************/
using webrtc::AudioDeviceModule;

class SoundLinkAudioTransport: public webrtc::AudioTransport, public SoundLinkOutputReceiver {
public:
    SoundLinkAudioTransport(AudioDeviceModule* audioDevice){
        Create_SoundLink(&soundLink_);
    }
    
    ~SoundLinkAudioTransport() {
        Free_SoundLink(soundLink_);
    }
    
    virtual int32_t RecordedDataIsAvailable(
                                            const void* audioSamples,
                                            const uint32_t nSamples,
                                            const uint8_t nBytesPerSample,
                                            const uint8_t nChannels,
                                            const uint32_t sampleRate,
                                            const uint32_t totalDelay,
                                            const int32_t clockSkew,
                                            const uint32_t currentMicLevel,
                                            const bool keyPressed,
                                            uint32_t& newMicLevel) {
        uint8_t msg_out[MSG_SIZE_BYTES];
        int ret = SoundLinkRecord( soundLink_, (const int16_t *)audioSamples, nSamples );
                
        return 0;
    }
    
    virtual int32_t NeedMorePlayData(
                                     const uint32_t nSamples,
                                     const uint8_t nBytesPerSample,
                                     const uint8_t nChannels,
                                     const uint32_t sampleRate,
                                     void* audioSamples,
                                     uint32_t& nSamplesOut,
                                     int64_t* elapsed_time_ms,
                                     int64_t* ntp_time_ms) {
        if(nChannels == 2){
            int16_t SoundLinkbuf[nSamples], *ptr;
            SoundLinkPlay( soundLink_, SoundLinkbuf, nSamples );
            ptr = (int16_t *)audioSamples;
            for( int i = 0; i < nSamples; i++){
                ptr[2*i] = SoundLinkbuf[i];
                ptr[2*i+1] = 0;
            }
        } else{
            SoundLinkPlay( soundLink_, (int16_t *)audioSamples, nSamples );
        }        
#if 0 // for debugging playout routing and volumen enable playing an audible tone
        float delta_omega = 0.0674f;// * 12*(10/3);
        int16_t* buf = (int16_t *)audioSamples;
        for( int i = 0; i < nSamples; i++){
            buf[i] = (int16_t)(sinf(_omega) * 8000.0f);
            _omega += delta_omega;
        }
        _omega = fmod(_omega, 2*3.1415926536);
#endif
        nSamplesOut = nSamples;
        return 0;
    }
    
    virtual int OnDataAvailable(const int voe_channels[],
                                int number_of_voe_channels,
                                const int16_t* audio_data,
                                int sample_rate,
                                int number_of_channels,
                                int number_of_frames,
                                int audio_delay_milliseconds,
                                int current_volume,
                                bool key_pressed,
                                bool need_audio_processing) {
        return 0;
    }
    
    virtual void PushCaptureData(int voe_channel, const void* audio_data,
                                 int bits_per_sample, int sample_rate,
                                 int number_of_channels,
                                 int number_of_frames) {}
    
    virtual void PullRenderData(int bits_per_sample, int sample_rate,
                                int number_of_channels, int number_of_frames,
                                void* audio_data,
                                int64_t* elapsed_time_ms,
                                int64_t* ntp_time_ms) {}
    
    int InitSoundLink(int Fs, const std::vector<uint8_t> *msg_in){
        int ret = Init_SoundLink( soundLink_, 1, Fs, Fs, msg_in);
        
        RegisterDetectionCallback(soundLink_, this);
        
        return ret;
    }
    
    int InitDelayEstimator(int Fs){
        int ret = Init_SoundLink( soundLink_, 0, Fs, Fs, NULL);
        
        RegisterDetectionCallback(soundLink_, this);
        
        return ret;
    }
    
    //const unsigned char* GetSelfMessage(){
    //    return SoundLinkGetSelfMessage(soundLink_);
    //}
    
    void SlDumpStart(){
        SoundLinkDumpStart( soundLink_, "/sdcard");
    }
    
    void SlDumpStop(){
        SoundLinkDumpStop( soundLink_);
    }
    
    void DetectedSoundLink(const std::vector<uint8_t> &msg,
                           const struct tm timeLastDetected,
                           const int     deviceRoundTripLatencyMs)
    {
        char tmp_time[80], tmp_msg[msg.size()];
        strftime(tmp_time, sizeof(tmp_time), "%Y-%m-%d.%X", &timeLastDetected);
        
        for( int i = 0; i < msg.size(); i++){
            tmp_msg[i] = msg[i];
        }
        
        LOGI("Detected : time %s msg %s deviceRoundTripLatencyMs = %d \n", tmp_time, tmp_msg, deviceRoundTripLatencyMs);

        // Pass on to Java Callback
        c2java_call_method( tmp_time, tmp_msg, msg.size(), deviceRoundTripLatencyMs );
    }
    
private:
    void *soundLink_;
    float _omega;
};
/**********************************************************************/

class VoELogCallback : public webrtc::TraceCallback {
public:
    VoELogCallback() {};
    virtual ~VoELogCallback() {};
    
    virtual void Print(webrtc::TraceLevel lvl, const char* message,
                       int length) override
    {
        LOGI("%s\n", message);
    };
};

static VoELogCallback logCb;

/**********************************************************************/
/* Interface functions to be called from java                         */
/**********************************************************************/
static AudioDeviceModule  *AudioDevice = NULL;
static SoundLinkAudioTransport *AudioTransport = NULL;
static JavaVM *jvm;
static int sample_rate;
bool is_recording = false;
bool is_playing = false;
bool should_play = false;
bool should_record = false;

extern "C" {

// Using the Android audio interface requires all modifications to be made on the same thread.
// The marshalling has 3 states
// 1) API_CALL_BEGIN.  The wanted settings can be updated from any thread
// 2) RUN_WORKER.      The execution of the update happens on a seperate worker thread
// 3) API_CALL_RETURN. The worker thread has changed the settings and the API function can return
// In this way from the calling thread it looks like the update hapend syncronous as the change has been done before the function returned
enum SLctrlState{
    API_CALL_BEGIN,    // A calling function can update the wanted state
    RUN_WORKER,        // Update the state on the worker thread
    API_CALL_RETURN,   // Updating has finished calling function can return
    STOPPED
};
    
SLctrlState State = API_CALL_BEGIN;
pthread_mutex_t mutex;
pthread_cond_t cond12, cond23, cond31;
pthread_t tid;
uint32_t start_volumen;
 
void create_and_init(){
    if(AudioDevice == NULL){
        AudioDevice = CreateAudioDeviceModule(0, webrtc::AudioDeviceModule::kPlatformDefaultAudio);
        LOGI("CreateAudioDeviceModule \n");
    }
    // Register Processing Callbacks Callbacks
    AudioDevice->RegisterAudioCallback(AudioTransport);
    LOGI("AudioDevice->RegisterAudioCallback() \n");
    
    AudioDevice->Init();
    LOGI("AudioDevice->Init() \n");
}

void terminate_and_free(){
    AudioDevice->RegisterAudioCallback(NULL);
    LOGI("AudioDevice->DeRegisterAudioCallback() \n");

    AudioDevice->Terminate();
    LOGI("AudioDevice->Terminate() \n");
    
    //delete AudioDevice;
    AudioDevice = NULL;
    LOGI("delete AudioDevice \n");
}
    
void *sound_link_ctrl_thread(void *arg)
{
    is_recording = false;
    is_playing = false;
    
    if(AudioTransport == NULL){
        AudioTransport = new SoundLinkAudioTransport(AudioDevice);
    }
    
    do{
        pthread_mutex_lock(&mutex);
        while(State != RUN_WORKER && State != STOPPED){
            pthread_cond_wait(&cond12, &mutex);
        }
        if(State == STOPPED){
            pthread_mutex_unlock(&mutex);
            break;
        }
        if(should_play && !is_playing){
            /* Start Playing */
            if(!is_recording){
                create_and_init();
            }
            AudioDevice->SetPlayoutDevice(0);
            AudioDevice->InitPlayout();
            LOGI("AudioDevice->InitPlayout() \n");
            AudioDevice->StartPlayout();
            LOGI("AudioDevice->StartPlayout() \n");
                
            AudioDevice->SpeakerVolume(&start_volumen);
            LOGI("start_volumen = %d \n", start_volumen);
                
            uint32_t volumen;
            AudioDevice->MaxSpeakerVolume(&volumen);
            LOGI("MaxSpeakerVolume = %d \n", volumen);
                
            /* Set volumen to 75% of maximum */
            //volumen = (volumen * 3) >> 2;
            if( volumen > 0){
                LOGI("SetSpeakerVolume to %d \n", volumen);
                AudioDevice->SetSpeakerVolume(volumen);
            }
                
            is_playing = true;
        }
        if(!should_play && is_playing){
            /* Stop Playing */
            if(is_playing){
                if( start_volumen > 0){
                    LOGI("SetSpeakerVolume to %d \n", start_volumen);
                    AudioDevice->SetSpeakerVolume(start_volumen);
                }
                AudioDevice->StopPlayout();
                LOGI("AudioDevice->StopPlayout() \n");
                    
                is_playing = false;
                if(!is_recording){
                    terminate_and_free();
                }
            }
        }
        if(should_record && !is_recording){
            /* Start record */
            if(!is_playing){
                create_and_init();
            }
            AudioDevice->SetRecordingDevice(0);
            AudioDevice->InitRecording();
            LOGI("AudioDevice->InitRecording() \n");
            AudioDevice->StartRecording();
            LOGI("AudioDevice->StartRecording() \n");
                
            is_recording = true;
        }
        if(!should_record && is_recording){
            /* Stop record */
            if(is_recording){
                LOGI("AudioDevice->StopRecording() \n");
                AudioDevice->StopRecording();
                
                is_recording = false;
                if(!is_playing){
                    terminate_and_free();
                }
            }
        }
        State = API_CALL_RETURN;
        pthread_mutex_unlock(&mutex);
        
        /* Wakeup the API calling thread so it can terminate the function */
        pthread_cond_signal(&cond23);
    }while(1);
    return NULL;
}
    
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_createEngine(JNIEnv* env, jclass clazz, jobject context, int Fs, int bufSize)
{
    JavaVM* vm = NULL;
    
    jint res = env->GetJavaVM(&jvm);
    
    webrtc::VoiceEngine::SetAndroidObjects(jvm, context);
    
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond12, NULL);
    pthread_cond_init(&cond23, NULL);
    pthread_cond_init(&cond31, NULL);
    
    sample_rate = Fs;
    
    State = API_CALL_BEGIN;
    
    should_play = false;
    should_record = false;
    
    int ret = pthread_create(&tid, NULL, sound_link_ctrl_thread, NULL);
    
    LOGI("Engine created \n");
}

JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_startRecord(JNIEnv* env, jclass clazz)
{
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_BEGIN){
        pthread_cond_wait(&cond31, &mutex);
    }

    should_record = true;
    LOGI("StartSend() should_record = %d is_recording = %d \n", should_record, is_recording);
    
    State = RUN_WORKER;
    pthread_mutex_unlock(&mutex);
    
    /* wakeup the worker thread */
    pthread_cond_signal(&cond12);
    
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_RETURN){
        pthread_cond_wait(&cond23, &mutex); // While it waits it releases the mutex
    }
    State = API_CALL_BEGIN;
    pthread_mutex_unlock(&mutex);
    
    pthread_cond_signal(&cond31);
}
    
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_stopRecord(JNIEnv* env, jclass clazz)
{
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_BEGIN){
        pthread_cond_wait(&cond31, &mutex);
    }
    
    should_record = false;
    LOGI("StartSend() should_record = %d is_recording = %d \n", should_record, is_recording);
    
    State = RUN_WORKER;
    pthread_mutex_unlock(&mutex);
    
    /* wakeup the worker thread */
    pthread_cond_signal(&cond12);
    
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_RETURN){
        pthread_cond_wait(&cond23, &mutex);
    }
    State = API_CALL_BEGIN;
    pthread_mutex_unlock(&mutex);
    
    pthread_cond_signal(&cond31);
}
    
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_startPlayout(JNIEnv* env, jclass clazz)
{
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_BEGIN){
        pthread_cond_wait(&cond31, &mutex);
    }
    
    should_play = true;
    LOGI("StartSend() should_record = %d is_recording = %d \n", should_record, is_recording);
    
    State = RUN_WORKER;
    pthread_mutex_unlock(&mutex);
    
    /* wakeup the worker thread */
    pthread_cond_signal(&cond12);
    
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_RETURN){
        pthread_cond_wait(&cond23, &mutex);
    }
    State = API_CALL_BEGIN;
    pthread_mutex_unlock(&mutex);
    
    pthread_cond_signal(&cond31);
}
    
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_stopPlayout(JNIEnv* env, jclass clazz)
{
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_BEGIN){
        pthread_cond_wait(&cond31, &mutex);
    }
    
    should_play = false;
    LOGI("StartSend() should_record = %d is_recording = %d \n", should_record, is_recording);
    
    State = RUN_WORKER;
    pthread_mutex_unlock(&mutex);
    
    /* wakeup the worker thread */
    pthread_cond_signal(&cond12);
    
    pthread_mutex_lock(&mutex);
    while(State != API_CALL_RETURN){
        pthread_cond_wait(&cond23, &mutex);
    }
    State = API_CALL_BEGIN;
    pthread_mutex_unlock(&mutex);
    
    pthread_cond_signal(&cond31);
}

// shut down the native audio system
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_shutdown(JNIEnv* env, jclass clazz)
{
    if(AudioDevice != NULL){
        if(is_playing || is_recording){
            AudioDevice->Terminate();
        }
        
        // Does AudioDevice Leak here ?
        AudioDevice = NULL;
    }
    
    if(AudioTransport != NULL){
        delete AudioTransport;
        
        AudioTransport = NULL;
    }
    
    pthread_mutex_lock(&mutex);
    State = STOPPED;
    pthread_mutex_unlock(&mutex);
    
    pthread_cond_signal(&cond12);
    
    pthread_join(tid, NULL);
    
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond12);
    pthread_cond_destroy(&cond23);
    pthread_cond_destroy(&cond31);
}

#if 0
JNIEXPORT jstring JNICALL Java_com_waz_soundlink_SoundLinkAPI_GetSelfMessage(JNIEnv* env, jclass clazz)
{
    jstring j_test;
    const unsigned char* ptr;
    char buf[MSG_SIZE_BYTES*2 + 1];

    ptr = AudioTransport->GetSelfMessage();
    for( int i = 0; i < MSG_SIZE_BYTES; i++ ){
        sprintf(&buf[2*i], "%.2x", ptr[i]);
    }
    
    j_test = env->NewStringUTF(buf);
        
    return(j_test);
}
#endif
    
JNIEXPORT int JNICALL Java_com_waz_soundlink_SoundLinkAPI_initSoundLink(JNIEnv* env, jclass clazz, jbyteArray message)
{
    int ret;
    jsize lengthOfArray;
    if(message == NULL){
        lengthOfArray = 0;
    } else {
        lengthOfArray = env->GetArrayLength(message);
    }
    
    if(lengthOfArray == 0){
        ret = AudioTransport->InitSoundLink(sample_rate, NULL);
    } else {
        jbyte* nativeMessage = env->GetByteArrayElements(message, 0);
    
        std::vector<uint8_t> msg(lengthOfArray);
    
        for( int i = 0; i < lengthOfArray; i++ ){
            msg[ i ] = nativeMessage[ i ];
        }
    
        ret = AudioTransport->InitSoundLink(sample_rate, &msg);
    
        env->ReleaseByteArrayElements(message, nativeMessage, 0);
    }
    
    return ret;
}

JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_initDelayEstimator(JNIEnv* env, jclass clazz)
{
    AudioTransport->InitDelayEstimator(sample_rate);
}

JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_soundLinkDumpStart(JNIEnv* env, jclass clazz)
{
    AudioTransport->SlDumpStart();
}
        
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_attach(JNIEnv *env, jobject self, jobject ctx)
{
    if (!c2java.initialized) {
        c2java_init(env, self, ctx);
    }
    
    c2java.self = env->NewGlobalRef(self);
}
    
JNIEXPORT void JNICALL Java_com_waz_soundlink_SoundLinkAPI_detach(JNIEnv *env, jobject self)
{
    c2java.self = NULL;
    env->DeleteGlobalRef(c2java.self);
}
    
}    // extern C
/**********************************************************************/

/**********************************************************************/
/* Helper functionality to call java from c/c+                        */
/**********************************************************************/
static int c2java_init(JNIEnv *env, jobject jobj, jobject ctx)
{
    jint res;
    jclass cls;
    int err = 0;
    
    res = env->GetJavaVM(&c2java.vm);
    if (res != JNI_OK) {
        LOGI("jni: sound_link: no Java VM\n");
        err = -1;
        return err;
        //goto out;
    }
    
    cls = env->GetObjectClass(jobj);
    if (cls == NULL) {
        LOGI("GetObjectClass failed \n");
        err = -2;
        return err;
        //goto out;
    }
    
    c2java.sldmid = env->GetMethodID(cls, "callBackCaller",
                                     "(Ljava/lang/String;"
                                     "[BI)V");
    
    c2java.initialized = true;
    
    //out:
    LOGI("Leaving init.\n");
    return err;
}

static int c2java_jni_attach(struct jni_env *je)
{
    int res;
    int err = 0;
    
    je->attached = false;
    res = c2java.vm->GetEnv((void **)&je->env, JNI_VERSION_1_6);
    
    if (res != JNI_OK || je->env == NULL) {
#ifdef ANDROID
        res = c2java.vm->AttachCurrentThread(&je->env, NULL);
#else
        res = c2java.vm->AttachCurrentThread((void **)&je->env, NULL);
#endif
        
        if (res != JNI_OK) {
            err = -1;//ENOSYS;
            goto out;
        }
        je->attached = true;
    }
    
out:
    return err;
}

static void c2java_jni_detach(struct jni_env *je)
{
    if (je->attached) {
        c2java.vm->DetachCurrentThread();
    }
}

static void c2java_call_method( const char* tmp_time, const char* tmp_msg, int msg_len, int deviceRoundTripLatencyMs )
{
    struct jni_env je;
    int err = 0;
    
    err = c2java_jni_attach(&je);
    
    jstring jtime = je.env->NewStringUTF(tmp_time);
    
    //fill the buffer
    jbyteArray jBuff = je.env->NewByteArray(msg_len);
    je.env->SetByteArrayRegion(jBuff, 0, msg_len, (jbyte*) tmp_msg);
    
    je.env->CallVoidMethod(c2java.self, c2java.sldmid, jtime, jBuff, deviceRoundTripLatencyMs);
    
    // Should jbyteArray be released somehow ?
    je.env->DeleteLocalRef(jtime);
    je.env->DeleteLocalRef(jBuff);
    
    c2java_jni_detach(&je);
}
/**********************************************************************/


