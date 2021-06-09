#
# mod.mk
#

AVS_SRCS += \
	protobuf/wrap.c \
	protobuf/protobuf.c \
	protobuf/proto/messages.pb-c.c

AVS_CPPFLAGS	+= -Isrc/protobuf

# XXX try to avoid this
AVS_CPPFLAGS	+= $(shell pkg-config --cflags 'libprotobuf-c >= 1.0.0')


src/protobuf/wrap.c:	src/protobuf/proto/messages.pb-c.c
