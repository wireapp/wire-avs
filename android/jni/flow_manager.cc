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

#include <jni.h>
#ifdef ANDROID
#include <android/log.h>
#endif

#include "webrtc/voice_engine/include/voe_base.h"

#include <unistd.h>
#include <pthread.h>

#include <re/re.h>

#define DEBUG_MODULE "flow_manager"
#define DEBUG_LEVEL 7
#include <re/re_dbg.h>
#include <avs.h>
#include <avs_wcall.h>

#include <avs_vie.h>

#ifdef ANDROID
#ifdef USE_BREAKPAD
#include "breakpad/dumpcall.h"
#endif
#endif

/* JNI interface */
#include "com_waz_call_FlowManager.h"
#include "com_waz_avs_VideoCapturer.h"
#include "com_waz_avs_VideoRenderer.h"
#include "com_waz_audioeffect_AudioEffect.h"

#include "video_renderer.h"

#define THREAD_NAME "re_main"

struct jfm;

static struct {
	bool initialized;
	bool video_inited;
	struct jfm *jfm;
	int err;
	JavaVM *vm;
	jfieldID fmfid;
	jmethodID reqmid;
        jmethodID hemid;
        jmethodID ummid;
	jmethodID mestabmid;
	jmethodID confposmid;	
        jmethodID uvmid;
	jmethodID nqmid;

	/* video: */
	jmethodID csmid;
	jmethodID acsmid;
	jmethodID vszmid;
	
	struct {
		struct {
			jmethodID enter;
			jmethodID exit;
		} mid;

		struct video_renderer *renderer;
	} video;

	pthread_t tid;
	
	enum log_level log_level;

	jobject context;

	uint64_t avs_flags;
} java = {
	.initialized = false,
	.err = 0,
	.jfm = NULL,
	.vm = NULL,
	.fmfid = NULL,
	.reqmid = NULL,
	.mestabmid = NULL,
	.confposmid = NULL,
	.nqmid = NULL,

	/* video */
	.csmid = NULL,
	.vszmid = NULL,

	.video = {
		.renderer = NULL,
		.mid = {
			.enter = NULL,
			.exit = NULL,
		},
	},

	.acsmid = NULL,
    
	.log_level = LOG_LEVEL_INFO,

	.context = NULL,
	.avs_flags = 0,
};

struct jni_env {
	bool attached;
	JNIEnv *env;
};

struct jfm {
	struct flowmgr *fm;
	jobject self;

	struct {
		jobject handler;
		jmethodID appendmid;
		jmethodID uploadmid;
	} log;

	enum flowmgr_video_send_state video_state;
};


static int vie_jni_get_view_size_handler(const void *view, int *w, int *h);


static void jni_re_leave()
{		
	bool is_re;

	return;
	
	is_re = pthread_equal(pthread_self(), java.tid);
	if (is_re) {
		re_thread_leave();	
	}
}

static void jni_re_enter()
{
	bool is_re;

	return;
	
	is_re = pthread_equal(pthread_self(), java.tid);
	if (is_re) {
		re_thread_enter();	
	}
}

static void jni_log_handler(uint32_t lve, const char *msg)
{
#ifdef ANDROID
	int alp;
	switch (lve) {
	case LOG_LEVEL_DEBUG:
		alp = ANDROID_LOG_DEBUG;
		break;

	case LOG_LEVEL_INFO:
		alp = ANDROID_LOG_INFO;
		break;

	case LOG_LEVEL_WARN:
		alp = ANDROID_LOG_WARN;
		break;

	case LOG_LEVEL_ERROR:
		alp = ANDROID_LOG_ERROR;
		break;
	}

#ifdef AVS_LOG_DEBUG
	__android_log_write(alp, "avs", msg);
#endif
#endif

	if (java.log_level > lve)
		return;

	// XXX Call some SE function to log??
#ifdef ANDROID
	{
		#define LOG_MAX 1024
                #define min(a, b) ((a) < (b) ? (a) : (b))

		size_t slen = strlen(msg) + 1;
		if (slen > LOG_MAX) {
			char *split = (char *)mem_alloc(LOG_MAX+1, NULL);
			size_t nbytes;
			size_t pos = 0;
			
			do {
				nbytes = min(slen, LOG_MAX);
				memcpy(split, msg + pos, nbytes);
				split[nbytes] = '\0';
				__android_log_write(alp, "avs", split);
				slen -= nbytes;
				pos += nbytes;
			}
			while (slen > 0);

			mem_deref(split);
		}
		else {
			__android_log_write(alp, "avs", msg);
		}
	}
#endif
}

 
static struct log log = {
	.h = jni_log_handler 
};


static int jni_attach(struct jni_env *je)
{
	int res;
	int err = 0;

	je->attached = false;
	res = java.vm->GetEnv((void **)&je->env, JNI_VERSION_1_6);

	if (res != JNI_OK || je->env == NULL) {
#ifdef ANDROID
		res = java.vm->AttachCurrentThread(&je->env, NULL);
#else
		res = java.vm->AttachCurrentThread((void **)&je->env, NULL);
#endif

		if (res != JNI_OK) {
			err = ENOSYS;
			goto out;
		}
		je->attached = true;
	}

 out:
	return err;
}


static void jni_detach(struct jni_env *je)
{
	if (je->attached) {
		java.vm->DetachCurrentThread();
	}
}


static void log_append_handler(const char *msg, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	jstring jmsg;
	struct jni_env je;
	int err;

	if (!jfm->log.handler)
		return;
	
	if (!jfm->log.appendmid)
		return;

	err = jni_attach(&je);
	if (err) {
		warning("jni: fm_request: cannot attach to JNI\n");
		return;
	}

	jmsg = je.env->NewStringUTF(msg);
	jni_re_leave();
	je.env->CallVoidMethod(jfm->log.handler, jfm->log.appendmid, jmsg);
	jni_re_enter();

	jni_detach(&je);
}


