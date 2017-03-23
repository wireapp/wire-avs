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

#include "re.h"
#include "avs.h"
#include "mm_platform.h"
#include "mm_platform_android.h"
#include "avs_mediamgr.h"

#include <pthread.h>
#include <sys/time.h>

static struct {
    bool initialized;
    int err;
    JavaVM *vm;
    jfieldID mmfid;
    jobject ctx;
    jclass mmClass;
    jobject mm;
    jmethodID mmOnPlaybackRouteChanged;
    jmethodID mmOnMediaCategoryChanged;
    jmethodID mmOnEnterCall;
    jmethodID mmOnExitCall;
    jclass arClass;
    jmethodID arConstructor;
    jmethodID arEnableSpeaker;
    jmethodID arEnableHeadset;
    jmethodID arEnableEarpiece;
    jmethodID arEnableBTSco;
    jmethodID arGetRoute;
    jobject router;
    jclass mpClass;
    jmethodID mpPlay;
    jmethodID mpStop;
    jmethodID mpSetShouldLoop;
    jmethodID mpSetVolumen;
    jmethodID mpGetIsPlaying;
} java = {
    .initialized = false,
    .err = 0,
    .vm = NULL,
    .mmfid = NULL,
    .ctx = NULL,
    .mmClass = NULL,
    .mm = NULL,
    .mmOnPlaybackRouteChanged = NULL,
    .mmOnMediaCategoryChanged = NULL,
    .mmOnEnterCall = NULL,
    .mmOnExitCall = NULL,
    .arClass = NULL,
    .arConstructor = NULL,
    .router = NULL,
    .mpClass = NULL,
    .mpPlay = NULL,
    .mpStop = NULL,
    .mpSetShouldLoop  = NULL,
    .mpSetVolumen = NULL,
    .mpGetIsPlaying = NULL
};

struct jni_env {
    bool attached;
    JNIEnv *env;
};

static void callVoidMethodHelper(JNIEnv * jEnv,
                                 jobject java_object,
                                 jmethodID java_method,
                                 ...)
{
    va_list args;
    va_start(args, java_method);

    (*jEnv)->CallVoidMethodV(jEnv, java_object, java_method, args);
    
    va_end(args);
}

static int callIntMethodHelper(JNIEnv * jEnv,
                               jobject java_object,
                               jmethodID java_method,
                               ...)
{
    va_list args;
    va_start(args, java_method);
    
    jint res = (*jEnv)->CallIntMethodV(jEnv, java_object, java_method, args);
    
    va_end(args);
    
    return (int)res;
}

static bool callBoolMethodHelper(JNIEnv * jEnv,
                                 jobject java_object,
                                 jmethodID java_method,
                                 ...)
{
    va_list args;
    va_start(args, java_method);
    
    jboolean res = (*jEnv)->CallBooleanMethodV(jEnv, java_object, java_method, args);
    
    va_end(args);
    
    return (bool)res;
}

static int jni_attach(struct jni_env *je)
{
    int res;
    int err = 0;
    
    je->attached = false;

	if (!java.vm)
		return ENOSYS;

    res = (*java.vm)->GetEnv(java.vm, (void **)&je->env, JNI_VERSION_1_6);
    
    if (res != JNI_OK || je->env == NULL) {
#ifdef ANDROID
        res = (*java.vm)->AttachCurrentThread(java.vm, &je->env, NULL);
#else
        res = (*java.vm)->AttachCurrentThread(java.vm, (void **)&je->env, NULL);
#endif
        
        if (res != JNI_OK) {
            error("mm:GetAttachCurrentThread failed \n");
            err = ENOSYS;
            goto out;
        }
        je->attached = true;
    }else {
        debug("mm: GetEnv managed to get java env = %p \n", je->env);
    }
    
out:
    return err;
}

static void jni_detach(struct jni_env *je)
{
    if (je->attached) {
        (*java.vm)->DetachCurrentThread(java.vm);
    }
}

bool mm_android_jni_java_get_initialized()
{
    return java.initialized;
}

jfieldID mm_android_jni_java_get_fid()
{
    return java.mmfid;
}

static jobject cbgp_jni_new(JNIEnv * jEnv,
                            jclass jcObject,
                            jmethodID jmObject,
                            const char * pcConstr,
                            ...)
{
    va_list ap;
    jobject joNew = NULL;
    
    va_start(ap, pcConstr);
    
    /* Build new object... */
    joNew= (*jEnv)->NewObjectV(jEnv, jcObject, jmObject, ap);
    if (joNew == NULL){
        error("Could not instantiate class \n");
    }
    
    va_end(ap);
    
    return joNew;
}

