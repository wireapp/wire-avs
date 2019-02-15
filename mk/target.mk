#
# Build System Configuration
#
#
# This file defines a multitude of variables to configure this here build
# system. These definitions do not apply to dist_* targets. See mk/dist.mk
# for these.
#
# We keep two variables for the target system information. AVS_OS contains
# the operating sytem (or platform) we are building for. AVS_ARCH contains
# the CPU architecture we are building for.
#
# Currently we support these combinations:
#
#     AVS_OS=android		AVS_ARCH=armv7
#				AVS_ARCH=arm64	(currently disabled)
#     				AVS_ARCH=i386
#     				AVS_ARCH=x86_64	(currently disabled)
#     AVS_OS=linux		AVS_ARCH=x86_64
#     				AVS_ARCH=i386
#     AVS_OS=ios		AVS_ARCH=armv7
#     				AVS_ARCH=armv7s
#     				AVS_ARCH=arm64
#     				AVS_ARCH=x86_64	(simulator)
#     AVS_OS=osx		AVS_ARCH=x86_64
#
# The system we are building on is defined through HOST_OS and HOST_ARCH.
# Allowed pairs here are:
#
#     HOST_OS=linux		HOST_ARCH=x86_64|i386
#     HOST_OS=osx		HOST_ARCH=x86_64
#
# You don't need to define those, the system will auto-detect the host
# itself.
#
# Both of these are being merged into AVS_PAIR and HOST_PAIR which defines
# the full system as a single string.
#
# In addition, AVS_FAMILY as one of armv7, arm64, or x86 is declared and
# AVS_OS_FAMILY as linux or darwin.
#
# Some behaviour can be changed through flags. These are variables that
# should be defined to non-empty (the actual content doesn't matter) to
# enable something. All of them default to what you probably want by not
# being set.
#
# Here the current list:
#
#     ENABLE_DATA_LOGGING	enables data logging in mediaengine
#     				(whatever that means).
#
#     FORCE_RECORDING           force recording of calls.
#
#     NETEQ_LOGGING             log NetEQ status.
#
#     PREFER_FIXED_POINT	fixed point implementations are prefered.
#
#     SKIP_MK_DEPS		don't include dependencies on make snippets.
#     				This will avoid rebuilding everything when
#     				you are hacking away on the build system.
#
#     USE_X11			include code for X11.
#
# From all this information, this file defines various variables that are
# being passed as arguments to the tools for building. These are called
# "argument variables" throughout the system and all follow the same
# general structure: there is a well-defined set of these. This file
# defines them without any prefix. You will always want to use these.
# The various modules can define their own versions. These are then
# prefix with the modules own prefix and can be reused later by everything
# that uses the module. However, this can only happen if the using modules
# is after the defining one in the master Makefile's MK_COMPONENTS.
#
# Here's the complete set of argument variables in there unprefixed
# version as defined in this file:
#
#     CPPFLAGS         arguments to be passed to all C-flavour compilers:
#                      C, C++, Objective C both with C and C++ code.
#
#     CFLAGS           arguments passed to the compiler only when compiling
#                      C code.
#
#     CXXFLAGS         arguments passed to the compiler only when compiling
#                      C++ code.
#
#     OCPPFLAGS        arguments passed to the compiler when compiling
#                      Objective C code with either C or C++ code included.
#
#     JNICPPFLAGS      arguments passed to the compiler when compiling
#                      code for the Java Native Interface.
#
#     AFLAGS           flags given to ar.
#
#     DFLAGS           flags given to the compiler to produce dependency
#                      make files.
#
#     LFLAGS           flags given to the linker.
#
#     LIBS             libraries given to the linker, either as static
#                      library files or as -l flags. This is not just
#                      the library names.
#
#     SH_LFLAGS        flags given to the linker when linking shared
#                      libraries.
#
#     SH_LIBS          libraries necessary for linking shared libraries.
#                  
#
# The file also defines the various tools to use when building stuff. These
# are:
#
#     CC               the C compiler,
#     CXX              the C++ compiler,
#     LD               the linker,
#     AR               the archiving utility for static libraries,
#     AS               the assembler,
#     RANLIB           indexer for static libraries; it may be undefined
#                      in which case you should not index the library,
#     JAVAC            the Java compiler.
#
# More system configuration settings:
#
#     SYSROOT          the path the base of the system's library and include
#                      files; would be /usr in a "normal" system,
#     BIN_SUFFIX       the suffix of executables in the sytem,
#     LIB_SUFFIX       the suffix of shared libraries of the system,
#     JNI_SUFFIX       the suffix for JNI shared libraries,
#     HOST_OPTIONS     give this to ./configure scripts for cross compiling,
#
# Finally, the paths to the build directories are defined:
#
#     BUILD_BASE       the root directory for building into (note that the
#                      contrib libraries will build within their source tree;
#                      all our own stuff will put all its generated files
#                      somewhere under BUILD_BASE),
#     BUILD_TARGET     the root directory for building for the current
#                      system: $(BUILD_BASE)/$(AVS_PAIR),
#     BUILD_LIB        where libraries go: $(BUILD_TARGET)/lib,
#     BUILD_OBJ        where compiled object files go: $(BUILD_TARGET)/obj;
#                      each subproject should pick a directory under here
#                      and put all its files there,
#     BUILD_DIST_BASE  where distribution files go,
#     BUILD_BIN        where binaries go; if this is the host system, then
#                      they are placed in the root directory of the
#                      repository else in $(BUILD_TARGET)/bin
#
# If DIST is set to non-empty, BUILD_BIN will bin $(BUILD_TARGET)/bin no
# matter what.
#

