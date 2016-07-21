MENG_CPPFLAGS_webrtc/modules/audio_coding/codecs/opus/ += \
	-Icontrib/opus/celt \
	-Icontrib/opus/opus \
        -Icontrib/opus/src \
	-Icontrib/opus/include

MENG_SRCS += \
	webrtc/modules/audio_coding/codecs/opus/audio_decoder_opus.cc \
	webrtc/modules/audio_coding/codecs/opus/audio_encoder_opus.cc \
	webrtc/modules/audio_coding/codecs/opus/opus_interface.c
