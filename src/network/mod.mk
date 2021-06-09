#
# mod.mk
#

AVS_SRCS += \
	network/dns.c \
	network/sa.c \

ifeq ($(AVS_OS),ios)

AVS_SRCS += \
        network/dns_platform_iosx.m

else ifeq ($(AVS_OS),osx)

AVS_SRCS += \
        network/dns_platform_dummy.c

else ifeq ($(AVS_OS),android)

AVS_SRCS += \
	network/dns_platform_android.c

else

AVS_SRCS += \
        network/dns_platform_dummy.c

endif