# Here's all the AVS_OS and AVS_ARCH values we support
#
ALL_AVS_OS := android ios linux osx
ALL_AVS_ARCH := armv7 arm64 i386 x86_64


# Start by auto-determining host system and arch.
#
ifeq ($(HOST_OS),)
HOST_UNAME := $(shell uname)
ifeq ($(HOST_UNAME),Darwin)
HOST_OS := osx
else
ifeq ($(HOST_UNAME),Linux)
HOST_OS := linux
else
$(error Unknown host system)
endif
endif
endif

ifeq ($(HOST_ARCH),)
HOST_ARCH :=  $(shell uname -m | sed -e s/i686/i386/)
endif

# If we don't have a target set, we do the host target. If we have
# a AVS_OS but not AVS_ARCH, we assume something reasonable.
#
ifeq ($(AVS_OS),)
AVS_OS := $(HOST_OS)
AVS_ARCH := $(HOST_ARCH)
endif
ifeq ($(AVS_ARCH),)
ifeq ($(AVS_OS),android)
AVS_ARCH := armv7
endif
ifeq ($(AVS_OS),linux)
AVS_ARCH := $(HOST_ARCH)
endif
ifeq ($(AVS_OS),ios)
AVS_ARCH := arm64
endif
ifeq ($(AVS_OS),osx)
AVS_ARCH := x86_64
endif
endif

AVS_PAIR  := $(AVS_OS)-$(AVS_ARCH)
HOST_PAIR := $(HOST_OS)-$(HOST_ARCH)

# Allow overiding of tool locations
#
ifeq ($(XCRUN),)
XCRUN := xcrun
endif


#
# Detect if Compiler-Cache (ccache) is available
#
CCACHE	:= $(shell [ -e /usr/bin/ccache ] 2>/dev/null \
	|| [ -e /usr/local/bin/ccache ] \
	&& echo "ccache")


#--- Build paths ------------------------------------------------------------

BUILD_BASE := $(shell pwd)/build
BUILD_TARGET := $(BUILD_BASE)/$(AVS_PAIR)
BUILD_LIB := $(BUILD_TARGET)/lib
BUILD_OBJ := $(BUILD_TARGET)/obj
BUILD_DIST_BASE := $(BUILD_BASE)/dist

# Binaries go into the base dir if this is the host system or into
# $(BUILD_TARGET)/bin otherwise
#
ifneq ($(DIST),)
BUILD_BIN := $(BUILD_TARGET)/bin
else ifeq ($(AVS_OS)-$(AVS_ARCH),$(HOST_OS)-$(HOST_ARCH))
BUILD_BIN := .
else
BUILD_BIN := $(BUILD_TARGET)/bin
endif


#--- Generic settings -------------------------------------------------------

JAVAC := javac

AFLAGS := cr

CFLAGS   += \
         -fvisibility=hidden -Os -g -ffunction-sections -fdata-sections

