#
# tool.mk
#

TOOL 		:= siptest
siptest_SRCS	+= \
		main.c

VIEW_FILE = 
PLATFORM_FILES =

siptest_SRCS	+= $(VIEW_FILE) $(PLATFORM_FILES)


siptest_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
siptest_CFLAGS := $(AVS_CFLAGS) $(MENG_CFLAGS)

#sectest_LIBS	:= $(AVS_LIBS) $(MENG_LIBS)
siptest_LIBS	:= $(AVS_LIBS)

siptest_LIBS	+= -lpthread -lbaresip

siptest_DEPS := $(AVS_DEPS) $(MENG_DEPS)
siptest_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)

include mk/tool.mk
