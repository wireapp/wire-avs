

MENG_CPPFLAGS_webrtc/common_video/ += \
	-Imediaengine/webrtc/common_video/interface \
	-Imediaengine/webrtc/common_video/libyuv/include \
	-Imediaengine/libyuv/include

MENG_CPPFLAGS_webrtc/common_video/libyuv/ += \
	-Imediaengine/libyuv/include

MENG_SRCS += \
	webrtc/common_video/bitrate_adjuster.cc \
	webrtc/common_video/libyuv/webrtc_libyuv.cc \
	webrtc/common_video/libyuv/scaler.cc \
	webrtc/common_video/i420_buffer_pool.cc \
	webrtc/common_video/incoming_video_stream.cc \
	webrtc/common_video/video_frame.cc \
	webrtc/common_video/video_frame_buffer.cc \
	webrtc/common_video/video_render_frames.cc

ifeq ($(AVS_OS),ios)

MENG_SRCS += \
	webrtc/common_video/corevideo_frame_buffer.cc

endif

