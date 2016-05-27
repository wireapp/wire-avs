# Makefile for test for test_support

LOCAL_PATH := $(call my-dir)

COMPONENTS_PATH := $(LOCAL_PATH)/../../../../../build/components/android

include $(CLEAR_VARS)

LOCAL_MODULE    := test_support
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DWEBRTC_ANDROID

LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DWEBRTC_ANDROID -x c++ -std=c++11 -stdlib=libc++

LOCAL_C_INCLUDES := $(COMPONENTS_PATH)/zcontrib/include \
                    $(COMPONENTS_PATH)/zwebrtc/include \
		    $(LOCAL_PATH)/../../../src/ \
                    $(LOCAL_PATH)/../../../src/gflags/gen/posix/include \
                    $(LOCAL_PATH)/../../../src/gmock/include

LOCAL_SRC_FILES := ../../../src/testsupport/fileutils.cc \
                   ../../../src/testsupport/frame_reader.cc \
                   ../../../src/testsupport/frame_writer.cc \
                   ../../../src/testsupport/packet_reader.cc \
                   ../../../src/testsupport/perf_test.cc \
                   ../../../src/testsupport/trace_to_stderr.cc \
                   ../../../src/testsupport/android/root_path_android.cc

include $(BUILD_STATIC_LIBRARY)
