#
# mod.mk
#

AVS_SRCS += \
	mediamgr/mediamgr.c \
	mediamgr/sound.c

ifeq ($(AVS_OS),ios)
IS_IOS := true
endif
ifeq ($(AVS_OS),iossim)
IS_IOS := true
endif

ifneq ($(IS_IOS),)

AVS_SRCS += \
	mediamgr/mm_platform_iosx.m

else ifeq ($(AVS_OS),osx)

AVS_SRCS += \
	mediamgr/mm_platform_dummy.c

else ifeq ($(AVS_OS),android)

AVS_SRCS += \
    mediamgr/mm_platform_android.c

else

AVS_SRCS += \
	mediamgr/mm_platform_dummy.c

endif


