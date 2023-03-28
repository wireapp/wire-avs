#
# mod.mk
#

AVS_SRCS += \
	msystem/msystem.c

ifeq ($(AVS_OS),ios)
IS_IOS := true
endif
ifeq ($(AVS_OS),iossim)
IS_IOS := true
endif

ifneq ($(IS_IOS),)

AVS_SRCS += \
	msystem/msys_ios.m
else
AVS_SRCS += \
	msystem/msys_default.c
endif

