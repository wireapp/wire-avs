# Makefile for test for gmock

LOCAL_PATH := $(call my-dir)

ZCONTRIB_PATH := $(LOCAL_PATH)/../../../../../build/contrib/android

include $(CLEAR_VARS)

LOCAL_MODULE    := gmock
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX

LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -x c++ -std=c++11 -stdlib=libc++

LOCAL_C_INCLUDES := $(ZCONTRIB_PATH)/include \
                    $(LOCAL_PATH)/../../../src/gmock/include \
		    $(LOCAL_PATH)/../../../src

LOCAL_SRC_FILES := ../../../src/gmock/src/gmock-cardinalities.cc \
                   ../../../src/gmock/src/gmock-internal-utils.cc \
		   ../../../src/gmock/src/gmock-matchers.cc \
                   ../../../src/gmock/src/gmock-spec-builders.cc \
                   ../../../src/gmock/src/gmock.cc

include $(BUILD_STATIC_LIBRARY)
