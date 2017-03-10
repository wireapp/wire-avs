MENG_CPPFLAGS_webrtc/voice_engine/ += \
	-DAVS_DISABLE_MULTIPARTY_OPT

MENG_SRCS += \
	webrtc/voice_engine/channel.cc \
	webrtc/voice_engine/channel_manager.cc \
	webrtc/voice_engine/channel_proxy.cc \
	webrtc/voice_engine/level_indicator.cc \
	webrtc/voice_engine/monitor_module.cc \
	webrtc/voice_engine/network_predictor.cc \
	webrtc/voice_engine/output_mixer.cc \
	webrtc/voice_engine/shared_data.cc \
	webrtc/voice_engine/statistics.cc \
	webrtc/voice_engine/transmit_mixer.cc \
	webrtc/voice_engine/utility.cc \
	webrtc/voice_engine/voe_audio_processing_impl.cc \
	webrtc/voice_engine/voe_base_impl.cc \
	webrtc/voice_engine/voe_codec_impl.cc \
	webrtc/voice_engine/voe_external_media_impl.cc \
	webrtc/voice_engine/voe_file_impl.cc \
	webrtc/voice_engine/voe_hardware_impl.cc \
	webrtc/voice_engine/voe_neteq_stats_impl.cc \
	webrtc/voice_engine/voe_network_impl.cc \
	webrtc/voice_engine/voe_rtp_rtcp_impl.cc \
	webrtc/voice_engine/voe_video_sync_impl.cc \
	webrtc/voice_engine/voe_volume_control_impl.cc \
	webrtc/voice_engine/voice_engine_impl.cc

