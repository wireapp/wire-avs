
TOOL 		:= netprobe
netprobe_SRCS	+= \
		main.c

netprobe_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
netprobe_CFLAGS := $(AVS_CFLAGS) $(AVS_CFLAGS)
netprobe_LIBS := $(AVS_LIBS) $(MENG_LIBS)
netprobe_DEPS := $(AVS_DEPS) $(MENG_DEPS)
netprobe_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)

include mk/tool.mk