CPPFLAGS += \
         -I. -DWEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE -DWEBRTC_CODEC_OPUS \
	 -DSSL_USE_OPENSSL -DFEATURE_ENABLE_SSL -D__STDC_FORMAT_MACROS=1 \
	 -DAVS_VERSION='"$(AVS_VERSION)"' -DAVS_PROJECT='"$(AVS_PROJECT)"' \
	 -I$(BUILD_TARGET)/include -Iinclude \
	 -Imediaengine -Imediaengine/webrtc \
	 -Icontrib/opus/include \
	 -Icontrib/opus/celt \
	 -Icontrib/opus/silk

CXXFLAGS += \
         -fvisibility=hidden -ffunction-sections -fdata-sections -Os -g \
         -std=c++11 -stdlib=libc++ 

ifeq ($(AVS_RELEASE),1)
 ifeq ($(AVS_OS),ios)
  CXXFLAGS += -DNDEBUG
 endif
 ifeq ($(AVS_OS),android)
  CXXFLAGS += -DNDEBUG
 endif
endif

# XXX Does just -MD do the right thing in clang?
#
#DFLAGS = -MD -MF $(@:.o=.d) -MT $@
DFLAGS := -MMD

LFLAGS += \
	-L$(BUILD_TARGET)/lib

# We have our own standalone tool chains for Android. They are being built
# in toolchain.mk.
#
# To make things easier, we define the relevant variables for every OS and
# ARCH.
#
TOOLCHAIN_BASE_PATH := $(BUILD_BASE)/toolchains
TOOLCHAIN_PATH := $(TOOLCHAIN_BASE_PATH)/$(AVS_OS)-$(AVS_ARCH)

ifneq ($(NETEQ_LOGGING),)
CPPFLAGS += -DNETEQ_LOGGING=1
endif
ifneq ($(FORCE_RECORDING),)
CPPFLAGS += -DFORCE_RECORDING=1
endif


#--- Platform settings ------------------------------------------------------

ifeq ($(AVS_ARCH),armv7)
AVS_FAMILY := armv7
CPPFLAGS += \
	-DWEBRTC_ARCH_ARM -DWEBRTC_ARCH_ARM_V7 -DWEBRTC_HAS_NEON
endif
ifeq ($(AVS_ARCH),armv7s)
AVS_FAMILY := armv7
CPPFLAGS += \
	-DWEBRTC_ARCH_ARM -DWEBRTC_ARCH_ARM_V7  -DWEBRTC_HAS_NEON
endif
ifeq ($(AVS_ARCH),arm64)
AVS_FAMILY := arm64
CPPFLAGS += \
	-DWEBRTC_ARCH_ARM -DWEBRTC_ARCH_ARM64
endif
ifeq ($(AVS_ARCH),i386)
AVS_FAMILY := x86
CPPFLAGS += \
	-msse2
endif
ifeq ($(AVS_ARCH),x86_64)
AVS_FAMILY := x86
CPPFLAGS += \
	-msse2
endif


#--- Android Target ---------------------------------------------------------
#
ifeq ($(AVS_OS),android)

AVS_OS_FAMILY := linux

# Cross-compiling tools have a prefix that's differing per architecture.
# We use this opportunity to check for a known $(AVS_ARCH).

CROSS_PREFIX_armv7  := arm-linux-androideabi
CROSS_PREFIX_arm64  := aarch64-linux-android
CROSS_PREFIX_i386   := i686-linux-android
CROSS_PREFIX_x86_64 := x86_64-linux-android
CROSS_PREFIX        := $(CROSS_PREFIX_$(AVS_ARCH))

ifeq ($(CROSS_PREFIX),)
$(error Unsupported arch $(AVS_ARCH))
endif

# All our tools
#
BIN_PATH     := $(TOOLCHAIN_PATH)/bin/$(CROSS_PREFIX)
CC           := $(BIN_PATH)-clang
CXX          := $(BIN_PATH)-clang++
GCC          := $(BIN_PATH)-gcc
GCXX         := $(BIN_PATH)-g++
LD           := $(CXX)
AR           := $(BIN_PATH)-ar
AS           := $(BIN_PATH)-as
RANLIB       := $(BIN_PATH)-ranlib
STRIP        := $(BIN_PATH)-strip
SYSROOT      := $(TOOLCHAIN_PATH)/sysroot/usr
HOST_OPTIONS := --host=$(CROSS_PREFIX)
LIB_SUFFIX   := .so
JNI_SUFFIX   := .so

