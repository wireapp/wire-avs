MENG_CPPFLAGS_webrtc/modules/audio_coding/codecs/isac/fix/source/ += \
	-Imediaengine/webrtc/modules/audio_coding/codecs/isac/fix/interface

MENG_SRCS += \
	webrtc/modules/audio_coding/codecs/isac/fix/source/arith_routines.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/arith_routines_hist.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/arith_routines_logist.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/audio_encoder_isacfix.cc \
	webrtc/modules/audio_coding/codecs/isac/fix/source/bandwidth_estimator.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/decode.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/decode_bwe.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/decode_plc.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/encode.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/entropy_coding.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/fft.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/filterbank_tables.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/filterbanks.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/filters.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/initialize.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/isacfix.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lattice.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lattice_c.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lpc_masking_model.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lpc_tables.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_estimator.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_estimator_c.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_filter.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_gain_tables.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_lag_tables.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/spectrum_ar_model_tables.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/transform.c \

ifeq ($(AVS_FAMILY),armv7)
MENG_SRCS += \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lattice_armv7.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_filter_armv6.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/entropy_coding_neon.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/filterbanks_neon.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/filters_neon.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lattice_neon.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/lpc_masking_model_neon.S \
	webrtc/modules/audio_coding/codecs/isac/fix/source/transform_neon.S \

else
MENG_SRCS += \
	webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_filter_c.c \
	webrtc/modules/audio_coding/codecs/isac/fix/source/transform_tables.c
endif
