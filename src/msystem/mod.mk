#
# mod.mk
#

AVS_SRCS += \
	msystem/msystem.c

ifeq ($(AVS_OS),ios)

AVS_SRCS += \
	msystem/msys_ios.m
else
AVS_SRCS += \
	msystem/msys_default.c
endif

