#
# mod.mk
#

ifeq ($(AVS_OS),osx)
KALIUM_REAL := true
endif

ifeq ($(AVS_OS),linux)
KALIUM_REAL := true
endif

ifneq ($(KALIUM_REAL),)
AVS_SRCS += \
	kalium/kcall.c \
	kalium/octotunnel.c \
	kalium/pgm.c \
	kalium/test_capturer.c \
	kalium/test_view.c
else
AVS_SRCS += \
	kalium/kcall_dummy.c
endif


