# mediaengine - Build system
#
# webrtc/common_audio

MENG_CPPFLAGS += \
	-Imediaengine/webrtc/common_audio/resampler/include \
	-Imediaengine/webrtc/common_audio/signal_processing/include \
	-Imediaengine/webrtc/common_audio/vad/include

MENG_SRCS	+= \
	webrtc/common_audio/audio_converter.cc \
	webrtc/common_audio/audio_ring_buffer.cc \
	webrtc/common_audio/audio_util.cc \
	webrtc/common_audio/blocker.cc \
    webrtc/common_audio/channel_buffer.cc \
    webrtc/common_audio/fft4g.c \
    webrtc/common_audio/fir_filter.cc \
    webrtc/common_audio/lapped_transform.cc \
    webrtc/common_audio/real_fourier.cc \
    webrtc/common_audio/real_fourier_ooura.cc \
    webrtc/common_audio/ring_buffer.c \
	webrtc/common_audio/resampler/push_resampler.cc \
	webrtc/common_audio/resampler/push_sinc_resampler.cc \
	webrtc/common_audio/resampler/resampler.cc \
	webrtc/common_audio/resampler/sinc_resampler.cc \
	webrtc/common_audio/signal_processing/auto_corr_to_refl_coef.c \
	webrtc/common_audio/signal_processing/auto_correlation.c \
	webrtc/common_audio/signal_processing/complex_fft.c \
	webrtc/common_audio/signal_processing/copy_set_operations.c \
	webrtc/common_audio/signal_processing/cross_correlation.c \
	webrtc/common_audio/signal_processing/division_operations.c \
	webrtc/common_audio/signal_processing/dot_product_with_scale.c \
	webrtc/common_audio/signal_processing/downsample_fast.c \
	webrtc/common_audio/signal_processing/energy.c \
	webrtc/common_audio/signal_processing/filter_ar.c \
	webrtc/common_audio/signal_processing/filter_ma_fast_q12.c \
	webrtc/common_audio/signal_processing/get_hanning_window.c \
	webrtc/common_audio/signal_processing/get_scaling_square.c \
	webrtc/common_audio/signal_processing/ilbc_specific_functions.c \
	webrtc/common_audio/signal_processing/levinson_durbin.c \
	webrtc/common_audio/signal_processing/lpc_to_refl_coef.c \
	webrtc/common_audio/signal_processing/min_max_operations.c \
	webrtc/common_audio/signal_processing/randomization_functions.c \
	webrtc/common_audio/signal_processing/refl_coef_to_lpc.c \
	webrtc/common_audio/signal_processing/real_fft.c \
	webrtc/common_audio/signal_processing/resample.c \
	webrtc/common_audio/signal_processing/resample_48khz.c \
	webrtc/common_audio/signal_processing/resample_by_2.c \
	webrtc/common_audio/signal_processing/resample_by_2_internal.c \
	webrtc/common_audio/signal_processing/resample_fractional.c \
	webrtc/common_audio/signal_processing/spl_init.c \
	webrtc/common_audio/signal_processing/spl_sqrt.c \
	webrtc/common_audio/signal_processing/splitting_filter.c \
	webrtc/common_audio/signal_processing/sqrt_of_one_minus_x_squared.c \
	webrtc/common_audio/signal_processing/vector_scaling_operations.c \
    webrtc/common_audio/sparse_fir_filter.cc \
	webrtc/common_audio/vad/vad.cc \
    webrtc/common_audio/vad/webrtc_vad.c \
	webrtc/common_audio/vad/vad_core.c \
	webrtc/common_audio/vad/vad_filterbank.c \
	webrtc/common_audio/vad/vad_gmm.c \
	webrtc/common_audio/vad/vad_sp.c \
	webrtc/common_audio/wav_header.cc \
	webrtc/common_audio/wav_file.cc \
	webrtc/common_audio/window_generator.cc \

ifeq ($(AVS_ARCH),armv7)
MENG_CPPFLAGS += -DWEBRTC_HAS_NEON

MENG_SRCS += \
	webrtc/common_audio/signal_processing/complex_bit_reverse_arm.S \
	webrtc/common_audio/signal_processing/spl_sqrt_floor_arm.S \
	webrtc/common_audio/signal_processing/filter_ar_fast_q12_armv7.S \
	webrtc/common_audio/fir_filter_neon.cc \
	webrtc/common_audio/resampler/sinc_resampler_neon.cc \
	webrtc/common_audio/signal_processing/cross_correlation_neon.c \
	webrtc/common_audio/signal_processing/downsample_fast_neon.c \
	webrtc/common_audio/signal_processing/min_max_operations_neon.c

else ifeq ($(AVS_ARCH),armv7s)
MENG_CPPFLAGS += -DWEBRTC_HAS_NEON

MENG_SRCS += \
	webrtc/common_audio/signal_processing/complex_bit_reverse_arm.S \
	webrtc/common_audio/signal_processing/spl_sqrt_floor_arm.S \
	webrtc/common_audio/signal_processing/filter_ar_fast_q12_armv7.S \
	webrtc/common_audio/fir_filter_neon.cc \
	webrtc/common_audio/resampler/sinc_resampler_neon.cc \
	webrtc/common_audio/signal_processing/cross_correlation_neon.c \
	webrtc/common_audio/signal_processing/downsample_fast_neon.c \
	webrtc/common_audio/signal_processing/min_max_operations_neon.c

#else ifeq ($(AVS_ARCH),arm64)
#MENG_SRCS += \
#	webrtc/common_audio/signal_processing/complex_bit_reverse.c \
#	webrtc/common_audio/signal_processing/spl_sqrt_floor.c \
#	webrtc/common_audio/signal_processing/filter_ar_fast_q12.c \
#   webrtc/common_audio/signal_processing/cross_correlation_neon.c \
#    webrtc/common_audio/signal_processing/downsample_fast_neon.c \
#    webrtc/common_audio/signal_processing/min_max_operations_neon.c \

else ifeq ($(AVS_ARCH),i386)
MENG_SRCS	+= \
	webrtc/common_audio/signal_processing/complex_bit_reverse.c \
	webrtc/common_audio/signal_processing/spl_sqrt_floor.c \
	webrtc/common_audio/signal_processing/filter_ar_fast_q12.c \
	webrtc/common_audio/fir_filter_sse.cc \
	webrtc/common_audio/resampler/sinc_resampler_sse.cc \

else ifeq ($(AVS_ARCH),x86_64)
MENG_SRCS += \
	webrtc/common_audio/signal_processing/complex_bit_reverse.c \
	webrtc/common_audio/signal_processing/spl_sqrt_floor.c \
	webrtc/common_audio/signal_processing/filter_ar_fast_q12.c \
	webrtc/common_audio/fir_filter_sse.cc \
	webrtc/common_audio/resampler/sinc_resampler_sse.cc \

else
MENG_SRCS += \
	webrtc/common_audio/signal_processing/complex_bit_reverse.c \
	webrtc/common_audio/signal_processing/spl_sqrt_floor.c \
	webrtc/common_audio/signal_processing/filter_ar_fast_q12.c \

endif
