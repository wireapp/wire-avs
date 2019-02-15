MENG_CPPFLAGS_webrtc/modules/audio_device/ += \
	-Imediaengine/webrtc/modules/audio_device/include

# XXX The gypi file drops dummy/file_audio_device_factory.cc when build
#     for Chrome. Maybe we want to do that to?
#
MENG_SRCS += \
	webrtc/modules/audio_device/audio_device_buffer.cc \
	webrtc/modules/audio_device/audio_device_generic.cc \
	webrtc/modules/audio_device/audio_device_impl.cc \
	webrtc/modules/audio_device/fine_audio_buffer.cc \
	webrtc/modules/audio_device/dummy/audio_device_dummy.cc \
	webrtc/modules/audio_device/dummy/file_audio_device.cc \
	webrtc/modules/audio_device/dummy/file_audio_device_factory.cc


# XXX The gypi file includes all source files for all platforms for some
#     reason. We only add the appropriate ones. Might be wrong ...
#
#     I suspect they have some stupid magic going in GYP, though.
#

ifeq ($(AVS_OS),linux)
MENG_CPPFLAGS_webrtc/modules/audio_device/ += \
	-Imediaengine/webrtc/modules/audio_device/linux \
	-DLINUX_ALSA

MENG_CXXFLAGS_webrtc/modules/audio_device/linux/ += \
	-Dtypeof=decltype

MENG_SRCS += \
	webrtc/modules/audio_device/linux/alsasymboltable_linux.cc \
	webrtc/modules/audio_device/linux/audio_device_alsa_linux.cc \
	webrtc/modules/audio_device/linux/audio_mixer_manager_alsa_linux.cc \
	webrtc/modules/audio_device/linux/latebindingsymboltable_linux.cc

# Let's not go overboard and assume we have pulse audio, too.
#
#MENG_CPPFLAGS += -DLINUX_PULSE

#MENG_SRCS += \
	webrtc/modules/audio_device/linux/audio_device_pulse_linux.cc \
	webrtc/modules/audio_device/linux/audio_mixer_manager_pulse_linux.cc \
	webrtc/modules/audio_device/linux/pulseaudiosymboltable_linux.cc \


else ifeq ($(AVS_OS),ios)
MENG_CPPFLAGS += \
	-Imediaengine/webrtc/modules/audio_device/ios

MENG_OCPPFLAGS_webrtc/modules/audio_device/ios/ += \
	-fobjc-arc

MENG_OCPPFLAGS_webrtc/modules/audio_device/ios/objc/ += \
	-fobjc-arc

MENG_SRCS += \
	webrtc/modules/audio_device/ios/audio_device_ios.mm \
	webrtc/modules/audio_device/ios/audio_device_not_implemented_ios.mm \
	webrtc/modules/audio_device/ios/voice_processing_audio_unit.mm \
	webrtc/modules/audio_device/ios/objc/RTCAudioSession+Configuration.mm \
	webrtc/modules/audio_device/ios/objc/RTCAudioSession.mm \
	webrtc/modules/audio_device/ios/objc/RTCAudioSessionConfiguration.m \
	webrtc/modules/audio_device/ios/objc/RTCAudioSessionDelegateAdapter.mm
 
else ifeq ($(AVS_OS),osx)
MENG_CPPFLAGS_webrtc/modules/audio_device/ += \
	-Imediaengine/webrtc/modules/audio_device/mac 

MENG_SRCS += \
	webrtc/modules/audio_device/mac/audio_device_mac.cc \
	webrtc/modules/audio_device/mac/audio_mixer_manager_mac.cc \
	webrtc/modules/audio_device/mac/portaudio/pa_ringbuffer.c

else ifeq ($(AVS_OS),android)
MENG_CPPFLAGS_webrtc/modules/audio_device/ += \
	-Imediaengine/webrtc/modules/audio_device/android

MENG_CXXFLAGS_webrtc/modules/audio_device/android/ += \
-Dtypeof=decltype

MENG_SRCS += \
	webrtc/modules/audio_device/android/audio_manager.cc \
	webrtc/modules/audio_device/android/audio_record_jni.cc \
	webrtc/modules/audio_device/android/audio_track_jni.cc \
    webrtc/modules/audio_device/android/build_info.cc \
	webrtc/modules/audio_device/android/opensles_common.cc \
	webrtc/modules/audio_device/android/opensles_player.cc

else
$(error Unknown target OS $(AVS_OS)).
endif
