#
# Android.mk
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include ../mk/target.mk

PROJDIR		:= $(PWD)
BUILDDEBUG	:= 1

LOCAL_MODULE    := avs
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -DWEBRTC_POSIX -DDEBUG=$(BUILDDEBUG) \
		   -pthread
LOCAL_CXXFLAGS  := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -x c++ -std=c++14 -stdlib=libc++ \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -pthread

LOCAL_C_INCLUDES := ../build/android-$(AVS_ARCH_NAME)/include \
		    ../build/android-$(AVS_ARCH_NAME)/include/re \
		    ../include \
		    ../contrib/webrtc/$(WEBRTC_VER)/include \
		    ../contrib/webrtc/$(WEBRTC_VER)/include/third_party/abseil-cpp

LOCAL_SRC_FILES := \
		flow_manager.cc \
		media_manager.cc \
		video_renderer.cc \
		audio_effect.cc

LOCAL_LDLIBS    := \
		-L../build/android-$(AVS_ARCH_NAME)/lib \
		-L../contrib/webrtc/$(WEBRTC_VER)/lib/android-$(AVS_ARCH_NAME) \
		-L$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/$(TARGET_ARCH_ABI)

LOCAL_LDLIBS    += \
		-lavscore \
		-lre \
		-lrew \
		-lsodium \
		-llog -lz -lGLESv2 \
		-latomic \
		-lwebrtc

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_LDLIBS	+= \
	-landroid_support \
	-lunwind 
endif

LOCAL_LDLIBS 	+= \
		-lOpenSLES

LOCAL_LDLIBS	+= \
		-lc++_static \
		-lc++abi \


include $(BUILD_SHARED_LIBRARY)
