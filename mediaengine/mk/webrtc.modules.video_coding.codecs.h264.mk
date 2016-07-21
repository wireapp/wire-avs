
MENG_CPPFLAGS_webrtc/modules/video_coding/codecs/h264/ += \
	-Imediaengine/libyuv/include

MENG_SRCS += \
	webrtc/modules/video_coding/codecs/h264/h264.cc \
	webrtc/modules/video_coding/codecs/h264/h264_video_toolbox_decoder.cc \
	webrtc/modules/video_coding/codecs/h264/h264_video_toolbox_encoder.cc \
	webrtc/modules/video_coding/codecs/h264/h264_video_toolbox_nalu.cc

ifeq ($(AVS_OS),ios)

MENG_SRCS += \
	webrtc/modules/video_coding/codecs/h264/h264_objc.mm \

endif
