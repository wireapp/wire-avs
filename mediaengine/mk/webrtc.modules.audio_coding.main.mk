MENG_CPPFLAGS_webrtc/modules/audio_coding/main/acm2/ += \
	-Imediaengine/webrtc/modules/audio_coding/main/interface \
	-Imediaengine/webrtc/modules/interface

MENG_CPPFLAGS += \
	-DWEBRTC_CODEC_ISAC

MENG_SRCS += \
	webrtc/modules/audio_coding/acm2/acm_codec_database.cc \
	webrtc/modules/audio_coding/acm2/acm_receiver.cc \
	webrtc/modules/audio_coding/acm2/acm_resampler.cc \
	webrtc/modules/audio_coding/acm2/audio_coding_module.cc \
	webrtc/modules/audio_coding/acm2/audio_coding_module_impl.cc \
	webrtc/modules/audio_coding/acm2/call_statistics.cc \
	webrtc/modules/audio_coding/acm2/codec_manager.cc \
	webrtc/modules/audio_coding/acm2/initial_delay_manager.cc \
	webrtc/modules/audio_coding/acm2/rent_a_codec.cc \
	webrtc/modules/audio_coding/codecs/builtin_audio_decoder_factory.cc
