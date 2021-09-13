#
# Makefile Snippet for make scan
#
#


SCAN_DIR :=$(BUILD_BASE)/scan
SCAN_EXE := $(shell which scan-build)

ifeq ($(SCAN_EXE),)
	LLVM_ROOT := /usr/local/Cellar/llvm
	LLVM_VER := $(shell ls -1 $(LLVM_ROOT) | tail -1)
	SCAN_EXE := $(LLVM_ROOT)/$(LLVM_VER)/bin/scan-build
endif

.PHONY: scan scan_ios scan_all
scan:
	@mkdir -p $(SCAN_DIR)
	@$(MAKE) avs_clean
	@$(MAKE) contrib
	@$(SCAN_EXE) -o $(SCAN_DIR) --status-bugs make $(JOBS) avs

scan_ios:
	@mkdir -p $(SCAN_DIR)
	@$(MAKE) AVS_OS=ios AVS_ARCH=x86_64 avs_clean iosx_clean
	@$(MAKE) AVS_OS=ios AVS_ARCH=x86_64 contrib
	@$(SCAN_EXE) -o $(SCAN_DIR) --status-bugs make $(JOBS) AVS_OS=ios AVS_ARCH=x86_64 avs iosx

scan_all: scan scan_ios

.PHONY: scan_clean

scan_clean:
	@rm -rf $(SCAN_DIR)

