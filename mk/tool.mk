#
# tool.mk
#

TOOLS := $(TOOLS) $(TOOL)


$(TOOL)_C_OBJS := $(patsubst %.c,$(TOOLS_OBJ_PATH)/$(TOOL)/%.o,\
			$(filter %.c,$($(TOOL)_SRCS)))
$(TOOL)_CC_OBJS := $(patsubst %.cpp,$(TOOLS_OBJ_PATH)/$(TOOL)/%.o,\
			$(filter %.cpp,$($(TOOL)_SRCS)))
$(TOOL)_M_OBJS := $(patsubst %.m,$(TOOLS_OBJ_PATH)/$(TOOL)/%.o,\
			$(filter %.m,$($(TOOL)_SRCS)))
$(TOOL)_MM_OBJS := $(patsubst %.mm,$(TOOLS_OBJ_PATH)/$(TOOL)/%.o,\
			$(filter %.mm,$($(TOOL)_SRCS)))
$(TOOL)_OBJS := $($(TOOL)_C_OBJS) $($(TOOL)_CC_OBJS) \
		$($(TOOL)_M_OBJS) $($(TOOL)_MM_OBJS)

#-include $($(TOOL)_OBJS:.o=.d)

$(TOOL)_NAME := $(TOOL)
$(TOOL)_MKS := tools/$(TOOL)/tool.mk mk/tools.mk $(OUTER_MKS)

$($(TOOL)_OBJS): $(TOOLCHAIN_MASTER) $($(TOOL)_DEPS)

ifeq ($(SKIP_MK_DEPS),)
$($(TOOL)_OBJS): $($(TOOL)_MKS)
endif

$(BUILD_BIN)/$(TOOL)$(BIN_SUFFIX): $($(TOOL)_OBJS) $($(TOOL)_LIB_FILES)
	@echo "  LD   $(AVS_PAIR) $@"
	@mkdir -p $(BUILD_BIN)
	$(CXX) $(LFLAGS) -fno-rtti -std=c++14 \
		$($(basename $(notdir $@))_LFLAGS) \
		$^ $($(basename $(notdir $@))_LIBS) $(LIBS) -o $@
