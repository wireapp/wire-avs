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
		user.c


PLATFORM_FILES =

ifeq ($(AVS_OS),osx)
PLATFORM_FILES = ../../iosx/src/flowmgr/AVSCapturer.mm \
	../../iosx/src/flowmgr/AVSVideoViewOSX.m \
	../../iosx/src/flowmgr/OSXView.m

endif

zcall_SRCS	+= $(PLATFORM_FILES)

ifneq ($(filter osx linux,$(AVS_OS)),)
zcall_DESKTOP_SRC_DIRS := \
		src/protobuf \
		src/protobuf/proto \
		src/cryptobox \
		src/engine \
		src/rtpdump \
		src/store
zcall_DESKTOP_SRCS := $(patsubst src/%,../../src/%,\
		$(foreach dir,$(zcall_DESKTOP_SRC_DIRS),\
			$(wildcard $(dir)/*.c $(dir)/*.cpp)))
zcall_SRCS	+= $(zcall_DESKTOP_SRCS)
endif


zcall_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
zcall_CFLAGS := $(AVS_CFLAGS) $(MENG_CFLAGS)

#zcall_LIBS	:= $(AVS_LIBS) $(MENG_LIBS)
zcall_LIBS	:= $(AVS_LIBS)

ifneq ($(HAVE_READLINE),)
zcall_CPPFLAGS	+= -DHAVE_READLINE=1
zcall_LIBS	+= -lreadline
endif

ifneq ($(AVS_OS),android)
zcall_LIBS	+= -lpthread
endif

zcall_DEPS := $(AVS_DEPS) $(MENG_DEPS)
zcall_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)


ifneq ($(filter osx linux,$(AVS_OS)),)
zcall_CPPFLAGS += \
	-DHAVE_PROTOBUF=1 \
	-DHAVE_CRYPTOBOX=1 \
	-Isrc/protobuf \
	$(shell pkg-config --cflags 'libprotobuf-c >= 1.0.0')
zcall_DEPS += $(CONTRIB_PROTOBUF_TARGET) $(CONTRIB_CRYPTOBOX_TARGET)
zcall_LIBS += $(CONTRIB_PROTOBUF_LIBS) $(CONTRIB_CRYPTOBOX_LIBS)
endif


include mk/tool.mk

ifneq ($(filter osx linux,$(AVS_OS)),)
zcall_DESKTOP_OBJS := \
		$(patsubst %.c,$(TOOLS_OBJ_PATH)/zcall/%.o,\
			$(filter %.c,$(zcall_DESKTOP_SRCS))) \
		$(patsubst %.cpp,$(TOOLS_OBJ_PATH)/zcall/%.o,\
			$(filter %.cpp,$(zcall_DESKTOP_SRCS)))
$(zcall_DESKTOP_OBJS): CPPFLAGS += $(zcall_CPPFLAGS)
$(zcall_DESKTOP_OBJS): CFLAGS += $(zcall_CFLAGS)
endif
