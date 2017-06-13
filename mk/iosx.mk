#
# Makefile Snippet for iOSX Wrappers
#
#
# This snippet builds the code for use by iOS and OSX clients, ie., the
# code that lives in iosx.
# 
# The results of this snippet are:
#
#    o  a shared library build/$(AVS_PAIR)/lib/libavsobjc.dylib with the
#       release code,
#
#    o  a shared library build/$(AVS_PAIR)/lib/libavsobjcstub.dylib with
#       the stub code,
#
#    o  a static library build/$(AVS_PAIR)/lib/libavsiox.a with the
#       release code from the iosx directory only,
#
#    o  a static library build/$(AVS_PAIR)/lib/libavsobjc.a combining all
#       the static libraries necessary to build an application using the
#       release code from the iosx directory,
#
#    o  a static library build/$(AVS_PAIR)/lib/libavsobjcstub.a with the
#       stub code from the iosx directory allowing to build an application
#       using iosx APIs but lacking the actual flow manager functionality
#       thus being really quite small for quick building.
#
# The snippet's main target "iosx" builds all of these. There are also
# targets "iosx_shared" and "iosx_static" that only build the shared and
# static libraries, respectively.
#
# The snippet requires the modules in iosx added to the IOSX_MODULES
# variable. The canonical place to do that is right here below. Each listed
# module must live in iosx/src in its own subdirectory by the module name.
# Within that directory must be a file mod.mk listing the source files
# provided by the module relative to the iosx/src directory (ie., starting
# with the module name portion of the path). The sources have to be added (!)
# to one of three variables: IOSX_LIB_SRCS contains the sources of the
# release code, IOSX_STUB_SRCS contains the sources of the stub code, and
# IOSX_ANY_SRCS contains sources used for both.
#
# There are currently no facilities to add module specific argument
# variables. Instead, add anything you may need to the respective
# argument variables in here. They are prefixed IOSX_.
#
# For information on argument variables, see mk/target.mk.
# 

# List of modules
#
IOSX_MODULES += flowmgr
IOSX_MODULES += mediamgr
IOSX_MODULES += videoutil
IOSX_MODULES += audioutil

IOSX_MODMKS := $(patsubst %,iosx/src/%/mod.mk,$(IOSX_MODULES))
IOSX_MKS    := $(OUTER_MKS) mk/iosx.mk $(IOSX_MODMKS)

IOSX_LIB_NAME := avsiosx
IOSX_FULL_NAME := avsobjc
IOSX_STUB_NAME := avsobjcstub

IOSX_FULL_SHARED := $(BUILD_LIB)/lib$(IOSX_FULL_NAME)$(LIB_SUFFIX)
IOSX_STUB_SHARED := $(BUILD_LIB)/lib$(IOSX_STUB_NAME)$(LIB_SUFFIX)
IOSX_LIB_STATIC := $(BUILD_LIB)/lib$(IOSX_LIB_NAME).a
IOSX_FULL_STATIC := $(BUILD_LIB)/lib$(IOSX_FULL_NAME).a
IOSX_STUB_STATIC := $(BUILD_LIB)/lib$(IOSX_STUB_NAME).a
IOSX_LIB_LIBS := $(IOSX_SHARED) $(IOSX_STATIC)
IOSX_STUB_LIBS := $(IOSX_STUB_SHARED) $(IOSX_STUB_STATIC)
IOSX_SHARED := $(IOSX_FULL_SHARED) $(IOSX_STUB_SHARED)
IOSX_STATIC := $(IOSX_LIB_STATIC) $(IOSX_STUB_STATIC) $(IOSX_FULL_STATIC)
IOSX_MASTER := $(IOSX_STATIC) $(IOSX_SHARED)

IOSX_OBJ_PATH := $(BUILD_OBJ)/iosx

include $(IOSX_MODMKS)

IOSX_SRCS := $(IOSX_LIB_SRCS) $(IOSX_STUB_SRCS) $(IOSX_ANY_SRCS)

IOSX_LIB_SRCS += $(IOSX_ANY_SRCS)
IOSX_STUB_SRCS += $(IOSX_ANY_SRCS)

IOSX_LIB_OBJS := \
	$(patsubst %.m,$(IOSX_OBJ_PATH)/%.o,$(filter %.m,$(IOSX_LIB_SRCS))) \
	$(patsubst %.mm,$(IOSX_OBJ_PATH)/%.o,$(filter %.mm,$(IOSX_LIB_SRCS)))
IOSX_STUB_OBJS := \
	$(patsubst %.m,$(IOSX_OBJ_PATH)/%.o,$(filter %.m,$(IOSX_STUB_SRCS))) \
	$(patsubst %.mm,$(IOSX_OBJ_PATH)/%.o,$(filter %.mm,$(IOSX_STUB_SRCS)))

