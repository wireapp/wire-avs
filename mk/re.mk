#
# Excerpt of relevant parts of libre's re.mk
#
# 
# The upstream version employs various tests to auto-detect the correct
# settings for your platform. Unfortunately, some of these collide with
# our own magic.
#
# Since we know our platforms, we can manually set the relevant flags.
# Note that we add them to the AVS_ scoped variables so as to not pollute
# the flags for those parts of the repository that does not use libre.
#
# Part of what re.mk does (most of the compiler related stuff) is already
# in mk/target.mk. Yeah, that is all a bit messy. But hey, build system.
#

# libre's own build system installs the headers in $(BUILD_TARGET)/include/re
# but we #include <re.h>. So add the right -I
#
AVS_CPPFLAGS += -I$(BUILD_TARGET)/include/re

# XXX Compiler warnings -- review
#
AVS_CFLAGS	+= -Wall
AVS_CFLAGS	+= -Wmissing-declarations
AVS_CFLAGS	+= -Wmissing-prototypes
AVS_CFLAGS	+= -Wstrict-prototypes
AVS_CFLAGS	+= -Wbad-function-cast
AVS_CFLAGS	+= -Wsign-compare
AVS_CFLAGS	+= -Wnested-externs
AVS_CFLAGS	+= -Wshadow
AVS_CFLAGS	+= -Waggregate-return
AVS_CFLAGS	+= -Wcast-align


# OS section
#
# XXX Is android == linux and ios == darwin right?

ifeq ($(AVS_OS),android)
	AVS_CFLAGS += -fPIC -DLINUX -DOS=\"linux\"
	AVS_LIBS += -ldl
	AVS_LFLAGS += -fPIC
endif

ifeq ($(AVS_OS),ios)
	AVS_CFLAGS += -fPIC -DDARWIN -DOS=\"darwin\"
	AVS_LFLAGS += -fPIC
endif

ifeq ($(AVS_OS),linux)
	AVS_CFLAGS += -fPIC -DLINUX -DOS=\"linux\"
	AVS_LIBS += -ldl
	AVS_LFLAGS += -fPIC
endif

ifeq ($(AVS_OS),osx)
	AVS_CFLAGS += -fPIC -DDARWIN -DOS=\"darwin\"
	AVS_LFLAGS += -fPIC
endif


# Architecture section
#
# libre uses slightly different arch names
#

ifeq ($(AVS_ARCH),armv7)
	AVS_CFLAGS += -DARCH=\"arm\"
endif
ifeq ($(AVS_ARCH),armv7s)
	AVS_CFLAGS += -DARCH=\"arm\"
endif
ifeq ($(AVS_ARCH),armv7l)
	AVS_CFLAGS += -DARCH=\"arm\"
endif
ifeq ($(AVS_ARCH),arm64)
	AVS_CFLAGS += -DARCH=\"arm64\"
endif
ifeq ($(AVS_ARCH),i386)
	AVS_CFLAGS += -DARCH=\"i686\"
endif
ifeq ($(AVS_ARCH),x86_64)
	AVS_CFLAGS += -DARCH=\"x86_64\"
endif


# External libraries section
#

# OpenSSL
#
AVS_CPPFLAGS += \
	-DUSE_OPENSSL -DUSE_TLS \
	-DUSE_OPENSSL_DTLS -DUSE_DTLS \
	-DUSE_OPENSSL_SRTP -DUSE_DTLS_SRTP
#AVS_LIBS += -lssl -lcrypto

# zlib
#
AVS_CPPFLAGS += -DUSE_ZLIB
AVS_LIBS += -lz

# pthread
#
AVS_CPPFLAGS += -DHAVE_PTHREAD
ifneq ($(AVS_OS),android)
AVS_LIBS += -lpthread
endif

# Various features
#
AVS_CPPFLAGS += \
	-DHAVE_GETIFADDRS -DHAVE_STRERROR_R -DHAVE_GETOPT \
	-DHAVE_INTTYPES_H -DHAVE_NET_ROUTE_H -DHAVE_SYS_SYSCTL_H \
	-DHAVE_STDBOOL_H -DHAVE_INET6 -DHAVE_SYSLOG -DHAVE_LIBRESOLV \
	-DHAVE_FORK -DHAVE_INET_NTOP -DHAVE_PWD_H -DHAVE_SELECT \
	-DHAVE_SELECT_H -DHAVE_SETRLIMIT -DHAVE_SIGNAL -DHAVE_SYS_TIME_H \
	-DHAVE_UNAME -DHAVE_UNISTD_H
ifneq ($(AVS_OS),android)
AVS_LIBS += -lresolv
endif

ifeq ($(AVS_OS_FAMILY),linux)
AVS_CPPFLAGS += -DHAVE_POLL -DHAVE_INET_PTON -DHAVE_EPOLL
endif
