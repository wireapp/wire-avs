#
# mod.mk
#

AVS_SRCS += \
    audio_io/audio_io.cpp \
	audio_io/mock/fake_audiodevice.cpp \
	audio_io/record/record_audiodevice.cpp

ifeq ($(AVS_OS),ios)
IS_IOS := true
endif
ifeq ($(AVS_OS),iossim)
IS_IOS := true
endif

ifneq ($(IS_IOS),)

AVS_SRCS += \
	audio_io/ios/audio_io_ios.mm

endif
