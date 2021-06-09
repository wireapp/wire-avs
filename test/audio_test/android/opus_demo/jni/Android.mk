# Makefile for test of Audio Coding Module

LOCAL_PATH := $(call my-dir)

ifeq ($(AVS_COMPONENTS),)
AVS_COMPONENTS := $(LOCAL_PATH)/../../../../../build/android-armv7/
endif

include $(CLEAR_VARS)

LOCAL_MODULE    := opus_demo
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC -x c++ -std=c++11 -stdlib=libc++ -DWEBRTC_ANDROID -pthread


LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../../../mediaengine \
		    $(LOCAL_PATH)/../../../../../contrib/opus/include \
		    $(LOCAL_PATH)/../../../../../contrib/opus/src \
		    $(LOCAL_PATH)/../../../../../contrib/opus/celt

LOCAL_SRC_FILES := ../../../src/opus_demo.cpp

LOCAL_LDLIBS    := \
		-llog -lz \
		-L$(AVS_COMPONENTS)/lib \
		-lmediaengine \
		-lopus

LOCAL_LDLIBS	+= \
$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++_static.a \
$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libandroid_support.a \
$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++abi.a \
$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libunwind.a

include $(BUILD_EXECUTABLE)
