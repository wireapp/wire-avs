#
# Makefile Snippet for AVS Core Library
#
# 
# This snippet build the AVS core library which provides basic functionality
# and some useful stuff and then some.
#
# The results of this snippet are:
#
#    o  a static library in build/$(AVS_PAIR)/lib/libavscore.a.
#
# The snippet's main target "avs" builds this library.
#
# The header files for this library are in include. They are not copied.
# Instead, the $(AVS_CPPFLAGS) argument variable adds an include path
# there.
#
# The snippets only builds those modules listed in $(AVS_MODULES). The
# canonical place to add to this variable is below. Each module must live
# in src/<module_name> and have a file src/<module_name>/mod.mk adding (!)
# all its source files to $(AVS_SRCS) relative to the src directory (ie.,
# starting with the module name portion of the path). Only source files
# listed in those mod.mk files are being built.
#
# There are currently no facilities to add module specific argument
# variables. Instead, add anything you may need to the respective
# argument variables in here. They are prefixed AVS_ and are later
# also added by all things that use libcoreavs.
#
# For information on argument variables, see mk/target.mk.
#

#--- AVS Core Modules ---

AVS_MODULES += aucodec
AVS_MODULES += base
AVS_MODULES += cert
AVS_MODULES += conf_pos
AVS_MODULES += config
ifneq ($(HAVE_CRYPTOBOX),)
AVS_MODULES += cryptobox
endif
AVS_MODULES += dce
AVS_MODULES += devpair
AVS_MODULES += dict
AVS_MODULES += ecall
AVS_MODULES += econn
AVS_MODULES += econn_fmt
AVS_MODULES += extmap
AVS_MODULES += flowmgr
AVS_MODULES += jzon
AVS_MODULES += kase
AVS_MODULES += log
AVS_MODULES += media
AVS_MODULES += mediamgr
AVS_MODULES += msystem
AVS_MODULES += nevent
AVS_MODULES += netprobe
AVS_MODULES += network
ifneq ($(HAVE_PROTOBUF),)
AVS_MODULES += protobuf
endif
AVS_MODULES += queue
AVS_MODULES += rest
AVS_MODULES += sem
AVS_MODULES += serial
AVS_MODULES += store
AVS_MODULES += string
AVS_MODULES += trace
AVS_MODULES += turn
AVS_MODULES += uuid
AVS_MODULES += version
AVS_MODULES += vidcodec
AVS_MODULES += voe
AVS_MODULES += vie
AVS_MODULES += wcall
AVS_MODULES += icall
AVS_MODULES += egcall
AVS_MODULES += audio_io
AVS_MODULES += audio_effect
AVS_MODULES += audummy
AVS_MODULES += extcodec
AVS_MODULES += rtpdump
AVS_MODULES += zapi
AVS_MODULES += ztime
AVS_MODULES += mediastats

AVS_MODULES += engine
AVS_MODULES += $(EXTRA_MODULES)


#--- Variable Definitions ---

AVS_STATIC := $(BUILD_TARGET)/lib/libavscore.a

AVS_OBJ_PATH := $(BUILD_OBJ)/avs

AVS_MODMKS := $(patsubst %,src/%/mod.mk,$(AVS_MODULES))
AVS_MKS := $(OUTER_MKS) mk/avs.mk $(AVS_MODMKS)

include $(AVS_MODMKS)

AVS_C_OBJS := $(patsubst %.c,$(AVS_OBJ_PATH)/%.o,$(filter %.c,$(AVS_SRCS)))
AVS_CC_OBJS := $(patsubst %.cpp,$(AVS_OBJ_PATH)/%.o,$(filter %.cpp,$(AVS_SRCS)))
AVS_M_OBJS := $(patsubst %.m,$(AVS_OBJ_PATH)/%.o,$(filter %.m,$(AVS_SRCS)))
AVS_MM_OBJS := $(patsubst %.mm,$(AVS_OBJ_PATH)/%.o,$(filter %.mm,$(AVS_SRCS)))

AVS_OBJS := $(AVS_C_OBJS) $(AVS_CC_OBJS) $(AVS_M_OBJS) $(AVS_MM_OBJS)

AVS_CPPFLAGS += \
	-I$(BUILD_TARGET)/include/re \
	-I$(BUILD_TARGET)/include/rew \
	-Iinclude \
	-Werror

