#
# mod.mk
#

AVS_SRCS += \
    audio_io/audio_io.cpp \
	audio_io/mock/fake_audiodevice.cpp

ifeq ($(AVS_OS),ios)

AVS_SRCS += \
	audio_io/ios/audio_io_ios.mm

endif
