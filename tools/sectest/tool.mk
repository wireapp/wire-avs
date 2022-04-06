#
# tool.mk
#

ifeq ($(AVS_OS),osx)
HAVE_READLINE	:= 1
endif
ifeq ($(AVS_OS),linux)
HAVE_READLINE	:= 1
endif


TOOL 		:= sectest
sectest_SRCS	+= \
		main.c \
		system.c \
		http.c \
		ccall_wrapper.c \
		ecall_wrapper.c


VIEW_FILE = 
PLATFORM_FILES =

sectest_SRCS	+= $(VIEW_FILE) $(PLATFORM_FILES)


sectest_CPPFLAGS := $(AVS_CPPFLAGS) $(MENG_CPPFLAGS)
sectest_CFLAGS := $(AVS_CFLAGS) $(MENG_CFLAGS)

#sectest_LIBS	:= $(AVS_LIBS) $(MENG_LIBS)
sectest_LIBS	:= $(AVS_LIBS)

sectest_LIBS	+= -lpthread

sectest_DEPS := $(AVS_DEPS) $(MENG_DEPS)
sectest_LIB_FILES := $(AVS_STATIC) $(MENG_STATIC)

include mk/tool.mk