ifneq ($(HAVE_PROTOBUF),)
AVS_CPPFLAGS += -DHAVE_PROTOBUF=1
endif
ifneq ($(HAVE_CRYPTOBOX),)
AVS_CPPFLAGS += -DHAVE_CRYPTOBOX=1
endif

AVS_DEPS := $(CONTRIB_LIBRE_TARGET) \
	$(CONTRIB_OPUS_TARGET) \
	$(CONTRIB_LIBREW_TARGET) \
	$(CONTRIB_USRSCTP_TARGET) \
	$(CONTRIB_SODIUM_TARGET)
AVS_LIBS += $(CONTRIB_LIBRE_LIBS) \
	$(CONTRIB_OPUS_LIBS) \
	$(CONTRIB_LIBREW_LIBS) \
	$(CONTRIB_USRSCTP_LIBS)

ifneq ($(HAVE_CRYPTOBOX),)
AVS_DEPS += $(CONTRIB_CRYPTOBOX_TARGET)
AVS_LIBS += $(CONTRIB_CRYPTOBOX_LIBS)
endif

ifneq ($(HAVE_PROTOBUF),)
AVS_DEPS += $(CONTRIB_PROTOBUF_TARGET)
AVS_LIBS += $(CONTRIB_PROTOBUF_LIBS)
endif

AVS_LIBS += \
	$(CONTRIB_SODIUM_LIBS)


ifeq ($(AVS_OS),android)
AVS_LIBS += $(CONTRIB_AND_IFADDRS_LIBS)
endif

AVS_LIB_FILES += $(CONTRIB_LIBRE_LIB_FILES) \
	$(CONTRIB_OPUS_LIB_FILES) \
	$(CONTRIB_LIBREW_LIB_FILES) \
	$(CONTRIB_USRSCTP_LIB_FILES) \
	$(CONTRIB_SODIUM_LIB_FILES)

#--- Dependency Targets ---
#

$(AVS_OBJS): $(TOOLCHAIN_MASTER) \
	     $(CONTRIB_LIBRE_TARGET) \
	     $(CONTRIB_OPUS_TARGET) \
	     $(CONTRIB_LIBREW_TARGET) \
	     $(CONTRIB_USRSCTP_TARGET)

ifeq ($(SKIP_MK_DEPS),)
$(AVS_OBJS):  $(AVS_MKS)
endif

-include $(AVS_OBJS:.o=.d)


#--- Building Targets ---

$(AVS_C_OBJS): $(AVS_OBJ_PATH)/%.o: src/%.c
	@echo "  CC   $(AVS_OS)-$(AVS_ARCH) src/$*.c"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CFLAGS) \
		$(AVS_CPPFLAGS_$(dir $*)) $(AVS_CFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(AVS_CC_OBJS): $(AVS_OBJ_PATH)/%.o: src/%.cpp
	@echo "  CXX  $(AVS_OS)-$(AVS_ARCH) src/$*.cpp"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CXXFLAGS) \
		$(AVS_CPPFLAGS_$(dir $*)) $(AVS_CXXFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(AVS_M_OBJS): $(AVS_OBJ_PATH)/%.o: src/%.m
	@echo "  OC $(AVS_OS)-$(AVS_ARCH) src/$*.m"
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(OCPPFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CFLAGS) \
		$(AVS_CPPFLAGS_$(dir $*)) $(AVS_CFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(AVS_MM_OBJS): $(AVS_OBJ_PATH)/%.o: src/%.mm
	@echo "  OCXX $(AVS_OS)-$(AVS_ARCH) src/$*.mm"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OCPPFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CXXFLAGS) \
		$(AVS_CPPFLAGS_$(dir $*)) $(AVS_CXXFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)


$(AVS_STATIC): $(AVS_OBJS)
	@echo "  AR   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@$(AR) $(AFLAGS) $@ $(filter %.o,$^)
ifneq ($(RANLIB),)
	@$(RANLIB) $@
endif


#--- Install Targets ---

.PHONY: avs_headers
avs_headers:
	@mkdir -p $(BUILD_TARGET)/include/avs
	@cp -a include/* $(BUILD_TARGET)/include/avs

#--- Phony Targets ---

.PHONY: avs avs_clean
avs: $(AVS_STATIC) avs_headers
avs_clean:
	@rm -f $(AVS_STATIC)
	@rm -rf $(AVS_OBJ_PATH)

