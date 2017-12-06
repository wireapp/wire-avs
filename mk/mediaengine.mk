#
# Makefile Snippet for the Media Engine
#
# 
# This snippet builds the media engine which consists mostly of the code
# from Google's WebRTC project plus our own extensions.
#
# The result of this snippet is:
#
#    o  a static library build/$(AVS_PAIR)/lib/libmediaengine.a.
#
# The main target "mediaengine" builds this library. For the lazy, this
# target is also available as "meng" which also is the prefix used for
# all argument variables exported by this snippet.
#
# See mk/target.mk for what argument variables are and which are
# recognized.
#

MENG_PARTS_MK := mediaengine/mk/parts.mk
MENG_MKS := $(OUTER_MKS) mk/mediaengine.mk $(MENG_PARTS_MK)

include $(MENG_PARTS_MK)

MENG_STATIC := $(BUILD_TARGET)/lib/libmediaengine.a
MENG_OBJ_PATH := $(BUILD_OBJ)/mediaengine

# Gather all object files
#
MENG_C_OBJS  := $(patsubst %.c,$(MENG_OBJ_PATH)/%.o,\
			$(filter %.c,$(MENG_SRCS)))
MENG_CC_OBJS := $(patsubst %.cc,$(MENG_OBJ_PATH)/%.o,\
			$(filter %.cc,$(MENG_SRCS)))
MENG_M_OBJS  := $(patsubst %.m,$(MENG_OBJ_PATH)/%.o,\
			$(filter %.m,$(MENG_SRCS)))
MENG_MM_OBJS := $(patsubst %.mm,$(MENG_OBJ_PATH)/%.o,\
			$(filter %.mm,$(MENG_SRCS)))
MENG_S_OBJS  := $(patsubst %.S,$(MENG_OBJ_PATH)/%.o,\
			$(filter %.S,$(MENG_SRCS)))

MENG_OBJS := $(MENG_C_OBJS) $(MENG_CC_OBJS) $(MENG_M_OBJS) \
	     $(MENG_MM_OBJS) $(MENG_S_OBJS)

MENG_DEPS      := $(CONTRIB_OPUS_TARGET)
MENG_LIBS      := $(CONTRIB_OPUS_LIBS)
MENG_LIB_FILES := $(CONTRIB_OPUS_LIB_FILES)

# video
MENG_DEPS      += $(CONTRIB_LIBVPX_TARGET)
MENG_LIBS      += $(CONTRIB_LIBVPX_LIBS)
MENG_LIB_FILES += $(CONTRIB_LIBVPX_LIB_FILES)

ifeq ($(AVS_OS),linux)
MENG_LIBS      += -levent
endif

MENG_CPPFLAGS += \
	-Imediaengine/chromium


#--- Dependency Targets ---

$(MENG_OBJS): $(TOOLCHAIN_MASTER) $(MENG_DEPS)

ifeq ($(SKIP_MK_DEPS),)
$(MENG_OBJS): $(MENG_MKS)
endif

-include $(MENG_OBJS:.o=.d)


#--- Building Targets ---

$(MENG_C_OBJS): $(MENG_OBJ_PATH)/%.o: mediaengine/%.c
	@echo "  CC   $(AVS_OS)-$(AVS_ARCH) mediaengine/$*.c"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) \
		$(MENG_CPPFLAGS) $(MENG_CFLAGS) \
		$(MENG_CPPFLAGS_$(dir $*)) $(MENG_CFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(MENG_CC_OBJS): $(MENG_OBJ_PATH)/%.o: mediaengine/%.cc
	@echo "  CXX  $(AVS_OS)-$(AVS_ARCH) mediaengine/$*.cc"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(MENG_CPPFLAGS) $(MENG_CXXFLAGS) \
		$(MENG_CPPFLAGS_$(dir $*)) $(MENG_CXXFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(MENG_M_OBJS): $(MENG_OBJ_PATH)/%.o: mediaengine/%.m
	@echo "  OCC  $(AVS_OS)-$(AVS_ARCH) mediaengine/$*.m"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) $(OCPPFLAGS) \
		$(MENG_CPPFLAGS) $(MENG_CFLAGS) \
		$(MENG_CPPFLAGS_$(dir $*)) $(MENG_CFLAGS_$(dir $*)) \
		$(MENG_OCPPFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(MENG_MM_OBJS): $(MENG_OBJ_PATH)/%.o: mediaengine/%.mm
	@echo "  OCXX $(AVS_OS)-$(AVS_ARCH) mediaengine/$*.mm"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OCPPFLAGS) \
		$(MENG_CPPFLAGS) $(MENG_CXXFLAGS) \
		$(MENG_CPPFLAGS_$(dir $*)) $(MENG_CXXFLAGS_$(dir $*)) \
		$(MENG_OCPPFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(MENG_S_OBJS): $(MENG_OBJ_PATH)/%.o: mediaengine/%.S
	@echo "  ASM  $(AVS_OS)-$(AVS_ARCH) mediaengine/$*.S"
	@mkdir -p $(dir $@)
	@$(CXX) -x assembler-with-cpp $(CPPFLAGS) $(MENG_CPPFLAGS) \
		$(MENG_CPPFLAGS_$(dir $*)) \
		-c $< -o $@ $(DFLAGS)

$(MENG_STATIC): $(MENG_OBJS)
	@echo "  AR   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@$(AR) $(AFLAGS) $@ $(filter %.o,$^)
ifneq ($(RANLIB),)
	@$(RANLIB) $@
endif


#--- Phony Targets ---

.PHONY: mediaengine mediaengine_clean
mediaengine: $(MENG_STATIC)
meng: $(MENG_STATIC)
mediaengine_clean:
	@rm -f $(MENG_STATIC)
	@rm -rf $(MENG_OBJ_PATH)


