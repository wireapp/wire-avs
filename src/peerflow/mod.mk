#
# mod.mk
#

ifeq ($(AVS_OS),wasm)
AVS_SRCS += \
	peerflow/dummy_lock.c \
	peerflow/jsflow.c \
	peerflow/sdp.c
else
AVS_SRCS += \
	peerflow/capture_source.cpp \
	peerflow/cbr_detector_local.cpp \
	peerflow/cbr_detector_remote.cpp \
	peerflow/frame_decryptor.cpp \
	peerflow/frame_encryptor.cpp \
	peerflow/frame_hdr.c \
	peerflow/peerflow.cpp \
	peerflow/video_renderer.cpp \
	peerflow/sdp.c
endif

AVS_CPPFLAGS_src/peerflow := \
	-Imediaengine \
	-Imediaengine/webrtc-checkout/src \
	-Imediaengine/abseil-cpp

ifeq ($(AVS_OS),ios)

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