static void nativeUpdateRoute(JNIEnv* env, jobject obj, jint route, jlong nativeMM) {
    info("mm: nativeUpdateRoute to %d \n", route);
    
    callVoidMethodHelper(env, java.mm, java.mmOnPlaybackRouteChanged, route);
}

static void nativeHeadsetConnected(JNIEnv* env, jobject obj, jboolean connected, jlong nativeMM) {
    struct mm *mm = (struct mm *)nativeMM;
    
    mediamgr_headset_connected(mm, connected);
}

static void nativeBTDeviceConnected(JNIEnv* env, jobject obj, jboolean connected, jlong nativeMM) {
    struct mm *mm = (struct mm *)nativeMM;
    
    mediamgr_bt_device_connected(mm, connected);
}

static JNINativeMethod methods[] = {
    {"nativeUpdateRoute", "(IJ)V", (void*)nativeUpdateRoute },
    {"nativeHeadsetConnected", "(ZJ)V", (void*)nativeHeadsetConnected },
    {"nativeBTDeviceConnected", "(ZJ)V", (void*)nativeBTDeviceConnected },
};

int mm_android_jni_init(JNIEnv *env, jobject jobj, jobject ctx)
{
    jint res;
    jclass cls;
    int err = 0;
    
    info("jni: init\n");
    
    res = (*env)->GetJavaVM(env, &java.vm);
    if (res != JNI_OK) {
        warning("jni: call_manager: no Java VM\n");
        err = ENOSYS;
        goto out;
    }
    
    debug("jni attach env = %p \n", env);
    
    cls = (*env)->GetObjectClass(env, jobj);
    if (cls == NULL) {
        err = ENOENT;
        goto out;
    }
    java.mm = ((*env)->NewGlobalRef(env, jobj));
    java.mmClass = ((*env)->NewGlobalRef(env, cls));
    
    java.ctx = (jobject)((*env)->NewGlobalRef(env, ctx));
    
    jmethodID jmObject;
    if ((jmObject= (*env)->GetMethodID(env, cls, "onPlaybackRouteChanged", "(I)V")) == NULL){
        error("mm: Could not get onPlaybackRouteChanged method ID \n");
    }
    java.mmOnPlaybackRouteChanged = jmObject;

    if ((jmObject= (*env)->GetMethodID(env, cls, "onMediaCategoryChanged", "(Z)V")) == NULL){
        error("mm: Could not get onMediaCategoryChanged method ID \n");
    }
    java.mmOnMediaCategoryChanged = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, cls, "onEnterCall", "()V")) == NULL){
        error("mm: Could not get onEnterCall method ID \n");
    }
    java.mmOnEnterCall = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, cls, "onExitCall", "()V")) == NULL){
        error("mm: Could not get onExitCall method ID \n");
    }
    java.mmOnExitCall = jmObject;
    
    /* Get the MediaPlayer class */
    jclass jcObject;
    if ((jcObject= (*env)->FindClass(env, "com/waz/media/manager/player/MediaPlayer")) == NULL){
        error("mm: Could not find class com/waz/media/manager/player/MediaPlayer \n");
    }
    java.mpClass = (jclass)((*env)->NewGlobalRef(env, jcObject));

    if ((jmObject= (*env)->GetMethodID(env, jcObject, "play", "()V")) == NULL){
        error("mm: Could not get play method ID \n");
    }
    java.mpPlay = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "stop", "()V")) == NULL){
        error("mm: Could not get stop method ID \n");
    }
    java.mpStop = jmObject;

    if ((jmObject= (*env)->GetMethodID(env, jcObject, "setShouldLoop", "(Z)V")) == NULL){
        error("mm: Could not get setShouldLoop method ID \n");
    }
    java.mpSetShouldLoop = jmObject;

    if ((jmObject= (*env)->GetMethodID(env, jcObject, "setVolume", "(F)V")) == NULL){
        error("mm: Could not get setShouldLoop method ID \n");
    }
    java.mpSetVolumen = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "getIsPlaying", "()Z")) == NULL){
        error("mm: Could not get getIsPlaying method ID \n");
    }
    java.mpGetIsPlaying = jmObject;
    
    /* Get the router class */
    if ((jcObject= (*env)->FindClass(env, "com/waz/media/manager/router/AudioRouter")) == NULL){
        error("mm: Could not find class com/waz/media/manager/router/AudioRouter \n");
    }
    java.arClass = (jclass)((*env)->NewGlobalRef(env, jcObject));

    if ((*env)->RegisterNatives(env, jcObject, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
        error("mm: RegisterNatives failed !! \n");
    }
    
    /* Get the constructor's method ID */
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "<init>", "(Landroid/content/Context;J)V")) == NULL){
        error("mm: Could not get AudioRouter() method ID \n");
    }
    java.arConstructor = jmObject;
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "EnableSpeaker", "()I")) == NULL){
        error("mm: Could not get EnableSpeaker method ID \n");
    }
    java.arEnableSpeaker = jmObject;

    if ((jmObject= (*env)->GetMethodID(env, jcObject, "EnableHeadset", "()I")) == NULL){
        error("mm: Could not get EnableHeadset method ID \n");
    }
    java.arEnableHeadset = jmObject;

    if ((jmObject= (*env)->GetMethodID(env, jcObject, "EnableEarpiece", "()I")) == NULL){
        error("mm: Could not get EnableEarpiece method ID \n");
    }
    java.arEnableEarpiece = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "EnableBTSco", "()I")) == NULL){
        error("mm: Could not get EnableBTSco method ID \n");
    }
    java.arEnableBTSco = jmObject;
    
    if ((jmObject= (*env)->GetMethodID(env, jcObject, "GetAudioRoute", "()I")) == NULL){
        error("mm: Could not get GetAudioRoute method ID \n");
    }
    java.arGetRoute = jmObject;
    
    java.initialized = true;
    
