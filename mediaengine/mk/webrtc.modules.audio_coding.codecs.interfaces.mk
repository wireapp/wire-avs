MENG_SRCS	+= \
	webrtc/modules/audio_coding/codecs/audio_encoder.cc \
	webrtc/modules/audio_coding/codecs/audio_decoder.cc \
	webrtc/modules/audio_coding/codecs/audio_format.cc

MENG_CPPFLAGS_webrtc/modules/audio_coding/codecs/interfaces/ += \
	-Iwebrtc/modules/audio_coding/codecs