static void log_upload_handler(void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	int err;
	
	if (!jfm->log.handler)
		return;

	err = jni_attach(&je);
	if (err) {
		warning("jni: fm_request: cannot attach to JNI\n");
		return;
	}
	
        jni_re_leave();
	je.env->CallVoidMethod(jfm->self, jfm->log.uploadmid);
        jni_re_enter();

	jni_detach(&je);
}


static void audio_change_state_handler(enum flowmgr_audio_receive_state state)
{
	struct jfm *jfm = java.jfm;
	struct jni_env je;
	jint jstate = 0;
	int err = 0;
    
	err = jni_attach(&je);
	if (err) {
		warning("%s: cannot attach JNI: %m\n", __func__, err);
		goto out;
	}
    
	switch(state) {
		case FLOWMGR_AUDIO_INTERRUPTION_STARTED:
			jstate = com_waz_call_FlowManager_AUDIO_INTERRUPTION_STARTED;
			break;
            
		case FLOWMGR_AUDIO_INTERRUPTION_STOPPED:
			jstate = com_waz_call_FlowManager_AUDIO_INTERRUPTION_STOPPED;
			break;
	}
    
	debug("change_state_handler: state=%d \n", jstate);
	je.env->CallVoidMethod(jfm->self, java.acsmid, jstate);
    
out:
	jni_detach(&je);
}

static void change_state_handler(enum flowmgr_video_receive_state state,
				 enum flowmgr_video_reason reason)
{
	struct jfm *jfm = java.jfm;
	struct jni_env je;
	jint jstate = 0;
	jint jreason = 0;
	int err = 0;

	err = jni_attach(&je);
	if (err) {
		warning("%s: cannot attach JNI: %m\n", __func__, err);
		goto out;
	}

	switch(state) {
	case FLOWMGR_VIDEO_RECEIVE_STOPPED:
		jstate = com_waz_call_FlowManager_VIDEO_STATE_STOPPED;
		break;

	case FLOWMGR_VIDEO_RECEIVE_STARTED:
		jstate = com_waz_call_FlowManager_VIDEO_STATE_STARTED;
		break;
	}

	switch(reason) {
	case FLOWMGR_VIDEO_NORMAL:
		jreason = com_waz_call_FlowManager_VIDEO_REASON_NORMAL;
		break;

	case FLOWMGR_VIDEO_BAD_CONNECTION:
		jreason = com_waz_call_FlowManager_VIDEO_REASON_BAD_CONNECTION;
		break;
	}

	debug("change_state_handler: state=%d reason=%d\n", jstate, jreason);
	je.env->CallVoidMethod(jfm->self, java.csmid, jstate, jreason);

 out:
	jni_detach(&je);	
}

void *flowmgr_thread(void *arg)
{
	int err;
    
#ifdef ANDROID
	pthread_setname_np(pthread_self(), THREAD_NAME);
#else
	pthread_setname_np(THREAD_NAME);
#endif
	/* Force the loading of wcall symbols! 
	 * Can't this be done with a linker directive?
	 */
	wcall_set_ice_servers(NULL, 0);

	err = libre_init();
	if (err) {
		warning("flowmgr_thread: libre_init failed (%m)\n", err);
		goto out;
	}

	debug("flowmgr_thread: using avs_flags=%llu", java.avs_flags);
	err = avs_init(java.avs_flags);
	if (err) {
		warning("flowmgr_thread: avs_init failed (%m)\n", err);
		goto out;
	}

#ifdef ANDROID
	err = flowmgr_init("voe", NULL, TLS_KEYTYPE_EC);
#else
	err = flowmgr_init("audummy", NULL, TLS_KEYTYPE_EC);
#endif
	if (err) {
		warning("flowmgr_thread: flowmgr_init failed (%m)\n", err);
		goto out;
	}
	java.initialized = true;

	re_main(NULL);
	flowmgr_close();
	avs_close();
	log_unregister_handler(&log);

 out:
	java.err = err;

	return NULL;
}


#ifdef ANDROID
#ifdef USE_BREAKPAD
static int setup_breakpad(JNIEnv *env, jobject ctx)
{	
        jclass clsCtx;
	jclass clsFile;
	jmethodID gfdmid;
	jmethodID gapmid;
	jobject jfile;
	jstring jpath;
	const char *path = NULL;

	clsCtx = env->GetObjectClass(ctx);
	clsFile = env->FindClass("java/io/File");
	
	if (clsCtx == NULL || clsFile == NULL) {
		warning("jni: setup_breakpad: missing class: ctx=%p file=%p\n",
			clsCtx, clsFile);
		return ENOSYS;
	}
	
	gfdmid = env->GetMethodID(clsCtx, "getFilesDir", "()Ljava/io/File;");
	gapmid = env->GetMethodID(clsFile, "getAbsolutePath",
				  "()Ljava/lang/String;");
	if (gfdmid == NULL || gapmid == NULL) {
		warning("jni: setup_breakpad: missing method: gfd=%p gap=%p\n",
			gfdmid, gapmid);
		return EPROTO;
	}

        jfile = env->CallObjectMethod(ctx, gfdmid);
	if (jfile == NULL) {
		warning("jni: setup_breakpad: no files dir\n");
		return ENOENT;
	}
	
	jpath = (jstring)env->CallObjectMethod(jfile, gapmid);	
	if (jpath)
		path = env->GetStringUTFChars(jpath, 0);

	debug("jni: setup_breakpad: jpath=%p path=%s\n", jpath, path);

	if (path) {
		hockey_register_breakpad(path);
		env->ReleaseStringUTFChars(jpath, path);
	}

	return 0;
}
#endif
#endif


