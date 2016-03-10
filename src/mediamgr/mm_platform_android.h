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
#ifndef MM_PLATFORM_ANDROID_H
#define MM_PLATFORM_ANDROID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <jni.h>

bool mm_android_jni_java_get_initialized(void);
jfieldID mm_android_jni_java_get_fid(void);
int mm_android_jni_init(JNIEnv *env, jobject jobj, jobject ctx);
void mm_android_jni_cleanup(JNIEnv *env, jobject jobj);

void mm_android_jni_on_playback_route_changed(enum mediamgr_auplay new_route, void *arg);
void mm_android_jni_on_media_category_changed(enum mediamgr_state state, void *arg);
    
#ifdef __cplusplus
}
#endif

#endif