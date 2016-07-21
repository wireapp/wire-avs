#
# Makefile snippet for building the test suite
#

TEST_MK := test/srcs.mk
TEST_BIN := ztest

include $(TEST_MK)

TEST_MKS := $(OUTER_MKS) mk/test.mk $(TEST_MK)
TEST_OBJ_PATH := $(BUILD_OBJ)/test

TEST_C_OBJS := $(patsubst %.c,$(TEST_OBJ_PATH)/%.o,\
			$(filter %.c,$(TEST_SRCS)))
TEST_CC_OBJS := $(patsubst %.cpp,$(TEST_OBJ_PATH)/%.o,\
			$(filter %.cpp,$(TEST_SRCS)))
TEST_OBJS := $(TEST_C_OBJS) $(TEST_CC_OBJS)

TEST_DEPS += $(CONTRIB_GTEST_TARGET) $(AVS_DEPS) $(MENG_DEPS)
TEST_LIBS += $(CONTRIB_GTEST_LIBS) $(AVS_LIBS) $(MENG_LIBS)

ifneq ($(HAVE_PROTOBUF),)
TEST_DEPS += $(CONTRIB_PROTOBUF_TARGET)
TEST_LIBS += $(CONTRIB_PROTOBUF_LIBS)
endif

ifneq ($(HAVE_CRYPTOBOX),)
TEST_DEPS += $(CONTRIB_CRYPTOBOX_TARGET)
TEST_LIBS += $(CONTRIB_CRYPTOBOX_LIBS)
endif

ifeq ($(AVS_OS),android)
    LFLAGS += -fPIE -pie
endif

-include $(TEST_OBJS:.o=.d)

$(TEST_OBJS): $(TOOLCHAIN_MASTER) $(TEST_DEPS)

ifeq ($(SKIP_MK_DEPS),)
$(TEST_OBJS): $(TEST_MKS)
endif

$(TEST_C_OBJS): $(TEST_OBJ_PATH)/%.o: test/%.c
	@echo "  CC   $(AVS_OS)-$(AVS_ARCH) test/$*.c"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CFLAGS) \
		$(TEST_CPPFLAGS) $(TEST_CFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(TEST_CC_OBJS): $(TEST_OBJ_PATH)/%.o: test/%.cpp
	@echo "  CXX  $(AVS_OS)-$(AVS_ARCH) test/$*.cpp"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CXXFLAGS) \
		$(TEST_CPPFLAGS) $(TEST_CXXFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(BUILD_BIN)/$(TEST_BIN)$(BIN_SUFFIX): $(TEST_OBJS) $(AVS_STATIC) $(MENG_STATIC)
	@echo "  LD      $@"
	@mkdir -p $(BUILD_BIN)
	@$(CXX) $(LFLAGS) $(TEST_LFLAGS) \
		$^ $(TEST_LIBS) $(LIBS) -o $@


#--- Phony Targets ---

.PHONY: test test_clean
test: $(BUILD_BIN)/$(TEST_BIN)$(BIN_SUFFIX)
test_clean:
	@rm -f $(BUILD_BIN)/$(TEST_BIN)$(BIN_SUFFIX)

