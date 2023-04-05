#
# tools.mk
#


ifeq ($(HAVE_WEBRTC),1)
TOOLS_ALL += sectest
endif

TOOLS_MKS := $(patsubst %,tools/%/tool.mk,$(TOOLS_ALL))
TOOLS_OBJ_PATH := $(BUILD_OBJ)/tools

include $(TOOLS_MKS)


#--- Variables ---

TOOLS_BINS := $(patsubst %,$(BUILD_BIN)/%$(BIN_SUFFIX),$(TOOLS))

TOOLS_SRCS := $(foreach tool,$(TOOLS),\
			$(foreach src,$($(tool)_SRCS),$(tool)/$(src)))
TOOLS_C_OBJS := $(foreach tool,$(TOOLS),$($(tool)_C_OBJS))
TOOLS_CC_OBJS := $(foreach tool,$(TOOLS),$($(tool)_CC_OBJS))
TOOLS_M_OBJS := $(foreach tool,$(TOOLS),$($(tool)_M_OBJS))
TOOLS_MM_OBJS := $(foreach tool,$(TOOLS),$($(tool)_MM_OBJS))
TOOLS_OBJS := $(TOOLS_C_OBJS) $(TOOLS_CC_OBJS) \
		$(TOOLS_M_OBJS) $(TOOLS_MM_OBJS)


#--- Dependency Targets ---

-include $(TOOLS_OBJS:.o=.d)


#--- Building Targets ---

$(TOOLS_C_OBJS): $(TOOLS_OBJ_PATH)/%.o: tools/%.c
	@echo "  CC   $(AVS_PAIR) $<"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CFLAGS) \
		-o $@ -c $< $(DFLAGS)

$(TOOLS_CC_OBJS): $(TOOLS_OBJ_PATH)/%.o: tools/%.cpp
	@echo "  CXX  $(AVS_PAIR) $<"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CXXFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CXXFLAGS) \
		-o $@ -c $< $(DFLAGS)

$(TOOLS_M_OBJS): $(TOOLS_OBJ_PATH)/%.o: tools/%.m
	@echo "  OCC  $(AVS_PAIR) $<"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(OCPPFLAGS) $(CFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_OCPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CFLAGS) \
		-o $@ -c $< $(DFLAGS)

$(TOOLS_MM_OBJS): $(TOOLS_OBJ_PATH)/%.o: tools/%.mm
	@echo "  OXX  $(AVS_PAIR) $<"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(OCPPFLAGS) $(CXXFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_OCPPFLAGS) \
		$($(patsubst %/,%,$(dir $*))_CXXFLAGS) \
		-o $@ -c $< $(DFLAGS)


#--- Phony Targets ---

.PHONY: tools tools_clean
tools: $(TOOLS_BINS)
tools_clean:
	@rm -rf $(TOOLS_OBJ_PATH)
	@rm -f $(TOOLS_BINS)

