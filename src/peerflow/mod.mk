#
# mod.mk
#

AVS_SRCS += \
	peerflow/capture_source.cpp \
	peerflow/cbr_detector_local.cpp \
	peerflow/cbr_detector_remote.cpp \
	peerflow/frame_decryptor_wrapper.cpp \
	peerflow/frame_encryptor_wrapper.cpp \
	peerflow/avsstats.cpp \
	peerflow/peerflow.cpp \
	peerflow/video_renderer.cpp

AVS_CPPFLAGS_src/peerflow := \
	-Imediaengine \
	-Imediaengine/webrtc-checkout/src \
	-Imediaengine/abseil-cpp

ifeq ($(AVS_OS),ios)
IS_IOS := true
endif
ifeq ($(AVS_OS),iossim)
IS_IOS := true
endif

ifneq ($(IS_IOS),)

AVS_SRCS += \
	peerflow/pc_platform_ios.m

else

ifeq ($(AVS_OS),android)

AVS_SRCS += \
	peerflow/pc_platform_android.cpp
else

AVS_SRCS += \
	peerflow/pc_platform_default.c
endif
endif
