LINUX_SHARED := $(BUILD_TARGET)/lib/libavs$(JNI_SUFFIX)

LINUX_MKS    := $(OUTER_MKS) mk/linux.mk

LINUX_TARGET := $(BUILD_TARGET)

$(LINUX_OBJS): $(TOOLCHAIN_MASTER) $(AVS_DEPS) $(MENG_DEPS) $(LINUX_DEPS)
$(LINUX_OBJS): $(LINUX_MKS)


-include $(LINUX_OBJS:.o=.d)

$(LINUX_SHARED):  $(LINUX_OBJS) $(AVS_STATIC) $(MENG_STATIC)
	echo "Linux objs=$(AVS_OBJS)"
	echo "MKS=$(OUTER_MKS)"
	@echo "  LD   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	$(LD) $(SH_LFLAGS) $(LFLAGS) $(LINUX_LFLAGS) \
		$(AVS_OBJS) $(AVS_STATIC) $(MENG_STATIC) \
		$(SH_LIBS) $(MENG_LIBS) \
		$(CONTRIB_LIBRE_LIBS) \
		$(CONTRIB_LIBREW_LIBS) \
		$(CONTRIB_WEBRTC_LIBS) \
		-lsodium \
		$(LINUX_LIBS) -o $@

linux_shared: $(LINUX_SHARED)

linux_clean:
	@rm -rf $(LINUX_TARGET)
	@rm -rf $(LINUX_SHARED)
