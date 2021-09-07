
TOOL 		:= verifyd
verifyd_SRCS	+= \
		main.c

verifyd_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
verifyd_CFLAGS := $(AVS_CFLAGS) $(AVS_CFLAGS)
verifyd_LIBS := $(AVS_LIBS) $(MENG_LIBS)
verifyd_DEPS := $(AVS_DEPS) $(MENG_DEPS)
verifyd_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)

include mk/tool.mk
