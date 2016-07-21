MENG_CPPFLAGS_webrtc/modules/audio_coding/codecs/isac/main/source/ += \
	-Imediaengine/webrtc/modules/audio_coding/codecs/isac/main/include

MENG_SRCS += \
	webrtc/modules/audio_coding/codecs/isac/locked_bandwidth_info.cc \
	webrtc/modules/audio_coding/codecs/isac/main/source/arith_routines.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/arith_routines_hist.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/arith_routines_logist.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/bandwidth_estimator.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/audio_encoder_isac.cc \
	webrtc/modules/audio_coding/codecs/isac/main/source/crc.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/decode.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/decode_bwe.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/encode.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/encode_lpc_swb.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/entropy_coding.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/fft.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/filter_functions.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/filterbank_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/intialize.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/isac.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/filterbanks.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/pitch_lag_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lattice.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lpc_gain_swb_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lpc_analysis.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lpc_shape_swb12_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lpc_shape_swb16_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/lpc_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/pitch_estimator.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/pitch_filter.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/pitch_gain_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/spectrum_ar_model_tables.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/transform.c \
	webrtc/modules/audio_coding/codecs/isac/main/source/audio_decoder_isac.cc