out:
    if (err)
        error("mm: jni: init failed\n");
    else
        info("mm: jni: init done\n");
    
    return err;
}

void mm_android_jni_cleanup(JNIEnv *env, jobject jobj){
    (*env)->DeleteGlobalRef(env, java.mm);
    (*env)->DeleteGlobalRef(env, java.mmClass);
    (*env)->DeleteGlobalRef(env, java.ctx);
    (*env)->DeleteGlobalRef(env, java.mpClass);
    (*env)->DeleteGlobalRef(env, java.arClass);
    
    java.initialized = false;
}

static void jni_create_router(struct mm *mm)
{
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    jobject jrouter = cbgp_jni_new( jni_env.env, java.arClass, java.arConstructor, "(Landroid/content/Context;)V", java.ctx, (jlong)mm);
    java.router = (*jni_env.env)->NewGlobalRef(jni_env.env, jrouter);
    debug("mm: jni_create_router called !! \n");
    
    jni_detach(&jni_env);
}

static void jni_free_router(struct mm *mm)
{
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    
    (*jni_env.env)->DeleteGlobalRef(jni_env.env, java.router);
    
    jni_detach(&jni_env);
}

int mm_platform_init(struct mm *mm, struct dict *sounds)
{
    jni_create_router(mm);
    
    dict_flush(sounds);
    
	return 0;
}

int mm_platform_free(struct mm *mm)
{
    jni_free_router(mm);
    
    return 0;
}

void mm_platform_registerMedia(struct dict *sounds,
                               const char *name,
                               void *mediaObj,
                               bool mixing,
                               bool incall,
                               int intensity,
                               int priority,
                               bool is_call_media)
{
    debug("mm: mm_platform_registerMedia name = %s \n", name);
    
    struct sound *snd;
    snd = mem_zalloc(sizeof(struct sound), NULL);
    
    snd->arg = mediaObj;
    snd->mixing = mixing;
    snd->incall = incall;
    snd->intensity = intensity;
    snd->priority = priority;
    snd->is_call_media = is_call_media;
    
    dict_add( sounds, name, (void*)snd);
    mem_deref(snd); // to get the ref count to 1
}

void mm_platform_unregisterMedia(struct dict *sounds, const char *name){
    struct sound *snd;

    debug("mm: mm_platform_unregisterMedia = %s \n", name);
    
    snd = dict_lookup(sounds, name);
    if(!snd){
        return;
    }
    
    struct jni_env jni_env;
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }

    snd = dict_lookup(sounds, name);
    
    (*jni_env.env)->DeleteGlobalRef(jni_env.env, (jobject)snd->arg);
    
    dict_remove(sounds, name);
    
    jni_detach(&jni_env);
}

void mm_platform_play_sound(struct sound *snd)
{
    debug("mm: mm_platform_play_sound obj = %p \n", (void*)snd->arg);
    
    jobject java_player = (jobject)snd->arg;
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    callVoidMethodHelper(jni_env.env, java_player, java.mpPlay);
    
    jni_detach(&jni_env);
}

void mm_platform_pause_sound(struct sound *snd)
{
}

void mm_platform_resume_sound(struct sound *snd)
{
}