static int init(JNIEnv *env, jobject jobj, jobject ctx, uint64_t avs_flags)
{
	jint res;
	jclass cls;
	jclass videoRendererCls;
	int err = 0;

	log_set_min_level(LOG_LEVEL_DEBUG);
	log_register_handler(&log);

	info("jni: init\n");
    
	res = env->GetJavaVM(&java.vm);
	if (res != JNI_OK) {
		warning("jni: call_manager: no Java VM\n");
		err = ENOSYS;
		goto out;
	}

	cls = env->GetObjectClass(jobj);
	if (cls == NULL) {
		err = ENOENT;
		goto out;
	}
	java.fmfid = env->GetFieldID(cls, "fmPointer", "J");
	if (java.fmfid == NULL) {
		warning("jni: call_manager: no fm_field\n");
		err = ENOENT;
		goto out;
	}

	java.reqmid = env->GetMethodID(cls, "request",
				       "(Ljava/lang/String;"
				       "Ljava/lang/String;"
				       "Ljava/lang/String;"
				       "[BJ)I");

	java.hemid = env->GetMethodID(cls, "handleError",
				       "(Ljava/lang/String;"
				       "I)V");

	java.ummid = env->GetMethodID(cls, "updateMode",
				      "(Ljava/lang/String;"
				      "I)V");
	java.mestabmid = env->GetMethodID(cls, "mediaEstablished",
					  "(Ljava/lang/String;)V");
	java.confposmid = env->GetMethodID(cls, "conferenceParticipants",
					   "(Ljava/lang/String;"
					   "[Ljava/lang/String;)V");
	java.uvmid = env->GetMethodID(cls, "updateVolume",
				      "(Ljava/lang/String;"
				      "Ljava/lang/String;"
				      "F)V");

#if 0 // XXX Don't try to get this method until its implemented
	java.nqmid = env->GetMethodID(cls, "networkQuality",
				      "ILjava/lang/String;F)V");
#endif

	/* Video: */
	java.csmid = env->GetMethodID(cls,
				      "changeVideoState",
				      "(II)V");
	java.vszmid = env->GetMethodID(cls,
				      "changeVideoSize",
				      "(II)V");
	

	videoRendererCls = env->FindClass("com/waz/avs/VideoRenderer");
	if (videoRendererCls == NULL) {
		warning("%s: cannot get VideoRenderer\n", __func__);
	}
	else {
		java.video.mid.enter = env->GetMethodID(videoRendererCls,
							"enter",
							"()Z");
		java.video.mid.exit = env->GetMethodID(videoRendererCls,
						      "exit",
						      "()V");
	}

	java.acsmid = env->GetMethodID(cls,
									"changeAudioState",
									"(I)V");
    
#ifdef ANDROID
	info("Calling SetAndroidObjects\n");
	webrtc::VoiceEngine::SetAndroidObjects(java.vm, ctx);

	java.context = env->NewGlobalRef(ctx);

#ifdef USE_BREAKPAD	
	setup_breakpad(env, ctx);
#endif
#endif
	java.avs_flags = avs_flags;
    
	err = pthread_create(&java.tid, NULL, flowmgr_thread, NULL);
	if (err) {
		error("jni: init: cannot create flomgr_thread (%m)\n", err);
		goto out;
	}

	while(!java.initialized && !java.err)
		usleep(50000);
	err = java.err;

 out:
	if (err)
		error("jni: init failed\n");
	else
		info("jni: init done\n");

	return err;
}


static void close(void)
{
	re_cancel();
}


struct jfm *self2fm(JNIEnv *env, jobject self)
{
	jlong ptr = env->GetLongField(self, java.fmfid);
	struct jfm *jfm = (struct jfm *)((void*)ptr); 
	
	return jfm;
}


int fm_request(struct rr_resp *ctx, const char *path, const char *method,
	       const char *ctype, const char *content, size_t clen, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	jboolean success;
	jstring jpath;
	jstring jmethod;
	jstring jctype;
	jbyteArray jcontent;
	struct jni_env je;
	jint ret;
	int err;

	if (!jfm)
		return EINVAL;

	err = jni_attach(&je);
	if (err) {
		warning("jni: fm_request: cannot attach to JNI\n");
		return err;
	}

	jpath = je.env->NewStringUTF(path);
	jmethod = je.env->NewStringUTF(method);
	jctype = je.env->NewStringUTF(ctype);
	jcontent = je.env->NewByteArray(clen);
	if (jcontent) {
		je.env->SetByteArrayRegion(jcontent, 0, clen,
					   (jbyte *)content);
	}

	jni_re_leave();
	err = (int)je.env->CallIntMethod(jfm->self, java.reqmid,
				         jpath, jmethod,
				         jctype, jcontent, (jlong)ctx);
	jni_re_enter();

	jni_detach(&je);

	return err;
}

static void media_estab_handler(const char *convid, bool estab, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jstring jconvid;
	int err = 0;

	err = jni_attach(&je);
	if (err) {
		warning("media_established: cannot attach JNI: %m\n", err);
		goto out;
	}

	jconvid = je.env->NewStringUTF(convid);

	debug("media_estab_handler: convid=%s estab=%d\n", convid, estab);
	//jni_re_leave();
	je.env->CallVoidMethod(jfm->self, java.mestabmid, jconvid);
	//	jni_re_enter();

 out:
	jni_detach(&je);
}

