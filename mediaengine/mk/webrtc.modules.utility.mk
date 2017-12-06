MENG_CPPFLAGS_webrtc/modules/utility/source/ += \
	-Imediaengine/webrtc/system_wrappers/interface

MENG_SRCS += \
	webrtc/modules/utility/source/audio_frame_operations.cc \
	webrtc/modules/utility/source/coder.cc \
	webrtc/modules/utility/source/file_player_impl.cc \
	webrtc/modules/utility/source/file_recorder_impl.cc \
	webrtc/modules/utility/source/process_thread_impl.cc

ifeq ($(AVS_OS),android)
MENG_SRCS += \
	webrtc/modules/utility/source/helpers_android.cc \
	webrtc/modules/utility/source/jvm_android.cc

endif

ifeq ($(AVS_OS),ios)
MENG_SRCS += \
    webrtc/modules/utility/source/helpers_ios.mm

endif
