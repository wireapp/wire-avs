#
# Makefile
#

TARGET		:= voe_loopback_test
SYSROOT		:= $(shell xcrun --show-sdk-path)

LIB_PATH         := ../../../../build/dist/osx/avsball/lib
MEDIAENGINE_PATH := ../../../../mediaengine

CXX		:= /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
CXXFLAGS	:= -std=c++11 -fvisibility=default \
		   -isysroot $(SYSROOT) -DWEBRTC_POSIX -I$(MEDIAENGINE_PATH)

LD		:= $(CXX)
LDFLAGS		:= -L$(LIB_PATH) -lavsobjc \
	-framework ApplicationServices \
	-framework AudioToolbox \
	-framework AudioUnit \
	-framework CoreAudio \
	-framework AVFoundation

SOURCES = \
	../../src/voe_loopback_test.cpp

OBJECTS = \
	$(patsubst %.c,%.o,$(filter %.c,$(SOURCES))) \
	$(patsubst %.cpp,%.o,$(filter %.cpp,$(SOURCES))) \
	$(patsubst %.cc,%.o,$(filter %.cc,$(SOURCES)))

all:	$(TARGET)

$(OBJECTS): Makefile
#$(OBJECTS):

$(TARGET): $(OBJECTS)
	@echo "  LD      $@"
	@$(LD) -o $@ $^ $(LDFLAGS)


%.o:	%.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)


%.o:	%.cpp
	@echo "  CXX     $@"
	@$(CXX) $(CXXFLAGS) $(TARGET_CFLAGS) -c $< -o $@ $(DFLAGS)


%.o:	%.cc
	@echo "  CXX     $@"
	@$(CXX) $(CXXFLAGS) -c $< -o $@ $(DFLAGS)


clean:
	@echo " CLEAN "
	@rm -f $(TARGET) $(OBJECTS)

info:
	@echo SYSROOT=$(SYSROOT)
	@echo TARGET=$(TARGET)
	@echo SOURCES=$(SOURCES)
	@echo OBJECTS=$(OBJECTS)

version:
	@$(CXX) -v