static void conf_pos_handler(const char *convid, struct list *partl, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jstring jconvid;
	jobjectArray jparts;
	struct le *le;
	int i = 0;
	int err = 0;

	err = jni_attach(&je);
	if (err) {
		warning("conf_pos_handler: cannot attach JNI: %m\n", err);
		goto out;
	}

	jconvid = je.env->NewStringUTF(convid);
	jparts = je.env->NewObjectArray(list_count(partl),
					je.env->FindClass("java/lang/String"),
					NULL);
	LIST_FOREACH(partl, le) {
		struct conf_part *cp = (struct conf_part *)le->data;

		je.env->SetObjectArrayElement(jparts, i,
					      je.env->NewStringUTF(cp->uid));
		++i;				      
	}

	jni_re_leave();
	je.env->CallVoidMethod(jfm->self, java.confposmid, jconvid, jparts);
	jni_re_enter();

 out:
	jni_detach(&je);
}	


static void mcat_handler(const char *convid, enum flowmgr_mcat cat, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jclass cls;
	jstring jconvid;
	jmethodID mid;
	int err = 0;

#ifndef ANDROID // Android simulator dosnt set this...
	flowmgr_mcat_changed(jfm->fm, convid, cat);
	return;
#endif
	
	err = jni_attach(&je);
	if (err) {
		warning("media_established: cannot attach JNI: %m\n", err);
		goto out;
	}

	jconvid = je.env->NewStringUTF(convid);

        jni_re_leave();
	je.env->CallVoidMethod(jfm->self, java.ummid, jconvid, (jint)cat);
        jni_re_enter();


 out:
	jni_detach(&je);
}


void err_handler(int err, const char *convid, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jclass cls;
	jstring jconvid;
	jmethodID mid;
	int ret = 0;

	ret = jni_attach(&je);
	if (ret) {
		warning("err_handler: cannot attach JNI: %m\n", ret);
		goto out;
	}

	jconvid = je.env->NewStringUTF(convid);

        jni_re_leave();
        je.env->CallVoidMethod(jfm->self, java.hemid, jconvid, (jint)err);
        jni_re_enter();

 out:
	jni_detach(&je);
}


void volume_handler(const char *convid,
		    const char *userid,
		    double invol, double outvol, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jclass cls;
	jstring jconvid;
	jstring juserid;
	jstring jself;
	jmethodID mid;
	int err = 0;

	err = jni_attach(&je);
	if (err) {
		warning("volume_handler: cannot attach JNI: %m\n", err);
		goto out;
	}

	jconvid = je.env->NewStringUTF(convid);
	juserid = je.env->NewStringUTF(userid);
	jself = je.env->NewStringUTF("self");

        jni_re_leave();
        je.env->CallVoidMethod(jfm->self, java.uvmid, jconvid, jself, invol);
        je.env->CallVoidMethod(jfm->self, java.uvmid,
			       jconvid, juserid, outvol);
        jni_re_enter();
    
    je.env->DeleteLocalRef(jconvid);
    je.env->DeleteLocalRef(juserid);
    je.env->DeleteLocalRef(jself);
    
 out:
	jni_detach(&je);

}


static void netq_handler(int nqerr, const char *convid, float q, void *arg)
{
	struct jfm *jfm = (struct jfm *)arg;
	struct jni_env je;
	jmethodID mid;
	int err;

	debug("netq_handler: err=%d convid=%s q=%f\n", nqerr, convid, q);

	err = jni_attach(&je);
	if (err) {
		warning("netq_handler: cannot attach JNI: %m\n", err);
		goto out;
	}

	if (java.nqmid) {
		jstring jconvid;

		jconvid = je.env->NewStringUTF(convid);
		je.env->CallVoidMethod(jfm->self, java.nqmid,
				       nqerr, jconvid, q);
		jni_re_enter();
	}

 out:
	jni_detach(&je);
}

static void vm_play_status_handler(bool is_playing, unsigned int cur_time_ms, unsigned int file_length_ms, void *arg)
{
    struct jfm *jfm = (struct jfm *)arg;
    struct jni_env je;
    jmethodID mid;
    int err;
    
    debug("vm_play_status_handler: is_playing = %d cur_time_ms = %d file_length_ms = %d \n", is_playing, cur_time_ms, file_length_ms);
    
    err = jni_attach(&je);
    if (err) {
        warning("vm_play_status_handler: cannot attach JNI: %m\n", err);
        goto out;
    }
        
out:
    jni_detach(&je);
}


static void video_state_handler(enum flowmgr_video_receive_state state,
				enum flowmgr_video_reason reason,
				void *arg)
{
	debug("video_state_change_handler: state=%d reason=%d\n",
	      state, reason);

	change_state_handler(state, reason);
}


static int render_frame_handler(struct avs_vidframe *vf, void *arg)
{
	struct video_renderer *vr = java.video.renderer;
	struct jni_env je;
	jobject jself;
	jboolean entered;
	int err;
	(void)arg;

#if 0
	debug("render_frame_handler: renderer=%p y/u/v=%p/%p/%p\n",
	      java.video.renderer, vf->y, vf->u, vf->v);
#endif
	
	if (!java.video.renderer) {
		return ENOSYS;
	}

	err = jni_attach(&je);
	if (err) {
		warning("jni: fm_request: cannot attach to JNI\n");
		return EFAULT;
	}
	
	jself = (jobject)video_renderer_arg(vr);
	if (!jself) {
		err = EIO;
		goto out;
	}
	
	entered = je.env->CallBooleanMethod(jself, java.video.mid.enter);
	if (entered) {
		err = video_renderer_handle_frame(vr, vf);
		
		je.env->CallVoidMethod(jself, java.video.mid.exit);
	}
 out:
	jni_detach(&je);

	return err;
}

