# Makefile for ztest

LOCAL_PATH := $(call my-dir)

LIBS_PATH := $(LOCAL_PATH)/../../../../../build/dest/android/lib
ZCONTRIB_PATH := $(LOCAL_PATH)/../../../../../build/contrib/android
MEDIAENGINE_PATH := $(LOCAL_PATH)/../../../../../mediaengine

include $(CLEAR_VARS)

LOCAL_MODULE    := webrtc_unit_test
LOCAL_CFLAGS    := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC \
		   -DWEBRTC_ANDROID -pthread
LOCAL_CXXFLAGS   := -g -DHAVE_INTTYPES_H=1 -DPOSIX -DHAVE_WEBRTC -x c++ -std=c++11 -stdlib=libc++ -DWEBRTC_ANDROID -DWEBRTC_CODEC_OPUS -pthread -v

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../include \
    $(LOCAL_PATH)/../../../common/engine \
    $(ZCONTRIB_PATH)/include/ \
    $(ZCONTRIB_PATH)/include/re/ \
    $(MEDIAENGINE_PATH) \
    $(MEDIAENGINE_PATH)/webrtc \
    $(MEDIAENGINE_PATH)/webrtc/modules/audio_coding/codecs/cng/include \
    $(LOCAL_PATH)/../tmp \
    $(LOCAL_PATH)/../../../test/audio_test/src \
    $(LOCAL_PATH)/../../../test/audio_test/src/gflags/gen/posix/include \
    $(LOCAL_PATH)/../../../test/audio_test/src/gflags/gen/posix/include/private \
    $(LOCAL_PATH)/../../../../../test/audio_test/src/gmock/include

LOCAL_SRC_FILES := ../../../../../test/main.cpp \
                   ../../../../../mediaengine/webrtc/common_audio/audio_util_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/resampler/resampler_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/resampler/push_resampler_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/resampler/sinusoidal_linear_chirp_source.cc \
                   ../../../../../mediaengine/webrtc/common_audio/resampler/push_sinc_resampler_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/resampler/sinc_resampler_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/signal_processing/real_fft_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/signal_processing/signal_processing_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/vad/vad_core_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/vad/vad_filterbank_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/vad/vad_gmm_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/vad/vad_sp_unittest.cc \
                   ../../../../../mediaengine/webrtc/common_audio/vad/vad_unittest.cc \
                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/acm_receiver_unittest.cc \
                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/initial_delay_manager_unittest.cc \
                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/nack_unittest.cc \
                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/acm_opus_unittest.cc
