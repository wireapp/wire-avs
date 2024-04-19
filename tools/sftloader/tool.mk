#
# tool.mk
#

ifeq ($(AVS_OS),osx)
HAVE_READLINE	:= 1
endif
ifeq ($(AVS_OS),linux)
HAVE_READLINE	:= 1
endif


TOOL 		:= sftloader
sftloader_SRCS	+= \
		sftloader.c

sftloader_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
sftloader_CFLAGS := $(AVS_CFLAGS) $(MENG_CFLAGS)

#sftloader_LIBS	:= $(AVS_LIBS) $(MENG_LIBS)
sftloader_LIBS	:= $(AVS_LIBS)

ifneq ($(HAVE_READLINE),)
sftloader_CPPFLAGS	+= -DHAVE_READLINE=1
sftloader_LIBS	+= -lreadline
endif

ifneq ($(AVS_OS),android)
sftloader_LIBS	+= -lpthread
endif

sftloader_DEPS := $(AVS_DEPS) $(MENG_DEPS)
sftloader_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)


ifneq ($(HAVE_PROTOBUF),)
sftloader_DEPS += $(CONTRIB_PROTOBUF_TARGET)
sftloader_LIBS += $(CONTRIB_PROTOBUF_LIBS)
endif

ifneq ($(HAVE_CRYPTOBOX),)
sftloader_DEPS += $(CONTRIB_CRYPTOBOX_TARGET)
sftloader_LIBS += $(CONTRIB_CRYPTOBOX_LIBS)
endif


include mk/tool.mk
