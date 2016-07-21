MENG_CPPFLAGS_webrtc/modules/audio_coding/neteq/ += \
	-Icontrib/opus/celt \
	-Icontrib/opus/opus \
	-Icontrib/opus/silk \
        -Icontrib/opus/src \
	-Icontrib/opus/include

MENG_SRCS += \
	webrtc/modules/audio_coding/neteq/accelerate.cc \
	webrtc/modules/audio_coding/neteq/audio_classifier.cc \
	webrtc/modules/audio_coding/neteq/audio_decoder_impl.cc \
	webrtc/modules/audio_coding/neteq/audio_multi_vector.cc \
	webrtc/modules/audio_coding/neteq/audio_vector.cc \
	webrtc/modules/audio_coding/neteq/background_noise.cc \
	webrtc/modules/audio_coding/neteq/buffer_level_filter.cc \
	webrtc/modules/audio_coding/neteq/comfort_noise.cc \
	webrtc/modules/audio_coding/neteq/cross_correlation.cc \
	webrtc/modules/audio_coding/neteq/decision_logic.cc \
	webrtc/modules/audio_coding/neteq/decision_logic_fax.cc \
	webrtc/modules/audio_coding/neteq/decision_logic_normal.cc \
	webrtc/modules/audio_coding/neteq/decoder_database.cc \
	webrtc/modules/audio_coding/neteq/delay_manager.cc \
	webrtc/modules/audio_coding/neteq/delay_peak_detector.cc \
	webrtc/modules/audio_coding/neteq/dsp_helper.cc \
	webrtc/modules/audio_coding/neteq/dtmf_buffer.cc \
	webrtc/modules/audio_coding/neteq/dtmf_tone_generator.cc \
	webrtc/modules/audio_coding/neteq/expand.cc \
	webrtc/modules/audio_coding/neteq/merge.cc \
	webrtc/modules/audio_coding/neteq/nack.cc \
	webrtc/modules/audio_coding/neteq/neteq_impl.cc \
	webrtc/modules/audio_coding/neteq/neteq.cc \
	webrtc/modules/audio_coding/neteq/normal.cc \
	webrtc/modules/audio_coding/neteq/packet.cc \
	webrtc/modules/audio_coding/neteq/packet_buffer.cc \
	webrtc/modules/audio_coding/neteq/payload_splitter.cc \
	webrtc/modules/audio_coding/neteq/post_decode_vad.cc \
	webrtc/modules/audio_coding/neteq/preemptive_expand.cc \
	webrtc/modules/audio_coding/neteq/random_vector.cc \
	webrtc/modules/audio_coding/neteq/rtcp.cc \
	webrtc/modules/audio_coding/neteq/statistics_calculator.cc \
	webrtc/modules/audio_coding/neteq/sync_buffer.cc \
	webrtc/modules/audio_coding/neteq/tick_timer.cc \
	webrtc/modules/audio_coding/neteq/timestamp_scaler.cc \
	webrtc/modules/audio_coding/neteq/time_stretch.cc

