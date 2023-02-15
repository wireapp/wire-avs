#
# Android.mk
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include ../mk/target.mk

PROJDIR		:= $(PWD)
BUILDDEBUG	:= 1

LOCAL_MODULE    := avs
LOCAL_CFLAGS    := -DANDROID_PLATFORM=android-21 \
		   -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -DWEBRTC_POSIX -DDEBUG=$(BUILDDEBUG) \
		   -pthread

LOCAL_CXXFLAGS  := -DANDROID_PLATFORM=android-21 \
		   -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -x c++ -std=c++17 -stdlib=libc++ \
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

endif

LOCAL_LDLIBS 	+= \
		-lOpenSLES


include $(BUILD_SHARED_LIBRARY)