# Settings
#
CPPFLAGS += \
         -I$(TOOLCHAIN_PATH)/ndk/sources/android/cpufeatures \
         -DWEBRTC_POSIX -DWEBRTC_LINUX -DWEBRTC_ANDROID \
         -DNO_STL -DZETA_AECM -DHAVE_GAI_STRERROR=1 \
	 -DANDROID -D__ANDROID__ \
	 -U__STRICT_ANSI__ \
	 -fPIC

# XXX Review these ...
CFLAGS   += \
	 -fpic \
	 -ffunction-sections -funwind-tables \
	 -fstack-protector -fno-short-enums \
	 -fomit-frame-pointer -fno-strict-aliasing \
	 -fPIC

CXXFLAGS += -nostdlib -fPIC

ifneq ($(BLA),)
LFLAGS   += \
	 -nostdlib -fPIC -Wl,-soname,libtwolib-second.so \
	 -Wl,--whole-archive -Wl,--no-undefined -Wl,--gc-sections
else
LFLAGS	+= \
	-fPIC \
	-L$(SYSROOT)/lib \
	-no-canonical-prefixes -Wl,--fix-cortex-a8  -Wl,--no-undefined \
	-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -mthumb
endif

SH_LFLAGS += \
	-shared

SH_LIBS += \
	$(TOOLCHAIN_PATH)/libc++/libc++_static.a

LIBS += \
	-lcpufeatures -lc -lm -ldl -llog -lGLESv2 -latomic -lOpenSLES

# this one was added to get ztest to link:
LIBS += \
	$(TOOLCHAIN_PATH)/libc++/libc++_static.a \
	$(TOOLCHAIN_PATH)/libc++/libc++abi.a \
	$(TOOLCHAIN_PATH)/libc++/libandroid_support.a

ifeq ($(AVS_ARCH),armv7)
LIBS +=	\
	$(TOOLCHAIN_PATH)/libc++/libunwind.a
endif

# Architecture Settings
#
ifeq ($(AVS_ARCH),armv7)
CPPFLAGS += \
	-march=armv7-a -mfpu=neon -mfloat-abi=softfp -mcpu=cortex-a8
SH_LIBS += \
	$(TOOLCHAIN_PATH)/libc++/libc++_static.a

else ifeq ($(AVS_ARCH),arm64)
CPPFLAGS += \
	-mfpu=neon
else ifeq ($(AVS_ARCH),i386)

else ifeq ($(AVS_ARCH),x86_64)

else
$(error Unknown architecture $(AVS_ARCH) for Android.)
endif


endif


#--- iOS Target -------------------------------------------------------------
#
ifeq ($(AVS_OS),ios)

AVS_OS_FAMILY := darwin

# SDK
#
ifeq ($(AVS_ARCH),x86_64)
SDK := iphonesimulator
HOST_OPTIONS := --host=arm-apple-darwin
else
SDK := iphoneos
endif
SDK_PATH := $(shell $(XCRUN) --show-sdk-path --sdk $(SDK))

# Tools
#
CC	   := $(shell $(XCRUN) --sdk $(SDK) -f clang)
CXX	   := $(shell $(XCRUN) --sdk $(SDK) -f clang++)
LD	   := $(CXX)
AR	   := $(shell $(XCRUN) --sdk $(SDK) -f ar)
RANLIB	   := $(shell $(XCRUN) --sdk $(SDK) -f ranlib)
SYSROOT    := $(SDK_PATH)/usr
LIB_SUFFIX := .dylib
JNI_SUFFIX := .jnilib

# Settings
#
CPPFLAGS += \
	 -arch $(AVS_ARCH) \
	 -isysroot $(SDK_PATH) \
	 -DCARBON_DEPRECATED=YES -DWEBRTC_POSIX -DWEBRTC_MAC -DWEBRTC_IOS \
	 -DZETA_IOS_STEREO_PLAYOUT -DHAVE_GAI_STRERROR=1 \
	 -DUSE_APPLE_COMMONCRYPTO \
	 -DIPHONE -pipe -no-cpp-precomp
LFLAGS	 += \
	 -arch $(AVS_ARCH) \
	 -isysroot $(SDK_PATH) \
	 -no-cpp-precomp \
	 -Wl,-read_only_relocs,suppress
SH_LFLAGS += -dynamiclib
LIBS	 += \
	-lz \
	-framework AudioToolbox \
	-framework AVFoundation \
	-framework Foundation \
	-framework Security \
	-framework SystemConfiguration \
	-framework UIKit