static void audio_state_handler(enum flowmgr_audio_receive_state state,
								void *arg)
{
	audio_change_state_handler(state);
}

static void video_size_handler(int w, int h, void *arg)
{
	struct jfm *jfm = java.jfm;	
	struct jni_env je;
	int err;

	(void)arg;
	
	err = jni_attach(&je);
	if (err) {
		warning("jni: video_size_handlert: cannot attach to JNI\n");
		return;
	}

	je.env->CallVoidMethod(jfm->self, java.vszmid, w, h);
	
	jni_detach(&je);	
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_attach
(JNIEnv *env, jobject self, jobject ctx, jlong avs_flags)
{
	struct jfm *jfm;
	int err;

	if (!java.initialized) {
		init(env, self, ctx, (uint64_t)avs_flags);
	}

	jfm = (struct jfm *)mem_zalloc(sizeof(*jfm), NULL);
	if (!jfm) {
		err = ENOMEM;
		goto out;
	}

	jfm->self = env->NewGlobalRef(self);

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_alloc, &jfm->fm,
			    fm_request, err_handler, jfm);
	if (err) {
		warning("jni: call_manager: cannot allocate fm: %m\n", err);
		goto out;
	}
    
	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_media_handlers,
			     jfm->fm, mcat_handler, volume_handler, jfm);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_media_estab_handler,
			     jfm->fm, media_estab_handler, jfm);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_conf_pos_handler,
			     jfm->fm, conf_pos_handler, jfm);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_video_handlers, jfm->fm,
			     video_state_handler,
			     render_frame_handler,
			     video_size_handler,
			     jfm);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_audio_state_handler,
			     jfm->fm, audio_state_handler, jfm);
    
	env->SetLongField(self, java.fmfid, (jlong)((void *)jfm));

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_start);

	java.jfm = jfm;
	
 out:
	if (err) {
		mem_deref((void *)jfm);
	}
}


JNIEXPORT void JNICALL Java_com_waz_call_detach
(JNIEnv *env, jobject self)
{
	struct jfm *jfm = self2fm(env, self);
	jobject jobj;
	void *arg;
	
	jobj = jfm->self;
	jfm->self = NULL;
	env->DeleteGlobalRef(jobj);

	mem_deref(jfm);
}


JNIEXPORT jboolean JNICALL Java_com_waz_call_FlowManager_acquireFlows
(JNIEnv *env, jobject self, jstring jconvid, jstring jsessid)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *convid = env->GetStringUTFChars(jconvid, 0);
	const char *sessid = env->GetStringUTFChars(jsessid, 0);
	int err;

	debug("jni: acquire_flows: convid=%s sessid=%s\n", convid, sessid);

	if (fm == NULL) {
		warning("jni: acquireFlows: no flowmgr\n");
		return false;
	}

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_acquire_flows, fm,
			    convid, sessid, NULL, NULL);
			    //netq_handler, jfm); // Enable when 
	                                          // networkQuality exists

 out:
	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
	if (sessid)
		env->ReleaseStringUTFChars(jconvid, sessid);

	if (err) {
		warning("jni: acquireFlows: failed: %m\n", err);
	}
		
	return err == 0;
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_releaseFlowsNative
(JNIEnv *env, jobject self, jstring jconvid)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *convid = env->GetStringUTFChars(jconvid, 0);
	void *vjcb;
	int err;

	if (fm == NULL) {
		warning("jni: releaseFlows: no flowmgr\n");
		return;
	}

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_release_flows, fm, convid);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_addUser
(JNIEnv *env, jobject self, jstring jconvid, jstring juserid, jstring jname)
{

	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *convid = NULL;
	const char *userid = NULL;
	const char *name = NULL;
	int err;

	if (fm == NULL) {
		warning("jni: addUser: no flowmgr\n");
		return;
	}

	if (jconvid)
		convid = env->GetStringUTFChars(jconvid, 0);
	if (juserid)
		userid = env->GetStringUTFChars(juserid, 0);
	if (jname)
		name = env->GetStringUTFChars(jname, 0);
	
	
	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_user_add,
			     fm, convid, userid, name);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
	if (userid)
		env->ReleaseStringUTFChars(jconvid, userid);
	if (name)
		env->ReleaseStringUTFChars(jconvid, name);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_refreshAccessToken
(JNIEnv *env, jobject self, jstring jtoken, jstring jtype)
{

	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *token = NULL;
	const char *type = NULL;
	int err;

	if (fm == NULL) {
		warning("jni: refreshAccessToken: no flowmgr\n");
		return;
	}

	if (jtoken)
		token = env->GetStringUTFChars(jtoken, 0);
	if (jtype)
		type = env->GetStringUTFChars(jtype, 0);


	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_refresh_access_token,
			     fm, token, type);
	
	if (token)
		env->ReleaseStringUTFChars(jtoken, token);
	if (type)
		env->ReleaseStringUTFChars(jtype, type);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setSelfUser
(JNIEnv *env, jobject self, jstring juserid)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *userid = NULL;
	int err;

	if (fm == NULL) {
		warning("jni: setSelfUser: no flowmgr\n");
		return;
	}

	if (juserid)
		userid = env->GetStringUTFChars(juserid, 0);
	
	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_self_userid, fm, userid);

	if (userid)
		env->ReleaseStringUTFChars(juserid, userid);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setActive
(JNIEnv *env, jobject self, jstring jconvid, jboolean active)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *convid = env->GetStringUTFChars(jconvid, 0);
	void *vjcb;
	int err;

	if (fm == NULL) {
		warning("jni: setActive: no flowmgr\n");
		return;
	}

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_active, fm, convid, active);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_networkChanged
(JNIEnv *env, jobject self)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	
	if (fm == NULL) {
		warning("jni: networkChanged: no flowmgr\n");
		return;
	}

	info("jni: netwrokChanged\n");
	
	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_network_changed, fm);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_response
