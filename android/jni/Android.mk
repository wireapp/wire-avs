LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

PROJDIR		:= $(PWD)
BUILDDEBUG	:= 1

LOCAL_MODULE    := avs
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -DHAVE_VIDEO=1 -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -x c++ -std=c++11 -stdlib=libc++ \
		   -DWEBRTC_ANDROID -DDEBUG=$(BUILDDEBUG) \
		   -DHAVE_VIDEO=1 -pthread

LOCAL_C_INCLUDES := ../build/android-armv7/include \
		    ../build/android-armv7/include/re \
		    ../include \
		    ../mediaengine

LOCAL_SRC_FILES := \
		flow_manager.cc \
		media_manager.cc \
		sound_link.cc

LOCAL_LDLIBS    := \
		-L../build/android-armv7/lib \
		../build/android-armv7/lib/libavscore.a \
		-lvpx \
		-lre \
		-lrew \
		-logg \
		-lssl \
		-lcrypto \
		-lcpufeatures \
		-lbreakpad \
		-llog -lz -lGLESv2 \
		-latomic

LOCAL_C_INCLUDES += \
		../mediaengine
LOCAL_LDLIBS    += \
		-L../../build/android-armv7/lib \
		-lmediaengine \
		-ljsoncpp \
		-lvpx \
		-lcpufeatures \
		-lopus \
		-llog

LOCAL_LDLIBS	+= \
		$(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a/libc++_static.a \


include $(BUILD_SHARED_LIBRARY)
