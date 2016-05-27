# Makefile for test for gflags

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := gflags
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX

LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -x c++ -std=c++11 -stdlib=libc++

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../src/gflags/gen/posix/include \
                    $(LOCAL_PATH)/../../../src/gflags/gen/posix/include/private \
                    $(LOCAL_PATH)/../../../src/gmock/include

LOCAL_SRC_FILES := ../../../src/gflags/src/gflags.cc \
                   ../../../src/gflags/src/gflags_completions.cc \
                   ../../../src/gflags/src/gflags_reporting.cc

include $(BUILD_STATIC_LIBRARY)
