LOCAL_PATH:= $(call my-dir)

BASE_PATH := $(LOCAL_PATH)/../../../../../
ifeq ($(AVS_COMPONENTS),)
AVS_COMPONENTS := $(LOCAL_PATH)/../../../../../build/android-armv7/
endif

include $(CLEAR_VARS)

PROJDIR		:= $(PWD)
BUILDDEBUG	:= 1

LOCAL_MODULE    := audiotestnative
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -x c++ -std=c++11 -stdlib=libc++ \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -pthread

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../../../mediaengine

LOCAL_SRC_FILES := \
		audiotestwrapper.cc \
        ../../../src/NwSimulator.cpp \
        ../../../src/voe_conf_test_dec.cpp \
	../../../src/start_stop_stress_test.cpp \
	../../../src/voe_loopback_test.cpp

LOCAL_LDLIBS    := \
		-L$(AVS_COMPONENTS)/lib \
		-lcpufeatures \
		-lbreakpad \
		-llog -lz -lGLESv2 \
		-lmediaengine \
		-lopus \
		-llog

LOCAL_LDLIBS	+= \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++_static.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++_static.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libandroid_support.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++abi.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libunwind.a

include $(BUILD_SHARED_LIBRARY)
