# Makefile for test of Audio Coding Module

LOCAL_PATH := $(call my-dir)

COMPONENTS_PATH := $(LOCAL_PATH)/../../../../../build/android-armv7/

include $(CLEAR_VARS)

LOCAL_MODULE    := resampler_test
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC -x c++ -std=c++11 -stdlib=libc++ -DWEBRTC_ANDROID -pthread


LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../../../mediaengine

LOCAL_SRC_FILES := ../../../src/resampler_test.cpp

LOCAL_LDLIBS    := \
		-llog -lz \
		-L$(COMPONENTS_PATH)/lib \
		-lmediaengine \
		-lssl \
		-lcrypto \
                $(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi/libc++_static.a

include $(BUILD_EXECUTABLE)
