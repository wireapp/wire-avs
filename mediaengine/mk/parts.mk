# mediaengine - Build system
#
# All our source files
#
include mediaengine/mk/libyuv.mk
include mediaengine/mk/webrtc.mk
include mediaengine/mk/webrtc.audio.mk
include mediaengine/mk/webrtc.base.mk
include mediaengine/mk/webrtc.call.mk
include mediaengine/mk/webrtc.common_audio.mk
include mediaengine/mk/webrtc.common_video.mk
include mediaengine/mk/webrtc.modules.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.interfaces.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.cng.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.g711.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.isac.main.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.opus.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.pcm16b.mk
include mediaengine/mk/webrtc.modules.audio_coding.codecs.red.mk
include mediaengine/mk/webrtc.modules.audio_coding.main.mk
include mediaengine/mk/webrtc.modules.audio_coding.neteq.mk
include mediaengine/mk/webrtc.modules.audio_conference_mixer.mk
include mediaengine/mk/webrtc.modules.audio_device.mk
include mediaengine/mk/webrtc.modules.audio_processing.mk
include mediaengine/mk/webrtc.modules.bitrate_controller.mk
include mediaengine/mk/webrtc.modules.congestion_controller.mk
include mediaengine/mk/webrtc.modules.desktop_capture.mk
include mediaengine/mk/webrtc.modules.media_file.mk
include mediaengine/mk/webrtc.modules.pacing.mk
include mediaengine/mk/webrtc.modules.remote_bitrate_estimator.mk
include mediaengine/mk/webrtc.modules.rtp_rtcp.mk
include mediaengine/mk/webrtc.modules.utility.mk
# We provide capturers in wrappers, so webrtc ones are unnecessary
# include mediaengine/mk/webrtc.modules.video_capture.mk
include mediaengine/mk/webrtc.modules.video_coding.codecs.h264.mk
include mediaengine/mk/webrtc.modules.video_coding.codecs.i420.mk
include mediaengine/mk/webrtc.modules.video_coding.codecs.vp8.mk
include mediaengine/mk/webrtc.modules.video_coding.codecs.vp9.mk
include mediaengine/mk/webrtc.modules.video_coding.main.mk
include mediaengine/mk/webrtc.modules.video_coding.utility.mk
include mediaengine/mk/webrtc.modules.video_processing.mk
# We provide renderers in wrappers, so webrtc ones are unnecessary
# include mediaengine/mk/webrtc.modules.video_render.mk
include mediaengine/mk/webrtc.sdk.mk
include mediaengine/mk/webrtc.system_wrappers.mk
include mediaengine/mk/webrtc.video.mk
include mediaengine/mk/webrtc.voice_engine.mk
