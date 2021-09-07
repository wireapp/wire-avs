#
# tool.mk
#

ifeq ($(AVS_OS),osx)
HAVE_READLINE	:= 1
endif
ifeq ($(AVS_OS),linux)
HAVE_READLINE	:= 1
endif


TOOL 		:= zcall
zcall_SRCS	+= \
		calling3.c \
		client.c \
		clilog.c \
		conv.c \
		io.c \
		main.c \
		options.c \
		restsrv.c \
		user.c \
		view.c \
		test_view.c \
		test_capturer.c \
		octotunnel.c \
		pgm.c


VIEW_FILE = 
PLATFORM_FILES =

ifeq ($(AVS_OS),osx)
VIEW_FILE = osx_view.m
PLATFORM_FILES = ../../iosx/src/flowmgr/AVSCapturer.mm \
	../../iosx/src/flowmgr/AVSVideoViewOSX.m
endif

zcall_SRCS	+= $(VIEW_FILE) $(PLATFORM_FILES)


zcall_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
zcall_CFLAGS := $(AVS_CFLAGS) $(MENG_CFLAGS)

#zcall_LIBS	:= $(AVS_LIBS) $(MENG_LIBS)
zcall_LIBS	:= $(AVS_LIBS)

ifneq ($(HAVE_READLINE),)
zcall_CPPFLAGS	+= -DHAVE_READLINE=1
zcall_LIBS	+= -lreadline
endif

zcall_LIBS	+= -lpthread

zcall_DEPS := $(AVS_DEPS) $(MENG_DEPS)
zcall_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)


ifneq ($(HAVE_PROTOBUF),)
zcall_DEPS += $(CONTRIB_PROTOBUF_TARGET)
zcall_LIBS += $(CONTRIB_PROTOBUF_LIBS)
endif

ifneq ($(HAVE_CRYPTOBOX),)
zcall_DEPS += $(CONTRIB_CRYPTOBOX_TARGET)
zcall_LIBS += $(CONTRIB_CRYPTOBOX_LIBS)
endif


include mk/tool.mk
