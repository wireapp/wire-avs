#
# mod.mk
#

AVS_SRCS += \
	voe/decode.cpp \
	voe/device.cpp \
	voe/encode.cpp \
	voe/shared.cpp \
	voe/voe.cpp \
	voe/audio_test.cpp \
	voe/stats.cpp \
	voe/voe_debug.cpp \
	voe/channel_settings.cpp

ifeq ($(AVS_OS),ios)
AVS_SRCS += \
	voe/voe_iosfilepath.mm
endif

AVS_CPPFLAGS_src/voe := \
	-Imediaengine
