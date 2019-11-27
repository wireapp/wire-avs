IOSX_WEBRTC_SRCS += \
	RTCAudioSession+Configuration.mm \
	UIDevice+RTCDevice.mm

IOSX_WEBRTC_CPPFLAGS += \
	-Icontrib/webrtc/$(WEBRTC_VER)/include/sdk/objc/components/audio \
	-Icontrib/webrtc/$(WEBRTC_VER)/include/sdk/objc/helpers
