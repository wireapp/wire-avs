#
# Makefile
#

TARGET		:= test_effect
SYSROOT		:= $(shell xcrun --show-sdk-path)

LIB_PATH         := ../../../../build/dist/osx/avsball/lib
MEDIAENGINE_PATH := ../../../../mediaengine
CONTRIB_PATH	 := ../../../../contrib
AVS_PATH	:= ../../../../include

CXX		:= /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
CXXFLAGS	:= -std=c++11 -fvisibility=default \
		   -isysroot $(SYSROOT) -DWEBRTC_POSIX -I$(MEDIAENGINE_PATH) -I$(CONTRIB_PATH) -I$(CONTRIB_PATH)/opus/include -I$(CONTRIB_PATH)/opus/celt -I$(CONTRIB_PATH)/opus/silk -I$(AVS_PATH)

LD		:= $(CXX)
LDFLAGS		:= -L$(LIB_PATH) -lavsobjc -framework CoreFoundation -framework ApplicationServices -framework Foundation

SOURCES = ../../src/test_effects.cpp

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