void mm_platform_stop_sound(struct sound *snd)
{
    jobject java_player = (jobject)snd->arg;
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    callVoidMethodHelper(jni_env.env, java_player, java.mpStop);
    
    jni_detach(&jni_env);
}

bool mm_platform_is_sound_playing(struct sound *snd)
{
    jobject java_player = (jobject)snd->arg;
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return false;
    }
    bool ret = callBoolMethodHelper(jni_env.env, java_player, java.mpGetIsPlaying);
    
    jni_detach(&jni_env);
    
    return ret;
}

int mm_platform_enable_speaker(void){
    debug("mm: mm_platform_enable_speaker() \n");

    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return -1;
    }
    int ret = callIntMethodHelper(jni_env.env, java.router, java.arEnableSpeaker);
    
    jni_detach(&jni_env);
    
    return ret;
}

int mm_platform_enable_headset(void){
    debug("mm: mm_platform_enable_headset() \n");
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return -1;
    }
    int ret = callIntMethodHelper(jni_env.env, java.router, java.arEnableHeadset);
    
    jni_detach(&jni_env);
    
    return ret;
}

int mm_platform_enable_earpiece(void){
    debug("mm: mm_platform_enable_earpiece() \n");
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return -1;
    }
    int ret = callIntMethodHelper(jni_env.env, java.router, java.arEnableEarpiece);
    
    jni_detach(&jni_env);
    
    return ret;
}

int mm_platform_enable_bt_sco(void){
    debug("mm: mm_platform_enable_bt_sco() \n");
    
    struct jni_env jni_env;
    
    // Android Hack wait 500 ms otherwise we will turn on too early if the BT device was just connected
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 500 * 1000 * 1000;
    nanosleep(&t, NULL);
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return -1;
    }
    int ret = callIntMethodHelper(jni_env.env, java.router, java.arEnableBTSco);
    
    jni_detach(&jni_env);
    
    return ret;
}

void mm_android_jni_on_playback_route_changed(enum mediamgr_auplay new_route, void *arg){
    debug("mm: mm_android_jni_on_playback_route_changed(%d) \n", (int)new_route);
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    callVoidMethodHelper(jni_env.env, java.mm, java.mmOnPlaybackRouteChanged, (int)new_route);
    
    jni_detach(&jni_env);
}

void mm_android_jni_on_media_category_changed(enum mediamgr_state state, void *arg){
    debug("mm: mm_android_jni_on_media_category_changed(%d) \n", (int)state);
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    
    bool mcat_is_call = false;
    if( state == MEDIAMGR_STATE_INCALL || state == MEDIAMGR_STATE_INVIDEOCALL){
        mcat_is_call = true;
    }
    
    callVoidMethodHelper(jni_env.env, java.mm, java.mmOnMediaCategoryChanged, mcat_is_call);
    
    jni_detach(&jni_env);
}

void mm_platform_enter_call(void){
    debug("mm: mm_platform_enter_call() \n");
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    callVoidMethodHelper(jni_env.env, java.mm, java.mmOnEnterCall);
    
    jni_detach(&jni_env);
}

void mm_platform_exit_call(void){
    debug("mm: mm_platform_exit_call() \n");
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return;
    }
    callVoidMethodHelper(jni_env.env, java.mm, java.mmOnExitCall);
    
    jni_detach(&jni_env);
}

void mm_platform_set_active(void){
}

enum mediamgr_auplay mm_platform_get_route(void){
    debug("mm: mm_platform_get_route \n");
    
    struct jni_env jni_env;
    
    if(jni_attach(&jni_env)){
        error("mm: %s jni_attach failed !! \n", __FUNCTION__);
        return MEDIAMGR_AUPLAY_UNKNOWN;
    }
    int ret = callIntMethodHelper(jni_env.env, java.router, java.arGetRoute);
    
    jni_detach(&jni_env);

    enum mediamgr_auplay route;
    switch(ret){
        case 0:
            route = MEDIAMGR_AUPLAY_EARPIECE;
            info("mm: route is Earpiece \n");
        break;
        
        case 1:
            route = MEDIAMGR_AUPLAY_SPEAKER;
            info("mm: route is Speaker \n");
        break;
        
        case 2:
            route = MEDIAMGR_AUPLAY_HEADSET;
            info("mm: route is Headset \n");
        break;
        
        case 3:
            route = MEDIAMGR_AUPLAY_BT;
            info("mm: route is BT \n");
        break;
        
        default:
            route = MEDIAMGR_AUPLAY_EARPIECE;
    }
    
    return route;
}
