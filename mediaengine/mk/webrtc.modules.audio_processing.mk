
# XXX This is missing the protobuf debugging for the moment until we get
#     protobuf build for all platforms.
#
#     Check audio_processing.gypi:audioproc_debug_proto for all of this.
#
# XXX This is also missing the generated iOS/Android assembler files for
#     aecm and nsx.
#

MENG_CPPFLAGS_webrtc/modules/audio_processing/aec/ += \
	-DWEBRTC_AEC_DEBUG_DUMP=0 \
	-DWEBRTC_AUDIOPROC_DEBUG_DUMP

MENG_CPPFLAGS_webrtc/modules/audio_processing/logging/ += \
	-DWEBRTC_AEC_DEBUG_DUMP=0

MENG_SRCS += \
	webrtc/modules/audio_processing/aec/echo_cancellation.cc \
	webrtc/modules/audio_processing/aec/aec_core.cc \
	webrtc/modules/audio_processing/aec/aec_rdft.cc \
	webrtc/modules/audio_processing/aec/aec_resampler.cc \
	webrtc/modules/audio_processing/aecm/echo_control_mobile.cc \
	webrtc/modules/audio_processing/aecm/aecm_core.cc \
	webrtc/modules/audio_processing/aecm/aecm_core_c.cc \
    webrtc/modules/audio_processing/agc/agc.cc \
    webrtc/modules/audio_processing/agc/agc_manager_direct.cc \
    webrtc/modules/audio_processing/agc/histogram.cc \
    webrtc/modules/audio_processing/agc/legacy/analog_agc.c \
    webrtc/modules/audio_processing/agc/legacy/digital_agc.c \
    webrtc/modules/audio_processing/agc/utility.cc \
	webrtc/modules/audio_processing/audio_buffer.cc \
	webrtc/modules/audio_processing/audio_processing_impl.cc \
    webrtc/modules/audio_processing/beamformer/array_util.cc \
    webrtc/modules/audio_processing/beamformer/covariance_matrix_generator.cc \
    webrtc/modules/audio_processing/beamformer/nonlinear_beamformer.cc \
	webrtc/modules/audio_processing/echo_cancellation_impl.cc \
	webrtc/modules/audio_processing/echo_control_mobile_impl.cc \
	webrtc/modules/audio_processing/gain_control_for_experimental_agc.cc \
	webrtc/modules/audio_processing/gain_control_impl.cc \
	webrtc/modules/audio_processing/high_pass_filter_impl.cc \
	webrtc/modules/audio_processing/intelligibility/intelligibility_enhancer.cc \
	webrtc/modules/audio_processing/intelligibility/intelligibility_utils.cc \
	webrtc/modules/audio_processing/level_estimator_impl.cc \
	webrtc/modules/audio_processing/logging/apm_data_dumper.cc \
	webrtc/modules/audio_processing/noise_suppression_impl.cc \
	webrtc/modules/audio_processing/rms_level.cc \
    webrtc/modules/audio_processing/splitting_filter.cc \
    webrtc/modules/audio_processing/three_band_filter_bank.cc \
    webrtc/modules/audio_processing/transient/moving_moments.cc \
    webrtc/modules/audio_processing/transient/transient_detector.cc \
    webrtc/modules/audio_processing/transient/transient_suppressor.cc \
    webrtc/modules/audio_processing/transient/wpd_node.cc \
    webrtc/modules/audio_processing/transient/wpd_tree.cc \
	webrtc/modules/audio_processing/typing_detection.cc \
	webrtc/modules/audio_processing/utility/delay_estimator.cc \
	webrtc/modules/audio_processing/utility/delay_estimator_wrapper.cc \
	webrtc/modules/audio_processing/utility/block_mean_calculator.cc \
	webrtc/modules/audio_processing/vad/gmm.cc \
	webrtc/modules/audio_processing/vad/pitch_based_vad.cc \
	webrtc/modules/audio_processing/vad/pitch_internal.cc \
	webrtc/modules/audio_processing/vad/pole_zero_filter.cc \
	webrtc/modules/audio_processing/vad/standalone_vad.cc \
	webrtc/modules/audio_processing/vad/vad_audio_proc.cc \
	webrtc/modules/audio_processing/vad/vad_circular_buffer.cc \
	webrtc/modules/audio_processing/vad/voice_activity_detector.cc \
	webrtc/modules/audio_processing/voice_detection_impl.cc

ifneq ($(PREFER_FIXED_POINT),)
MENG_CPPFLAGS += \
	-DWEBRTC_NS_FIXED

MENG_SRCS += \
	webrtc/modules/audio_processing/ns/noise_suppression_x.c \
	webrtc/modules/audio_processing/ns/nsx_core.c \
	webrtc/modules/audio_processing/ns/nsx_core_c.c

else
MENG_CPPFLAGS += \
	-DWEBRTC_NS_FLOAT

MENG_SRCS += \
	webrtc/modules/audio_processing/ns/noise_suppression.c \
	webrtc/modules/audio_processing/ns/ns_core.c
endif

ifeq ($(AVS_FAMILY),x86)
MENG_SRCS += \
	webrtc/modules/audio_processing/aec/aec_core_sse2.cc \
	webrtc/modules/audio_processing/aec/aec_rdft_sse2.cc
endif

ifeq ($(AVS_FAMILY),armv7)
MENG_SRCS += \
	webrtc/modules/audio_processing/aec/aec_core_neon.cc \
	webrtc/modules/audio_processing/aec/aec_rdft_neon.cc \
	webrtc/modules/audio_processing/aecm/aecm_core_neon.cc \
	webrtc/modules/audio_processing/ns/nsx_core_neon.c

endif

ifeq ($(AVS_OS),ios)
MENG_CPPFLAGS_webrtc/modules/audio_device/ += \
	-Imediaengine/webrtc/modules/audio_device/ios

endif