ifeq ($(SDK),iphoneos)
CPPFLAGS += \
         -miphoneos-version-min=8.0
LFLAGS	 += \
         -miphoneos-version-min=8.0
else
CPPFLAGS += \
         -mios-simulator-version-min=8.0
LFLAGS	 += \
         -mios-simulator-version-min=8.0
endif

# video
LIBS += \
	-framework OpenGLES \
	-framework QuartzCore \
	-framework CoreVideo \
	-framework CoreMedia

ifeq ($(AVS_FAMILY),armv7)
HOST_OPTIONS := --host=arm-apple-darwin
else ifeq ($(AVS_FAMILY),arm64)
HOST_OPTIONS := --host=arm-apple-darwin
endif

endif


#--- Linux Target -----------------------------------------------------------
#
ifeq ($(AVS_OS),linux)

AVS_OS_FAMILY := linux

# Tools
#
CC	   := $(CCACHE) /usr/bin/clang
CXX	   := $(CCACHE) /usr/bin/clang++
LD	   := /usr/bin/clang++
AR	   := /usr/bin/ar
RANLIB	   := /usr/bin/ranlib
SYSROOT	   := /usr
LIB_SUFFIX := .so
JNI_SUFFIX := .so

# Settings
#
CPPFLAGS += \
         -DWEBRTC_POSIX -DWEBRTC_LINUX -DHAVE_GAI_STRERROR=1

SH_LFLAGS += -shared

LIBS += -lX11 -lXcomposite -lXdamage -lXext -lXfixes -lXrender

LIBS += -ldl -lrt -lm -lc++ -lc++abi

ifneq ($(USE_X11),)
LIBS += -lX11 -lXcomposite -lXdamage -lXext -lXfixes -lXrender
endif

ifeq ($(AVS_ARCH),armv6)
LIBS += -lunwind
endif

ifeq ($(AVS_ARCH),armv7)
LIBS += -lunwind
endif

ifeq ($(AVS_ARCH),armv7l)
LIBS += -lunwind
endif

endif


#--- OSX Target -------------------------------------------------------------
#
ifeq ($(AVS_OS),osx)

AVS_OS_FAMILY := darwin

# SDK
#
SDK      := macosx
SDK_PATH := $(shell $(XCRUN) --show-sdk-path --sdk $(SDK))

# Tools
#
CC	   := $(CCACHE) $(shell $(XCRUN) --sdk $(SDK) -f clang)
CXX	   := $(CCACHE) $(shell $(XCRUN) --sdk $(SDK) -f clang++)
LD	   := $(CXX)
AR	   := $(shell $(XCRUN) --sdk $(SDK) -f ar)
RANLIB	   := $(shell $(XCRUN) --sdk $(SDK) -f ranlib)
SYSROOT    := $(SDK_PATH)/usr
LIB_SUFFIX := .dylib
JNI_SUFFIX := .jnilib

# Settings
#
CPPFLAGS += \
         -isysroot $(SDK_PATH) \
         -DWEBRTC_POSIX -DWEBRTC_MAC -DZETA_USING_AU_HAL -DHAVE_GAI_STRERROR=1 \
	 -pipe -no-cpp-precomp \
	 -mmacosx-version-min=10.9
LFLAGS   += \
         -isysroot $(SDK_PATH) \
	 -no-cpp-precomp
SH_LFLAGS += -dynamiclib

JNICPPFLAGS := \
	-I/System/Library/Frameworks/JavaVM.framework/Versions/A/Headers \
	-I$(SDK_PATH)/System/Library/Frameworks/JavaVM.framework/Versions/A/Headers

LIBS	+= \
	-framework AppKit \
	-framework ApplicationServices \
	-framework AudioToolbox \
	-framework AudioUnit \
	-framework Cocoa \
	-framework CoreAudio \
	-framework CoreFoundation \
	-framework CoreGraphics \
	-framework Foundation \
	-framework IOKit \
	-framework OpenGL \
	-framework Security \
	-framework SystemConfiguration

LIBS += \
	-framework CoreVideo \
	-framework QTKit \
	-framework CoreMedia \
	-framework AVFoundation

ifneq ($(AVS_ARCH),i386)
CPPFLAGS	+= -DCARBON_DEPRECATED=YES
else
LIBS	+= \
	-framework Carbon
endif

endif

