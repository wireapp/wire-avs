
TOOLCHAIN_GCC_VERSION  := 4.9
TOOLCHAIN_LLVM_VERSION := 3.4
#TOOLCHAIN_PLATFORM     := android-16
TOOLCHAIN_API          := 16
TOOLCHAIN_STL          := libc++

TOOLCHAIN_ARCH_armv7   := arm
TOOLCHAIN_ARCH_arm64   := arm64
TOOLCHAIN_ARCH_i386    := x86
TOOLCHAIN_ARCH_x86_64  := x86_64
TOOLCHAIN_ARCH         := $(TOOLCHAIN_ARCH_$(AVS_ARCH))

TOOLCHAIN_TOOLCHAIN_armv7  := arm-linux-androideabi-$(TOOLCHAIN_GCC_VERSION)
TOOLCHAIN_TOOLCHAIN_arm64  := aarch64-linux-android-$(TOOLCHAIN_GCC_VERSION)
TOOLCHAIN_TOOLCHAIN_i386   := x86-$(TOOLCHAIN_GCC_VERSION)
TOOLCHAIN_TOOLCHAIN_x86_64 := x86_64-$(TOOLCHAIN_GCC_VERSION)
TOOLCHAIN_TOOLCHAIN        := $(TOOLCHAIN_TOOLCHAIN_$(AVS_ARCH))

TC_ABI_armv7  := armeabi-v7a
TC_ABI_arm64  := arm64-v8a
TC_ABI_i386   := x86
TC_ABI_x86_64 := x86_64
TC_ABI        := $(TC_ABI_$(AVS_ARCH))

ifeq ($(SKIP_MK_DEPS),)
TC_MKS := $(OUTER_MKS) mk/toolchain.mk
endif


#--- libcpufeatures ---

TC_STATIC := $(BUILD_TARGET)/lib/libcpufeatures.a
TC_SRC_PATH := $(ANDROID_NDK_ROOT)/sources/android/cpufeatures
TC_OBJ_PATH := $(BUILD_OBJ)/cpufeatures

TC_SRCS := cpu-features.c

TC_C_OBJS := $(patsubst %.c,$(TC_OBJ_PATH)/%.o,\
			$(filter %.c,$(TC_SRCS)))

TC_OBJS := $(TC_C_OBJS)

$(TC_C_OBJS): $(TC_OBJ_PATH)/%.o: $(TC_SRC_PATH)/%.c $(TC_MKS)
	@echo "  CC   $(AVS_OS)-$(AVS_ARCH) cpufeatures/$*.c"
	@mkdir -p $(dir $@)
	@$(CC)  $(CPPFLAGS) $(CFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(TC_STATIC): $(TC_OBJS)
	@echo "  AR   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@$(AR) $(AFLAGS) $@ $(filter %.o,$^)
ifneq ($(RANLIB),)
	@$(RANLIB) $@
endif


#--- Actual Toolchain ---


$(TOOLCHAIN_BASE_PATH)/android-%/stat: $(ANDROID_NDK_ROOT) $(TC_MKS)
	@if [ x$$ANDROID_NDK_ROOT = x ] ; then \
		echo ANDROID_NDK_ROOT not set ; \exit 1 ; \
	fi
	@rm -rf $(TOOLCHAIN_PATH)
	$(ANDROID_NDK_ROOT)/build/tools/make_standalone_toolchain.py \
		--stl=$(TOOLCHAIN_STL) \
		--arch=$(TOOLCHAIN_ARCH) \
		--api $(TOOLCHAIN_API) \
		--install-dir=$(TOOLCHAIN_PATH)
	@mkdir $(TOOLCHAIN_PATH)/sysroot/usr/include/c++
	@ln -s ../../../../include/c++/$(TOOLCHAIN_GCC_VERSION).x \
		$(TOOLCHAIN_PATH)/sysroot/usr/include/c++/v1
	@ln -s  $(ANDROID_NDK_ROOT) $(TOOLCHAIN_PATH)/ndk
	@ln -s $(AR) $(TOOLCHAIN_PATH)/bin/ar
	@( cd $(ANDROID_NDK_ROOT)/sources/cxx-stl/llvm-libc++/libs/$(TC_ABI) ; \
		for i in `find . -name "*.a"` ; do \
			mkdir -p $(TOOLCHAIN_PATH)/libc++/`dirname $$i` ; \
			cp $$i $(TOOLCHAIN_PATH)/libc++/`dirname $$i` ; \
		done )
	@touch $@

$(TOOLCHAIN_BASE_PATH)/%/stat:
	@echo "Setting up dummy toolchain for $(AVS_OS)-$(AVS_ARCH)."
	@mkdir -p $@
	@touch $@


#--- Phony Targets ---

TOOLCHAIN_MASTER := $(TOOLCHAIN_PATH)/stat

ifeq ($(AVS_OS),android)
TOOLCHAIN_MASTER += $(TC_STATIC)
endif

.PHONY: toolchain toolchain_clean
toolchain: $(TOOLCHAIN_MASTER)
toolchain_clean:
	@rm -rf $(TOOLCHAIN_PATH)
	@rm -rf $(TC_STATIC)



