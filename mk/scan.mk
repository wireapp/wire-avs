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

.PHONY: scan

scan:
	@mkdir -p $(SCAN_DIR)
	@$(MAKE) avs_clean
	@$(MAKE) contrib
	@$(SCAN_EXE) -o $(SCAN_DIR) make $(JOBS) avs


.PHONY: scan_clean

scan_clean:
	@rm -rf $(SCAN_DIR)

