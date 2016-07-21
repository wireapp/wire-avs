
MENG_SRCS += \
	webrtc/modules/video_processing/frame_preprocessor.cc \
	webrtc/modules/video_processing/spatial_resampler.cc \
	webrtc/modules/video_processing/video_decimator.cc \
	webrtc/modules/video_processing/video_denoiser.cc \
	webrtc/modules/video_processing/video_processing_impl.cc \
	webrtc/modules/video_processing/util/denoiser_filter.cc \
	webrtc/modules/video_processing/util/denoiser_filter_c.cc \
	webrtc/modules/video_processing/util/noise_estimation.cc \
	webrtc/modules/video_processing/util/skin_detection.cc

ifeq ($(AVS_FAMILY),x86)
MENG_SRCS += \
	webrtc/modules/video_processing/util/denoiser_filter_sse2.cc
endif

ifeq ($(AVS_ARCH),armv7)
MENG_SRCS += \
	webrtc/modules/video_processing/util/denoiser_filter_neon.cc
endif

ifeq ($(AVS_ARCH),armv7s)
MENG_SRCS += \
	webrtc/modules/video_processing/util/denoiser_filter_neon.cc
endif

