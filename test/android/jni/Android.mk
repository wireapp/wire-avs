# Makefile for ztest

LOCAL_PATH := $(call my-dir)

LIBS_PATH := $(LOCAL_PATH)/../../../build/dest/android/lib
ZCONTRIB_PATH := $(LOCAL_PATH)/../../../build/contrib/android

include $(CLEAR_VARS)

LOCAL_MODULE    := ztest
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC -x c++ -std=c++11 -stdlib=libc++ -DWEBRTC_ANDROID -DWEBRTC_CODEC_OPUS -pthread -v

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../include \
    $(LOCAL_PATH)/../../../common/engine \
    $(ZCONTRIB_PATH)/include/ \
    $(ZCONTRIB_PATH)/include/re/

LOCAL_SRC_FILES := ../../../test/main.cpp \
                   ../../../test/util.cpp \
                   ../../../test/test_chunk.cpp \
                   ../../../test/test_cookie.cpp \
                   ../../../test/test_dict.cpp \
                   ../../../test/test_engine.cpp \
                   ../../../test/test_flowmgr.cpp \
                   ../../../test/test_flowmgr_b2b.cpp \
                   ../../../test/test_jzon.cpp \
                   ../../../test/test_libre.cpp \
                   ../../../test/test_login.cpp \
                   ../../../test/test_media.cpp \
                   ../../../test/test_media_b2b.cpp \
                   ../../../test/test_mill.cpp \
                   ../../../test/test_nevent.cpp \
                   ../../../test/test_self.cpp \
                   ../../../test/test_turn.cpp \
                   ../../../test/test_uuid.cpp \
                   ../../../test/test_audummy.cpp \
                   ../../../test/test_zapi.cpp \
                   ../../../test/fake_backend.cpp \
                   ../../../test/fake_stunsrv.cpp \
                   ../../../test/turn/fake_turnsrv.cpp \
                   ../../../test/turn/alloc.c \
                   ../../../test/turn/chan.c \
                   ../../../test/turn/perm.c \
                   ../../../test/turn/turn.c

LOCAL_LDLIBS    := \
		-llog -lz \
                -L$(LIBS_PATH) \
                -lavs -lmediaengine -lopus -lre -lssl -lcrypto -lcpufeatures -llog -lz -lGLESv2 -lgtest \
                $(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi/libc++_static.a

include $(BUILD_EXECUTABLE)
