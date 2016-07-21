
MENG_SRCS += \
	webrtc/sound/nullsoundsystem.cc \
	webrtc/sound/nullsoundsystemfactory.cc \
	webrtc/sound/platformsoundsystem.cc \
	webrtc/sound/platformsoundsystemfactory.cc \
	webrtc/sound/soundsysteminterface.cc \
	webrtc/sound/soundsystemproxy.cc

ifeq ($(AVS_OS),linux)
MENG_CXXFLAGS_webrtc/sound/ += \
	-Dtypeof=decltype

MENG_SRCS += \
	webrtc/sound/alsasoundsystem.cc \
	webrtc/sound/alsasymboltable.cc \
	webrtc/sound/linuxsoundsystem.cc \
	webrtc/sound/pulseaudiosoundsystem.cc \
	webrtc/sound/pulseaudiosymboltable.cc

endif
