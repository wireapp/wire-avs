#
# Android.mk
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

PROJDIR		:= $(PWD)
BUILDDEBUG	:= 1

LOCAL_MODULE    := avs
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -x c++ -std=c++11 -stdlib=libc++ \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -pthread

LOCAL_C_INCLUDES := ../build/android-armv7/include \
		    ../build/android-armv7/include/re \
		    ../include \
		    ../mediaengine

LOCAL_SRC_FILES := \
		audio_effect.cc \
		flow_manager.cc \
		media_manager.cc \
		video_renderer.cc

LOCAL_LDLIBS    := \
		-L../build/android-armv7/lib \
		../build/android-armv7/lib/libavscore.a \
		-lvpx \
		-lusrsctp \
		-lre \
		-lrew \
		-lsodium \
		-lssl \
		-lcrypto \
		-lcpufeatures \
		-llog -lz -lGLESv2 \
		-latomic

LOCAL_C_INCLUDES += \
		../mediaengine
LOCAL_LDLIBS    += \
		-L../../build/android-armv7/lib \
		-lmediaengine \
		-lvpx \
		-lcpufeatures \
		-lopus \
		-lOpenSLES \
		-llog

LOCAL_LDLIBS	+= \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++_static.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++abi.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libandroid_support.a \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libunwind.a 


include $(BUILD_SHARED_LIBRARY)
