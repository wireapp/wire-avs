#
# Master Makefile
#
# 
# Logic is split up into various modules. See MK_COMPONENTS below for the
# complete list. Each of them provides a target identical to its name. The
# default target will build the tools for your host system.
#
# Configuration can be found in mk/target.mk. This file also contains a
# list of the variables you can pass into make to influence building.
#
# For cross compiling, AVS_OS and AVS_ARCH are your most important variables.
# AVS_OS selects the platform to build for and is one of android, ios, linux,
# and osx. AVS_ARCH select the architecture within the platform and is one of
# armv7, armv7s, arm64, i386, and x86_64. Not all architectures are
# available for all platforms.
#
# Both have "sane" defaults. AVS_OS defaults to your host platform. If you
# only state AVS_OS, AVS_ARCH will pick the most likely architecture for
# that platform.
#
# Call "make info" for a listing of some of the variables.
#

# Master version number
#

ifeq ($(BUILD_NUMBER),)
VER_PATCH := local
else
VER_PATCH := $(BUILD_NUMBER)
endif

VER_BRANCH := $(shell git rev-parse --abbrev-ref HEAD || echo "")

ifeq ($(AVS_VERSION),)
ifeq ($(word 1, $(subst -, , $(VER_BRANCH))), release)
AVS_PROJECT := avs
AVS_RELEASE := 1
VER_MAJOR_MINOR := $(word 2, $(subst -, , $(VER_BRANCH)))
AVS_VERSION := $(VER_MAJOR_MINOR).$(VER_PATCH)
else
AVS_PROJECT := avsmaster
AVS_RELEASE := 0
AVS_VERSION := 0.0.$(VER_PATCH)
endif
else
ifeq ($(shell echo $(AVS_VERSION) | head -c 3),0.0)
AVS_PROJECT := avsmaster
AVS_RELEASE := 0
else
AVS_PROJECT := avs
AVS_RELEASE := 1
endif
endif

MK_COMPONENTS := toolchain contrib avs tools test android iosx dist scan


#--- Configuration ---

-include config.mk


ifeq ($(HAVE_WEBRTC),)
ifeq ($(AVS_OS),wasm)
HAVE_WEBRTC := 0
else
HAVE_WEBRTC := 1
endif
endif

include mk/target.mk
include mk/re.mk

OUTER_MKS := Makefile mk/target.mk


#
# NOTE: must be defined here, after target.mk is included
#
ifeq ($(AVS_OS),osx)
HAVE_PROTOBUF	:= 1
HAVE_CRYPTOBOX	:= 1
BUILD_OPTIONAL_MODULES := 1
endif
ifeq ($(AVS_OS),linux)
HAVE_PROTOBUF	:= 1
HAVE_CRYPTOBOX	:= 1
BUILD_OPTIONAL_MODULES := 1
endif

ifneq ($(AVS_OS),wasm)
BUILD_NETWORK_MODULES := 1
endif

#--- All My Targets ---

all: tools test

ifeq ($(AVS_OS),android)
wrapper: android
else ifeq ($(AVS_OS),ios)
wrapper: iosx
else ifeq ($(AVS_OS),osx)
wrapper: iosx
else
wrapper:
	echo "There ain't no wrapper for $(AVS_OS)."
endif

clean: $(patsubst %,%_clean,$(MK_COMPONENTS))

distclean: clean
	@rm -rf $(BUILD_BASE)

include $(patsubst %,mk/%.mk,$(MK_COMPONENTS))

info:
	@echo "        AVS_OS: $(AVS_OS)"
	@echo "      AVS_ARCH: $(AVS_ARCH)"
	@echo "    AVS_FAMILY: $(AVS_FAMILY)"
	@echo "       HOST_OS: $(HOST_OS)"
	@echo "     HOST_ARCH: $(HOST_ARCH)"
	@echo "        CCACHE: $(CCACHE)"
	@echo "            CC: $(CC)"
	@echo "           CXX: $(CXX)"
	@echo "            LD: $(LD)"
	@echo "            AR: $(AR)"
	@echo "            AS: $(AS)"
	@echo "        RANLIB: $(RANLIB)"
	@echo "      CPPFLAGS: $(CPPFLAGS)"
	@echo "        CFLAGS: $(CFLAGS)"
	@echo "      CXXFLAGS: $(CXXFLAGS)"
	@echo "        LFLAGS: $(LFLAGS)"
	@echo "          LIBS: $(LIBS)"
	@echo "      AVS_LIBS: $(AVS_LIBS)"
	@echo "      AVS_DEPS: $(AVS_DEPS)"
	@echo "     BUILD_BIN: $(BUILD_BIN)"
	@echo "  BUILD_TARGET: $(BUILD_TARGET)"
	@echo "           SDK: $(SDK)"
	@echo " HAVE_PROTOBUF: $(HAVE_PROTOBUF)"
	@echo "HAVE_CRYPTOBOX: $(HAVE_CRYPTOBOX)"
	@echo "   HAVE_WEBRTC: $(HAVE_WEBRTC)"

version:
	@echo "$(AVS_VERSION)"


avs_release:
	git archive --format=tar --prefix=$(AVS_PROJECT)-$(AVS_VERSION)/ \
		HEAD | gzip > $(AVS_PROJECT)-$(AVS_VERSION).tar.gz
