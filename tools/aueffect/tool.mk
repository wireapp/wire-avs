TOOL 		:= aueffect
aueffect_SRCS	+= \
		main.c

aueffect_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
aueffect_CFLAGS := $(AVS_CFLAGS) $(AVS_CFLAGS)
aueffect_LIBS := $(AVS_LIBS) $(MENG_LIBS)
aueffect_DEPS := $(AVS_DEPS) $(MENG_DEPS)
aueffect_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)

include mk/tool.mk