(JNIEnv *env, jobject self, jint status, jstring jctype,
 jbyteArray jcontent, jlong jctx)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	jboolean iscpy = JNI_FALSE;
	jbyte *content = NULL;
	const char *ctype = NULL;
	const char *reason = NULL;
	size_t clen = 0;
	
	if (fm == NULL) {
		warning("jni: response: no flowmgr\n");
		return;
	}

	if (jcontent) {
		clen = (size_t)env->GetArrayLength(jcontent);
		content = env->GetByteArrayElements(jcontent, &iscpy);
	}
	if (jctype)
		ctype = env->GetStringUTFChars(jctype, 0);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_resp, fm, (int)status, "",
			     ctype, (const char *)content, (size_t)clen,
			     (struct rr_resp *)jctx);

	if (content) {
		env->ReleaseByteArrayElements(jcontent, content, JNI_ABORT);
	}
	if (ctype) {
		env->ReleaseStringUTFChars(jctype, ctype);
	}
}


JNIEXPORT jint JNICALL Java_com_waz_call_FlowManager_mediaCategoryChanged
(JNIEnv *env, jobject self, jstring jconvid, jint mcat)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	const char *convid;
	int err = 0;

	if (!jconvid)
		return EINVAL;

	if (fm == NULL) {
		warning("jni: releaseFlows: no flowmgr\n");
		return ENOSYS;
	}

	convid = env->GetStringUTFChars(jconvid, 0);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_mcat_changed, fm, convid,
			     (enum flowmgr_mcat)mcat);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);

	return err;
}


JNIEXPORT jint JNICALL Java_com_waz_call_FlowManager_audioSourceDeviceChanged
(JNIEnv *env, jobject self, jint ausrc)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	int err;

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_ausrc_changed, fm,
			    (enum flowmgr_ausrc)ausrc);

	return err;
}


JNIEXPORT jint JNICALL Java_com_waz_call_FlowManager_audioPlayDeviceChanged
(JNIEnv *env, jobject self, jint auplay)
{
	struct jfm *jfm = self2fm(env, self);
    int err = -1;
    if(jfm != NULL){
        struct flowmgr *fm = jfm->fm;
    
        FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_auplay_changed, fm,
        		    (enum flowmgr_auplay)auplay);
    }
	return err;
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setLogHandler
(JNIEnv *env, jobject self, jobject logh)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	jclass cls;

	if (!logh)
		return;

	cls = env->GetObjectClass(logh);
	if (!cls) {
		warning("jni: setLogHandler: no class for handler\n");
		return;
	}

	jfm->log.appendmid = env->GetMethodID(cls, "append",
					      "(Ljava/lang/String;)V");
	jfm->log.uploadmid = env->GetMethodID(cls, "upload",
					      "()V");

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_log_handlers, fm,
			    log_append_handler, log_upload_handler, jfm);
}



JNIEXPORT jint JNICALL Java_com_waz_call_FlowManager_setMute
(JNIEnv *env, jobject self, jboolean mute)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	int err;

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_set_mute, fm,
			    mute ? true : false);

	return err;
}


JNIEXPORT jboolean JNICALL Java_com_waz_call_FlowManager_getMute
(JNIEnv *env, jobject self)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	bool muted;
	int err;

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_get_mute, fm, &muted);

	return err ? JNI_FALSE : (muted ? JNI_TRUE : JNI_FALSE);
}


JNIEXPORT jboolean JNICALL Java_com_waz_call_FlowManager_event
(JNIEnv *env, jobject self, jstring jctype, jbyteArray jcontent)
{
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;
	jboolean iscpy = JNI_FALSE;
	jbyte *content;
	const char *ctype;
	bool handled;
	size_t clen;
	int err = 0;

	if (fm == NULL) {
		warning("jni: event: no flowmgr\n");
		return JNI_FALSE;
	}

	clen = (size_t)env->GetArrayLength(jcontent);
	content = env->GetByteArrayElements(jcontent, &iscpy);
	ctype = env->GetStringUTFChars(jctype, 0);

	FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_process_event, &handled,
			    fm, ctype, (const char *)content, (size_t)clen);

	if (content) 
		env->ReleaseByteArrayElements(jcontent, content, JNI_ABORT);
	if (ctype)
		env->ReleaseStringUTFChars(jctype, ctype);

	return (!err && handled) ? JNI_TRUE : JNI_FALSE;
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setEnableLogging
(JNIEnv *env, jobject self, jboolean jenable)
{
	struct jfm *jfm = self2fm(env, self);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_enable_logging,
			     jfm->fm, (bool)jenable);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setEnableMetrics
(JNIEnv *env, jobject self, jboolean jenable)
{
	struct jfm *jfm = self2fm(env, self);

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_enable_metrics,
			     jfm->fm, (bool)jenable);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setSessionId
