
MENG_SRCS += \
	webrtc/modules/video_capture/device_info_impl.cc \
	webrtc/modules/video_capture/video_capture_factory.cc \
	webrtc/modules/video_capture/video_capture_impl.cc

ifeq ($(AVS_OS),linux)
MENG_SRCS += \
	webrtc/modules/video_capture/linux/device_info_linux.cc \
	webrtc/modules/video_capture/linux/video_capture_linux.cc

else ifeq ($(AVS_OS),osx)
MENG_SRCS += \
	webrtc/modules/video_capture/mac/video_capture_mac.mm \
	webrtc/modules/video_capture/mac/qtkit/video_capture_qtkit.mm \
	webrtc/modules/video_capture/mac/qtkit/video_capture_qtkit_info.mm \
	webrtc/modules/video_capture/mac/qtkit/video_capture_qtkit_info_objc.mm \
	webrtc/modules/video_capture/mac/qtkit/video_capture_qtkit_objc.mm

else ifeq ($(AVS_OS),android)
MENG_SRCS += \
	webrtc/modules/video_capture/android/device_info_android.cc \
	webrtc/modules/video_capture/android/video_capture_android.cc

else ifeq ($(AVS_OS),ios)
MENG_CPPFLAGS_webrtc/modules/video_capture/ios/ += \
	-fobjc-arc

MENG_SRCS += \
	webrtc/modules/video_capture/ios/device_info_ios.mm \
	webrtc/modules/video_capture/ios/device_info_ios_objc.mm \
	webrtc/modules/video_capture/ios/rtc_video_capture_ios_objc.mm \
	webrtc/modules/video_capture/ios/video_capture_ios.mm

endif