IOSX_M_OBJS := $(patsubst %.m,$(IOSX_OBJ_PATH)/%.o,\
			$(filter %.m,$(IOSX_SRCS)))
IOSX_MM_OBJS := $(patsubst %.mm,$(IOSX_OBJ_PATH)/%.o,\
			$(filter %.mm,$(IOSX_SRCS)))
IOSX_OBJS := $(IOSX_M_OBJS) $(IOSX_MM_OBJS)

IOSX_CPPFLAGS += \
	-Iiosx/include

IOSX_OCPPFLAGS += \
	 -fobjc-arc

IOSX_LIBS += -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox

ifeq ($(AVS_OS),ios)
IOSX_LIBS += -framework MobileCoreServices
endif

#--- Target Dependencies ---

$(IOSX_OBJS): $(TOOLCHAIN_MASTER) $(AVS_DEPS)
$(IOSX_LIB_OBJS): $(MENG_DEPS)

ifeq ($(SKIP_MK_DEPS),)
$(IOSX_OBJS): $(IOSX_MKS)
endif

-include $(IOSX_OBJS:.o=.d)

#--- Building Targets ---

$(IOSX_M_OBJS): $(IOSX_OBJ_PATH)/%.o: iosx/src/%.m
	@echo "  OC   $(AVS_OS)-$(AVS_ARCH) iosx/src/$*.m"
	@mkdir -p $(dir $@)
	$(CC)  -fobjc-arc -fmodules \
		-Xclang -fmodule-implementation-of -Xclang avs \
		$(CPPFLAGS) $(CFLAGS) $(OCPPFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CFLAGS) $(AVS_OPPCFLAGS) \
		$(IOSX_CPPFLAGS) $(IOSX_CFLAGS) $(IOSX_OCPPFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(IOSX_MM_OBJS): $(IOSX_OBJ_PATH)/%.o: iosx/src/%.mm
	@echo "  OCXX $(AVS_OS)-$(AVS_ARCH) iosx/src/$*.mm"
	@mkdir -p $(dir $@)
	@$(CXX) -fvisibility=default $(CPPFLAGS) $(CXXFLAGS) $(OCPPFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CXXFLAGS) $(AVS_OPPCFLAGS) \
		$(IOSX_CPPFLAGS) $(IOSX_CXXFLAGS) $(IOSX_OCPPFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(IOSX_LIB_STATIC): $(IOSX_LIB_OBJS)
$(IOSX_STUB_STATIC): $(IOSX_STUB_OBJS)
$(IOSX_LIB_STATIC) $(IOSX_STUB_STATIC):
	@echo "  AR   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@$(AR) $(AFLAGS) $@ $(filter %.o,$^)
ifneq ($(RANLIB),)
	@$(RANLIB) $@
endif

$(IOSX_FULL_STATIC): $(IOSX_LIB_STATIC) $(AVS_STATIC) $(MENG_STATIC) \
		     $(AVS_LIB_FILES) $(MENG_LIB_FILES)
	libtool -static -v -o $@ $^

$(IOSX_FULL_SHARED): $(IOSX_LIB_OBJS) $(AVS_STATIC) $(MENG_STATIC)
	@echo "  LD      $@"
	@mkdir -p $(dir $@)
	@mkdir -p $(BUILD_LIB)/avs.framework
	$(LD)	-install_name @rpath/avs.framework/avs \
		-Xlinker -rpath -Xlinker @executable_path/Frameworks \
		-Xlinker -rpath -Xlinker @loader_path/Frameworks \
		-Xlinker -no_implicit_dylibs \
		-single_module \
		$(SH_LFLAGS) $(LFLAGS) $(IOSX_LFLAGS) \
		$^ $(SH_LIBS) $(LIBS) $(AVS_LIBS) $(MENG_LIBS) $(IOSX_LIBS) \
		-o $(BUILD_LIB)/avs.framework/avs

$(IOSX_STUB_SHARED): $(IOSX_STUB_OBJS) $(AVS_STATIC) $(MENG_STATIC)
	@echo "  LD      $@"
	@mkdir -p $(dir $@)
	$(LD)   $(SH_LFLAGS) $(LFLAGS) $(IOSX_LFLAGS) \
		-ObjC \
		$^ $(SH_LIBS) $(LIBS) $(AVS_LIBS) $(MENG_LIBS) $(IOSX_LIBS) \
		-o $@

#--- Phony Targets ---

.PHONY: iosx iosx_static iosx_shared iosx_clean
iosx: $(IOSX_MASTER)
iosx_static: $(IOSX_STATIC)
iosx_shared: $(IOSX_SHARED)
iosx_clean:
	@rm -f $(IOSX_MASTER)
	@rm -rf $(IOSX_OBJ_PATH)