(JNIEnv *env, jobject self, jstring jconvid, jstring jsessid)
{
	const char *convid = env->GetStringUTFChars(jconvid, 0);
	const char *sessid = env->GetStringUTFChars(jsessid, 0);
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;

	flowmgr_set_sessid(fm, convid, sessid);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
	if (sessid)
		env->ReleaseStringUTFChars(jsessid, sessid);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_callInterruption
(JNIEnv *env, jobject self, jstring jconvid, jboolean interrupted)
{
	const char *convid = env->GetStringUTFChars(jconvid, 0);	
	struct jfm *jfm = self2fm(env, self);
	struct flowmgr *fm = jfm->fm;

	flowmgr_interruption(fm, convid, (bool)interrupted);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);		
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setLogLevel
(JNIEnv *env, jclass cls, jint jlevel)
{
	java.log_level = (enum log_level)jlevel;
}


JNIEXPORT jobjectArray JNICALL Java_com_waz_call_FlowManager_sortConferenceParticipants
(JNIEnv *env, jclass cls, jobjectArray jparts)
{
	struct list partl = LIST_INIT;
	jobjectArray jsorted_arr;
	jsize n = env->GetArrayLength(jparts);
	struct le *le;
	jsize i;
	jobject jpo;
	jclass partcls;
	int err;

    if( n == 0){
        return jparts;
    }
    
	jpo = env->GetObjectArrayElement(jparts, 0);
	partcls = env->GetObjectClass(jpo);

	for(i = 0; i < n; ++i) {
		jstring jpart =	(jstring)env->GetObjectArrayElement(jparts, i);
		const char *part;
		struct conf_part *cp;
		
		part = env->GetStringUTFChars(jpart, 0);

		err = conf_part_add(&cp, &partl, part, NULL);
		if (part)
			env->ReleaseStringUTFChars(jpart, part);

		if (err)
			goto out;
	}	

	err = flowmgr_sort_participants(&partl);
	if (err)
		goto out;

	jsorted_arr = env->NewObjectArray(n, partcls, NULL);	
	i = 0;
	le = partl.head;
	while (le) {
		struct conf_part *cp = (struct conf_part *)le->data;

		if (cp) {
			jstring jpart = env->NewStringUTF(cp->uid);
			env->SetObjectArrayElement(jsorted_arr, i, jpart);
		}
			
		++i;
		le = le->next;
		list_unlink(&cp->le);
		
		mem_deref(cp);
	}

out:
	if (err)
		return NULL;
	else
		return jsorted_arr;
}


#ifdef ANDROID
JNIEXPORT jboolean JNICALL Java_com_waz_call_FlowManager_canSendVideo
(JNIEnv *env, jobject self, jstring jconvid)
{
	int canSend = 0;
	struct jfm *jfm = self2fm(env, self);
	const char *convid =
		jconvid ? env->GetStringUTFChars(jconvid, 0) : NULL;

	debug("canSendVideo: convid-%s\n", convid);

	FLOWMGR_MARSHAL_RET(java.tid, canSend, flowmgr_can_send_video,
			     jfm->fm, convid);
	debug("canSendVideo: convid=%s %d\n", convid, canSend);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);

	return canSend ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoSendStateNative
(JNIEnv *env, jobject self, jstring jconvid, jint jstate)
{
	struct jfm *jfm = self2fm(env, self);
	const char *convid =
		jconvid ? env->GetStringUTFChars(jconvid, 0) : NULL;
	enum flowmgr_video_send_state state = FLOWMGR_VIDEO_SEND_NONE;
	
	debug("setVideoSendStateNative: convid=%s state=%d\n", convid, state);

	switch (jstate) {
	case com_waz_call_FlowManager_VIDEO_SEND_NONE:
		state = FLOWMGR_VIDEO_SEND_NONE;
		break;

	case com_waz_call_FlowManager_VIDEO_PREVIEW:
		return;

	case com_waz_call_FlowManager_VIDEO_SEND:
		state = FLOWMGR_VIDEO_SEND;
		break;

	defult:
		warning("setVideoSendState: unknown state: %d\n", jstate);
		return;
	}

	jfm->video_state = state;

	FLOWMGR_MARSHAL_VOID(java.tid, flowmgr_set_video_send_state,
			     jfm->fm, convid, state);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoView
(JNIEnv *env, jobject self, jstring jconvid, jstring jpartid, jobject jview)
{
	struct jfm *jfm = self2fm(env, self);
	jobject view = env->NewGlobalRef(jview);
	const char *convid =
		jconvid ? env->GetStringUTFChars(jconvid, 0) : NULL;
	const char *partid =
		jpartid ? env->GetStringUTFChars(jpartid, 0) : NULL;
	jmethodID getNativeRenderer;
	jclass cls;
	jlong jobj;
	
	debug("setVideoView:jview=%p view=%p convid=%s partid=%s\n",
	      jview, view, convid, partid);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
	if (partid)
		env->ReleaseStringUTFChars(jpartid, partid);
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setBackground
(JNIEnv *env, jobject self, jboolean jbg)
{
	debug("NOOP setBackground: backgrounded=%d\n", jbg);
}


JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoCaptureDevice
(JNIEnv *env, jobject self, jstring jconvid, jstring jdevid)
{
	struct jfm *jfm = self2fm(env, self);
	const char *convid = env->GetStringUTFChars(jconvid, 0);
	const char *devid = env->GetStringUTFChars(jdevid, 0);

	debug("setVideoCaptureDevice: convid=%s devid=%s\n",
	      convid, devid);

	if (convid)
		env->ReleaseStringUTFChars(jconvid, convid);
	if (devid)
		env->ReleaseStringUTFChars(jdevid, devid);

	return;	
}


#else

JNIEXPORT jboolean JNICALL Java_com_waz_call_FlowManager_canSendVideo
(JNIEnv *env, jobject self, jstring jconvid)
{
	(void)env;
	(void)self;
	(void)jconvid;

	return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoSendStateNative
(JNIEnv *env, jobject self, jstring jconvid, jint state)
{
	(void)env;
	(void)self;
	(void)jconvid;
	(void)state;
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoPreview
(JNIEnv *env, jobject self, jstring jconvid, jobject jview)
{
	(void)env;
	(void)self;
	(void)jconvid;
	(void)jview;
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoView
(JNIEnv *env, jobject self, jstring jconvid, jstring jpartid, jobject jview)
{
	(void)env;
	(void)self;
	(void)jconvid;
	(void)jpartid;
	(void)jview;
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setVideoCaptureDevice
(JNIEnv *env, jobject self, jstring jconvid, jstring jdevid)
{
	(void)env;
	(void)self;
	(void)jconvid;
	(void)jdevid;
}

JNIEXPORT jobjectArray JNICALL Java_com_waz_call_FlowManager_getVideoCaptureDevices
(JNIEnv *env, jobject self)
{
	struct jfm *jfm = self2fm(env, self);
	jobjectArray jdevs = NULL;

	jdevs = env->NewObjectArray(0,
				    env->FindClass("com/waz/call/CaptureDevice"),
				    NULL);
	return jdevs;
}

#endif

static int vie_jni_get_view_size_handler(const void *view, int *w, int *h)
{
	jobject jview = (jobject)view;
	jclass cls;
	jmethodID wmid;
	jmethodID hmid;
	struct jni_env je;
	int vieww;
	int viewh;
	int err = 0;

	debug("vie_jni_get_view_size: view=%p\n", view);

	if (!w || !h)
		return EINVAL;

	err = jni_attach(&je);
	if (err) {
		warning("%s: could not attach\n", __func__);
		return err;
	}
		
	cls = je.env->GetObjectClass(jview);
	wmid = je.env->GetMethodID(cls, "getWidth", "()I");
	hmid = je.env->GetMethodID(cls, "getHeight", "()I");
	if (!wmid || !hmid) {
		warning("vie: %s: no width or height\n", __func__);
		err = ENOSYS;
		goto out;
	}

	*w = je.env->CallIntMethod(jview, wmid);
	*h = je.env->CallIntMethod(jview, hmid);

 out:
	jni_detach(&je);

	return err;
}


JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
	info("JNI_OnLoad\n");

	(void)reserved;

	return JNI_VERSION_1_6;
};


JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
{
	info("JNI_OnUnload\n");

	close();

	info("JNI unloaded\n");
}


JNIEXPORT void JNICALL Java_com_waz_avs_VideoCapturer_handleCameraFrame
  (JNIEnv *env, jclass cls, jint w, jint h,
   jbyteArray jframe, jint degrees, jlong jts)
{

	struct avs_vidframe vf;
	jboolean iscpy = JNI_FALSE;	
	size_t len = (size_t)env->GetArrayLength(jframe);
	uint8_t *y = (uint8_t *)env->GetByteArrayElements(jframe, &iscpy);
	size_t ystride = (w + 16) & 0xfffffff0;
	size_t cstride = (ystride/2 + 16) & 0xfffffff0;
	size_t uoff = ystride*h;
	size_t voff = ystride*h + cstride*(h/2);

	/* Android is providing frames in NV21 */
	vf.type = AVS_VIDFRAME_NV21;
	vf.y = y;
	vf.u = y + uoff;
	vf.v = y + voff;
	vf.ys = ystride;
	vf.us = cstride;
	vf.vs = cstride;
	vf.w = w;
	vf.h = h;
	vf.rotation = (int)(360 - degrees);
	vf.ts = (uint32_t)jts;
	
	flowmgr_handle_frame(&vf);

 out:
	if (y) 
		env->ReleaseByteArrayElements(jframe, (jbyte*)y, JNI_ABORT);
}


/* Video renderer */
JNIEXPORT jlong JNICALL Java_com_waz_avs_VideoRenderer_createNative
(JNIEnv *env, jobject jself, jint w, jint h, jboolean rounded)
{
	struct video_renderer **vrp = &java.video.renderer;
	int err;
	jobject self = env->NewGlobalRef(jself);

	if (*vrp)
		*vrp = (struct video_renderer *)mem_deref((void *)(*vrp));
		
	err = video_renderer_alloc(vrp, w, h, rounded, (void *)self);
	if (err)
		return 0;

	return (jlong)((void *)(*vrp));
}


JNIEXPORT void JNICALL Java_com_waz_avs_VideoRenderer_destroyNative
(JNIEnv *env, jobject jself, jlong obj)
{
	struct video_renderer *vr = (struct video_renderer *)((void *)obj);
	jobject self = (jobject)video_renderer_arg(vr);

	env->DeleteGlobalRef(self);
	
	if (java.video.renderer == vr)
		java.video.renderer = NULL;
	
	mem_deref(vr);
}

JNIEXPORT void JNICALL Java_com_waz_avs_VideoRenderer_setShouldFill
  (JNIEnv *env, jobject jself, jlong obj, jboolean should_fill)
{
	struct video_renderer *vr = (struct video_renderer *)((void *)obj);
	jobject self = (jobject)video_renderer_arg(vr);

	video_renderer_set_should_fill(vr, true);
}

JNIEXPORT void JNICALL Java_com_waz_call_FlowManager_setFilePath
(JNIEnv *env, jobject self, jstring jpath)
{
    const char *file_path = env->GetStringUTFChars(jpath, 0);
    
    voe_set_file_path(file_path);
    
    if (file_path)
        env->ReleaseStringUTFChars(jpath, file_path);
}

JNIEXPORT jint JNICALL Java_com_waz_call_FlowManager_setAudioEffect
(JNIEnv *env, jobject self, jint effect)
{
    struct jfm *jfm = self2fm(env, self);
    struct flowmgr *fm = jfm->fm;
    int err;
    
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
    
    FLOWMGR_MARSHAL_RET(java.tid, err, flowmgr_set_audio_effect, fm, effect_type);
    
    return err;
}
