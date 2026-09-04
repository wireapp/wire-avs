#
# mod.mk
#


AVS_SRCS += \
	wcall/wcall.c \
	wcall/event.c \
	wcall/marshal.c

ifeq ($(AVS_OS),osx)
AVS_SRCS += wcall/sip.c
endif

ifeq ($(AVS_OS),linux)
AVS_SRCS += wcall/sip.c
endif