#                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/audio_coding_module_unittest.cc
#                   ../../../test/audio_test/src/modules/cng_unittest.cc \
#                   ../../../test/audio_test/src/modules/opus_unittest.cc \
#                   ../../../test/audio_test/src/modules/audio_multi_vector_unittest.cc \
#                   ../../../test/audio_test/src/modules/audio_vector_unittest.cc \
#                   ../../../test/audio_test/src/modules/background_noise_unittest.cc \
#                   ../../../test/audio_test/src/modules/buffer_level_filter_unittest.cc \
#                   ../../../test/audio_test/src/modules/comfort_noise_unittest.cc \
#                   ../../../test/audio_test/src/modules/decision_logic_unittest.cc \
#                   ../../../test/audio_test/src/modules/decoder_database_unittest.cc \
#                   ../../../test/audio_test/src/modules/delay_manager_unittest.cc \
#                   ../../../test/audio_test/src/modules/delay_peak_detector_unittest.cc \
#                   ../../../test/audio_test/src/modules/dsp_helper_unittest.cc \
#                   ../../../test/audio_test/src/modules/dtmf_buffer_unittest.cc \
#                   ../../../test/audio_test/src/modules/dtmf_tone_generator_unittest.cc \
#                   ../../../test/audio_test/src/modules/expand_unittest.cc \
#                   ../../../test/audio_test/src/modules/merge_unittest.cc \
#                   ../../../test/audio_test/src/modules/neteq_external_decoder_unittest.cc \
#                   ../../../test/audio_test/src/modules/neteq_impl_unittest.cc \
#                   ../../../test/audio_test/src/modules/neteq_stereo_unittest.cc \
#                   ../../../test/audio_test/src/modules/neteq_unittest.cc \
#                   ../../../test/audio_test/src/modules/normal_unittest.cc \
#                   ../../../test/audio_test/src/modules/packet_buffer_unittest.cc \
#                   ../../../test/audio_test/src/modules/payload_splitter_unittest.cc \
#                   ../../../test/audio_test/src/modules/post_decode_vad_unittest.cc \
#                   ../../../test/audio_test/src/modules/random_vector_unittest.cc \
#                   ../../../test/audio_test/src/modules/sync_buffer_unittest.cc \
#                   ../../../test/audio_test/src/modules/timestamp_scaler_unittest.cc \
#                   ../../../test/audio_test/src/modules/time_stretch_unittest.cc \
#                   ../../../test/audio_test/src/modules/audio_loop.cc \
#                   ../../../test/audio_test/src/modules/input_audio_file.cc \
#                   ../../../test/audio_test/src/modules/rtp_generator.cc \
#                   ../../../test/audio_test/src/modules/NETEQTEST_DummyRTPpacket.cc \
#                   ../../../test/audio_test/src/modules/NETEQTEST_RTPpacket.cc \
#                   ../../../test/audio_test/src/modules/system_delay_unittest.cc \
#                   ../../../test/audio_test/src/modules/echo_cancellation_unittest.cc \
#                   ../../../test/audio_test/src/modules/echo_cancellation_impl_unittest.cc \
#                   ../../../test/audio_test/src/modules/ring_buffer_unittest.cc \
#                   ../../../test/audio_test/src/modules/bitrate_controller_unittest.cc \
#                   ../../../test/audio_test/src/modules/byte_io_unittest.cc \
#                   ../../../test/audio_test/src/modules/fec_receiver_unittest.cc \
#                   ../../../test/audio_test/src/modules/fec_test_helper.cc \
#                   ../../../test/audio_test/src/modules/nack_rtx_unittest.cc \
#                   ../../../test/audio_test/src/modules/producer_fec_unittest.cc \
#                   ../../../test/audio_test/src/modules/receive_statistics_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtcp_receiver_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtcp_sender_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtcp_format_remb_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_fec_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_format_vp8_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_format_vp8_test_helper.cc \
#                   ../../../test/audio_test/src/modules/rtp_packet_history_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_payload_registry_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_rtcp_impl_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_utility_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_header_extension_unittest.cc \
#                   ../../../test/audio_test/src/modules/rtp_sender_unittest.cc \
#                   ../../../test/audio_test/src/modules/vp8_partition_aggregator_unittest.cc \
#                   ../../../test/audio_test/src/modules/test_api.cc \
#                   ../../../test/audio_test/src/modules/test_api_audio.cc \
#                   ../../../test/audio_test/src/modules/test_api_rtcp.cc \
#                   ../../../test/audio_test/src/modules/test_api_video.cc \
#                   ../../../test/audio_test/src/modules/delay_estimator_unittest.cc
#                   ../../../../../mediaengine/webrtc/modules/audio_coding/main/acm2/acm_neteq_unittest.cc \

LOCAL_STATIC_LIBRARIES := test_support \
			gflags \
			gmock

LOCAL_LDLIBS    := \
		-llog -lz \
                -L$(LIBS_PATH) \
                -lavs -lmediaengine -lopus -lre -lssl -lcrypto -lcpufeatures -llog -lz -lGLESv2 -lgtest \
                $(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/armeabi/libc++_static.a

include $(BUILD_EXECUTABLE)

ZPATH := $(LOCAL_PATH)

#include $(ZPATH)/../../audio_test/android/test_support/jni/Android.mk
include $(ZPATH)/../../gflags/jni/Android.mk
include $(ZPATH)/../../gmock/jni/Android.mk