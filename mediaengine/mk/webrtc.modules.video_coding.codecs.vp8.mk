
MENG_CPPFLAGS_webrtc/modules/video_coding/codecs/vp8/ += \
	-Imediaengine/libyuv/include

MENG_SRCS += \
	webrtc/modules/video_coding/codecs/vp8/default_temporal_layers.cc \
	webrtc/modules/video_coding/codecs/vp8/realtime_temporal_layers.cc \
	webrtc/modules/video_coding/codecs/vp8/reference_picture_selection.cc \
	webrtc/modules/video_coding/codecs/vp8/screenshare_layers.cc \
	webrtc/modules/video_coding/codecs/vp8/simulcast_encoder_adapter.cc \
	webrtc/modules/video_coding/codecs/vp8/vp8_impl.cc \


