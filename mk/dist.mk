#
# Makefile Snippet for the Final Distributions
#
#
# This snippet builds the final distributions for the various platforms.
#
# The main target "dist" builds distributions for Android, iOS, and OSX
# if run on an OSX host system or for Linux if run on a Linux host system.
#
# The target "dist_android" builds the following:
#
#    o  an Android AAR build/dist/android/avs.aar with the release code for
#       armv7, arm64 and x86_64 architectures. 
#
#    o  a Zip archive build/dist/android/avs.zip with the NDK shared
#       libraries for Android armv7, arm64 as well as for OSX x86_64, and
#       the class files in non-jar form.
#
# The targets "dist_ios" and "dist_osx" build the following:
#
#    o  an iOS/OSX framework build/dist/{ios,osx}/avs.framework.zip with
#       the release code of the iosx libraries,
#
# The targets "dist_linux" and "dist_osx" build the following:
#
#    o  a tar ball build/dist/{linux,osx}/avscore.tar.bz2 with all the
#       static libraries in avscore/lib, the avs include files in
#       avscore/include/avs and the tool binaries in avscore/bin.
#
# You can limit the architectures built by defining DIST_ARCH and set it
# to a white-space delimited list of the architectures you want.
#
# These targets recursively call make with AVS_OS and AVS_ARCH set as
# needed. So you don't need to pass in any of these when calling any of
# the make dist* targets.
#

JOBS	:= -j8

#--- Configuration ---

ifeq ($(BUILDVERSION),)
	BUILDVERSION := local
endif

# If DIST_ARCH isn't defined, we add all possible architectures. These are
# defined as ALL_AVS_ARCH in mk/target.mk plus for Android we have a fake
# arch "osx" to trigger the OSX build for the zip file.
#
ifeq ($(DIST_ARCH),)
	DIST_ARCH := $(ALL_AVS_ARCH) osx
endif

DIST_ARCH_android := $(filter armv7 arm64 x86_64 osx,$(DIST_ARCH))
DIST_ARCH_ios := $(filter arm64,$(DIST_ARCH))
DIST_ARCH_iossim := $(filter arm64 x86_64,$(DIST_ARCH))
DIST_ARCH_osx := $(filter arm64 x86_64,$(DIST_ARCH))

DIST_FMWK_VERSION := A
DIST_BUNDLE_LIB_NAME := AVS Library
DIST_BUNDLE_LIB_IDENTIFIER := com.wire.avs
DIST_BUNDLE_LIB_INFO := $(DIST_BUNDLE_LIB_NAME)
DIST_BUNDLE_VERSION := $(BUILDVERSION)
DIST_BUNDLE_SIGNATURE := wavs
DIST_BUNDLE_PACKAGE_TYPE := FMWK
DIST_BUNDLE_COPYRIGHT := © 2014 Wire Swiss GmbH

BUILD_DIST_AND := $(BUILD_DIST_BASE)/android
BUILD_DIST_IOS := $(BUILD_DIST_BASE)/ios
BUILD_DIST_IOSSIM := $(BUILD_DIST_BASE)/iossim
BUILD_DIST_OSX := $(BUILD_DIST_BASE)/osx
BUILD_DIST_LINUX := $(BUILD_DIST_BASE)/linux
BUILD_DIST_WASM := $(BUILD_DIST_BASE)/wasm

BUILD_LIB_REL := avs
BUILD_LIB_REL_V := $(BUILD_LIB_REL)/Versions/$(DIST_FMWK_VERSION)
BUILD_BALL_REL := avsball

DIST_BUNDLE_LIB := \
	{CFBundleName="$(DIST_BUNDLE_LIB_NAME)";\
	CFBundleIdentifier="$(DIST_BUNDLE_LIB_IDENTIFIER)";\
	CFBundleVersion="$(DIST_BUNDLE_VERSION)";\
	CFBundleShortVersionString="$(DIST_BUNDLE_VERSION)";\
	CFBundleSignature="$(DIST_BUNDLE_SIGNATURE)";\
	CFBundlePackageType="$(DIST_BUNDLE_PACKAGE_TYPE)";\
	NSHumanReadableCopyright="$(DIST_BUNDLE_COPYRIGHT)";\
	CFBundleGetInfoString="$(DIST_BUNDLE_LIB_INFO)";\
	CFBundleExecutable="avs";\
	CFBundleSignature="\?\?\?\?";\
	DTXcode="0821";\
	DTSDKName="iphoneos12.0";\
	DTSDKBuild="14C89";\
	DTPlatformName="iphoneos";\
	DTPlatformVersion="12.0";\
	DTPlatformBuild="14C89";\
	DTCompiler="com.apple.compilers.llvm.clang.1_0";\
	DTXCodeBuild="8C1002";\
	MinimumOSVersion="15.0";\
	UIDeviceFamily=(1, 2);\
	BuildMachineOSBuild="16D32";\
	CFBundleDevelopmentRegion="en";\
	CFBundleSupportedPlatforms=(iPhoneOS);\
	CFBundleInfoDictionaryVersion="6.0";}


#--- Target Definitions

DIST_AND_TARGETS := \
	$(BUILD_DIST_AND)/avs.aar

DIST_IOS_TARGETS := \
	$(BUILD_DIST_IOS)/$(BUILD_LIB_REL).framework.zip \

DIST_IOSSIM_TARGETS := \
	$(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL).framework.zip \

DIST_OSX_TARGETS := \
	$(BUILD_DIST_OSX)/$(BUILD_LIB_REL).framework.zip \
	$(BUILD_DIST_OSX)/avscore.tar.bz2

DIST_LINUX_TARGETS := \
	$(BUILD_DIST_LINUX)/lib$(BUILD_LIB_REL).so \
	$(BUILD_DIST_LINUX)/avscore.tar.bz2

DIST_WASM_PKG_VERSION := $(subst local,0,$(AVS_VERSION))
DIST_WASM_TARGETS := \
	$(BUILD_DIST_WASM)/avs-web-$(DIST_WASM_PKG_VERSION).tgz


#--- Android ---


DIST_AND_BUILDINFO := <?xml version="1.0" encoding="utf-8"?><resources><string name="avs_version">$(BUILDVERSION)</string></resources>

define build_arch
	mkdir -p $(BUILD_DIST_AND)/aar/jni/$2
	$(MAKE) contrib AVS_OS=android AVS_ARCH=$1 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=$1 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=$1

	@echo BUILD_TARGET=$(BUILD_TARGET)
	@rm -rf android/obj
	@rm -rf android/libs
	@echo "\
APP_ABI=$2\n\
AVS_ARCH_NAME := $1\n\
" > build/android-$1/Arch.mk
	$(TOOLCHAIN_BASE_PATH)/ndk-build -C android -I ../build/android-$1
	cp android/libs/$2/libavs.so \
		$(BUILD_DIST_AND)/aar/jni/$2/libavs.so
	mkdir -p $(BUILD_DIST_AND)/debug/$2/
	cp android/obj/local/$2/libavs.so $(BUILD_DIST_AND)/debug/$2/libavs.so
	cp $(TOOLCHAIN_PATH)/sysroot/usr/lib/$3/libc++_shared.so $(BUILD_DIST_AND)/aar/jni/$2/
endef


.PHONY: $(BUILD_DIST_AND)/avs.aar
$(BUILD_DIST_AND)/avs.aar:
	@mkdir -p $(BUILD_DIST_AND)/aar
	@$(MAKE) android_jar AVS_OS=android AVS_ARCH=armv7
	@cp $(BUILD_BASE)/android-armv7/classes.jar \
		$(BUILD_DIST_AND)/aar

ifneq ($(filter armv7,$(DIST_ARCH)),)
	$(call build_arch,armv7,armeabi-v7a,arm-linux-androideabi)
endif

ifneq ($(filter arm64,$(DIST_ARCH)),)
	$(call build_arch,arm64,arm64-v8a,aarch64-linux-android)
endif

ifneq ($(filter x86_64,$(DIST_ARCH)),)
	$(call build_arch,x86_64,x86_64,x86_64-linux-android)
endif

#ifneq ($(filter osx,$(DIST_ARCH)),)
#	@mkdir -p $(BUILD_DIST_AND)/aar/jni/darwin
#	$(MAKE) contrib AVS_OS=osx AVS_ARCH=arm64 && \
#	$(MAKE) $(JOBS) avs AVS_OS=osx AVS_ARCH=arm64 && \
#	$(MAKE) android_shared AVS_OS=osx AVS_ARCH=arm64
#	@cp $(BUILD_BASE)/osx-arm64/lib/libavs.jnilib \
#		$(BUILD_DIST_AND)/aar/jni/darwin/libavs.dylib
#endif

	@mkdir -p $(BUILD_DIST_AND)/aar/res/values
	@echo '$(DIST_AND_BUILDINFO)' \
		> $(BUILD_DIST_AND)/aar/res/values/buildinfo.xml
	@cp android/AndroidManifest.xml $(BUILD_DIST_AND)/aar
	@( cd $(BUILD_DIST_AND)/aar && zip -r $@ * )

.PHONY: $(BUILD_DIST_AND)/avs.zip
$(BUILD_DIST_AND)/avs.zip:
	@mkdir -p $(BUILD_DIST_AND)/zip
	@$(MAKE) android_jar AVS_OS=android AVS_ARCH=armv7
	@cp -a $(BUILD_BASE)/android-armv7/classes $(BUILD_DIST_AND)/zip
ifneq ($(filter armv7,$(DIST_ARCH)),)
	$(MAKE) contrib AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) tools test AVS_OS=android AVS_ARCH=armv7
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/armeabi-v7a
	@cp $(BUILD_BASE)/android-armv7/lib/libavs.so \
		$(BUILD_DIST_AND)/zip/libs/armeabi-v7a
endif
ifneq ($(filter arm64,$(DIST_ARCH)),)
	$(MAKE) contrib AVS_OS=android AVS_ARCH=arm64 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=arm64 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=arm64 && \
	$(MAKE) tools test AVS_OS=android AVS_ARCH=arm64
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/arm64-v8a
	@cp $(BUILD_BASE)/android-arm64/lib/libavs.so \
		$(BUILD_DIST_AND)/zip/libs/arm64-v8a
endif
ifneq ($(filter x86_64,$(DIST_ARCH)),)
	$(MAKE) contrib AVS_OS=android AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=x86_64 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=x86_64
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/x86_64
	@cp $(BUILD_BASE)/android-x86_64/lib/libavs.so \
		$(BUILD_DIST_AND)/zip/libs/x86_64
endif
#ifneq ($(filter osx,$(DIST_ARCH)),)
#	$(MAKE) contrib AVS_OS=osx AVS_ARCH=arm64 && \
#	$(MAKE) $(JOBS) avs AVS_OS=osx AVS_ARCH=arm64 && \
#	$(MAKE) android_shared AVS_OS=osx AVS_ARCH=arm64
#	@mkdir -p $(BUILD_DIST_AND)/zip/libs/osx
#	@cp $(BUILD_BASE)/osx-arm64/lib/libavs.jnilib \
#		$(BUILD_DIST_AND)/zip/libs/osx
#	@mkdir -p $(BUILD_DIST_AND)/zip/libs/darwin
#	@cp $(BUILD_BASE)/osx-arm64/lib/libavs.jnilib \
#		$(BUILD_DIST_AND)/zip/libs/darwin/libavs.dylib
#
#endif
	@echo 'GEN BUILD INFO > $(BUILD_DIST_AND)/zip/version.buildinfo'
	@echo `genbuildinfo -b BUILDCONTROL -c ../buildcomponents/android`
#	@echo '$(DIST_AND_BUILDINFO)' \
#		> $(BUILD_DIST_AND)/zip/version.buildinfo
	@genbuildinfo -b BUILDCONTROL -c ../build/components/android \
		-o $(BUILD_DIST_AND)/zip/version.buildinfo
	@( cd $(BUILD_DIST_AND)/zip && zip -r $@ * )


#--- iOSX Frameworks ---

# Resources/Info.plist
#
.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Info.plist
.SECONDARY: $(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL)/Info.plist
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/Info.plist
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Info.plist:
	@mkdir -p $(dir $@)
	@rm -f $@
	@defaults write $@ '$(DIST_BUNDLE_LIB)'

# Headers
#
.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Headers
.SECONDARY: $(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL)/Headers
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/Headers
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Headers:
	@mkdir $@
	@touch $@
	@cp -a iosx/include/* $@
	@cp -a include/avs_wcall.h $@

.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Modules
.SECONDARY: $(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL)/Modules
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/Modules
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Modules:
	@mkdir $@
	@touch $@
	@cp -a iosx/module.modulemap $@

# Libraries
#
.PHONY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)
$(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL):
	@for arch in $(DIST_ARCH_ios) ; do \
		$(MAKE) contrib AVS_OS=ios AVS_ARCH=$$arch && \
		$(MAKE) $(JOBS) avs AVS_OS=ios AVS_ARCH=$$arch && \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch && \
		echo "creating dSYM for ios-$$arch" && \
		dsymutil $(BUILD_BASE)/ios-$$arch/lib/avs.framework/avs \
			2>/dev/null && \
		mkdir -p $(BUILD_BASE)/ios-$$arch/lib/avs.framework/Headers && \
		touch $(BUILD_BASE)/ios-$$arch/lib/avs.framework/Headers && \
		cp -a iosx/include/* \
			$(BUILD_BASE)/ios-$$arch/lib/avs.framework/Headers && \
		cp -a include/avs_wcall.h \
			$(BUILD_BASE)/ios-$$arch/lib/avs.framework/Headers && \
		mkdir -p $(BUILD_BASE)/ios-$$arch/lib/avs.framework/Modules && \
		touch $(BUILD_BASE)/ios-$$arch/lib/avs.framework/Modules && \
		cp -a iosx/module.modulemap $(BUILD_BASE)/ios-$$arch/lib/avs.framework/Modules ; \
	done
	@mkdir -p $(dir $@)

	lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/avs.framework/avs)

.PHONY: $(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)
$(BUILD_DIST_IOSSIM)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL):
	@for arch in $(DIST_ARCH_iossim) ; do \
		$(MAKE) contrib AVS_OS=iossim AVS_ARCH=$$arch && \
		$(MAKE) $(JOBS) avs AVS_OS=iossim AVS_ARCH=$$arch && \
		$(MAKE) iosx AVS_OS=iossim AVS_ARCH=$$arch && \
		echo "creating dSYM for iossim-$$arch" && \
		dsymutil $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/avs \
			2>/dev/null && \
		mkdir -p $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Headers && \
		touch $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Headers && \
		cp -a iosx/include/* \
			$(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Headers && \
		cp -a include/avs_wcall.h \
			$(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Headers && \
		mkdir -p $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Modules && \
		touch $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Modules && \
		cp -a iosx/module.modulemap $(BUILD_BASE)/iossim-$$arch/lib/avs.framework/Modules ; \
	done
	@mkdir -p $(dir $@)

	lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_iossim),\
		-arch $(arch) $(BUILD_BASE)/iossim-$(arch)/lib/avs.framework/avs)


dist_test: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)

.PHONY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)
$(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL):
	@for arch in $(DIST_ARCH_osx) ; do \
		$(MAKE) contrib AVS_OS=osx AVS_ARCH=$$arch && \
		$(MAKE) iosx AVS_OS=osx AVS_ARCH=$$arch && \
		mkdir -p $(dir $@) && \
		cp $(BUILD_BASE)/osx-$$arch/lib/avs.framework/avs $@ ; \
	done


# Package
#
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL).framework.zip: \
	$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Info.plist \
	$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Headers \
	$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Modules \
	$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)
	mkdir -p $(BUILD_DIST_BASE)/$*/Carthage/Build/iOS/avs.framework
	cp -R $(BUILD_DIST_BASE)/$*/$(BUILD_LIB_REL)/* \
		$(BUILD_DIST_BASE)/$*/Carthage/Build/iOS/avs.framework
	dsymutil -o $(BUILD_DIST_BASE)/$*/Carthage/Build/iOS/avs.framework.dSYM \
		$(BUILD_DIST_BASE)/$*/$(BUILD_LIB_REL)/$(BUILD_LIB_REL) \
		2>/dev/null

	@( cd $(BUILD_DIST_BASE)/$* && \
		zip --symlinks -r $@ Carthage )


$(BUILD_DIST_IOS)/$(BUILD_LIB_REL).xcframework.zip:
	/Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild -create-xcframework \
	$(foreach os,ios iossim,\
                -framework $(BUILD_DIST_BASE)/$(os)/Carthage/Build/iOS/avs.framework \
		-debug-symbols $(BUILD_DIST_BASE)/$(os)/Carthage/Build/iOS/avs.framework.dSYM) \
		-output $(BUILD_DIST_IOS)/avs.xcframework
	@( cd $(BUILD_DIST_IOS) && \
		zip --symlinks -r avs.xcframework.zip avs.xcframework )

#--- iOSX Tarballs ---

$(BUILD_DIST_IOS)/$(BUILD_BALL_REL)/lib/libavsobjc.a:
	@for arch in $(DIST_ARCH_ios) ; do \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch ; \
	done
	@mkdir -p $(dir $@)
	@lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/libavsobjc.a)

$(BUILD_DIST_OSX)/$(BUILD_BALL_REL)/lib/libavsobjc.a:
	@mkdir -p $(dir $@)
	@cp $(BUILD_BASE)/osx-arm64/lib/libavsobjc.a $@

$(BUILD_DIST)/%/lib/libavsobjc.stripped.a: $(BUILD_DIST)/%/lib/libavsobjc.a
	strip -S -o $@ $^

#--- Linux shared ---
$(BUILD_DIST_LINUX)/lib$(BUILD_LIB_REL).so:
	$(MAKE) toolchain AVS_OS=linux AVS_ARCH=x86_64 && \
	$(MAKE) contrib AVS_OS=linux AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) avs AVS_OS=linux AVS_ARCH=x86_64 && \
	$(MAKE) linux_shared AVS_DEPS=$(AVS_DEPS) AVS_OS=linux AVS_ARCH=x86_64
	@mkdir -p $(dir $@)
	@cp -a $(BUILD_BASE)/linux-$(HOST_ARCH)/lib/lib$(BUILD_LIB_REL).so \
		$(dir $@)


#--- avscore Tarballs ---

$(BUILD_DIST_BASE)/%/avscore.tar.bz2:
	$(MAKE) contrib_librem AVS_OS=$* AVS_ARCH=$(HOST_ARCH) DIST=1
	@mkdir -p $(dir $@)/avscore
	@cp -a $(BUILD_BASE)/$*-$(HOST_ARCH)/lib \
	       $(BUILD_BASE)/$*-$(HOST_ARCH)/share \
	       $(BUILD_BASE)/$*-$(HOST_ARCH)/include \
		$(dir $@)/avscore
	@cp -a include $(dir $@)/avscore/include/avs
	@cp -R src/protobuf/proto $(dir $@)/avscore/include
	@( cd $(dir $@) && tar cfj $@ avscore)


#--- Wasm ---

DIST_WASM_JS_TARGET := $(BUILD_DIST_WASM)/src/avs_core.js
DIST_WASM_PC_TARGET := $(BUILD_DIST_WASM)/src/avs_pc.js
DIST_WASM_WC_TARGET := $(BUILD_DIST_WASM)/src/avs_wcall.ts
DIST_WASM_PKG_TARGET := $(BUILD_DIST_WASM)/package.json

ifeq ($(WASM_PATH),)
EMCC	:= emcc
else
EMCC 	:= $(WASM_PATH)/emcc
endif

ifeq ($(WASM_DEBUG),1)
DIST_WASM_WITH_SOURCE_MAP := -gsource-map -g
else
DIST_WASM_WITH_SOURCE_MAP :=
endif

$(DIST_WASM_JS_TARGET):
	$(MAKE) AVS_OS=wasm AVS_ARCH=generic avs
	@mkdir -p $(BUILD_DIST_WASM)/src
	$(EMCC) $(DIST_WASM_WITH_SOURCE_MAP) -o $(DIST_WASM_JS_TARGET) \
		$(BUILD_BASE)/wasm-generic/lib/libavscore.a \
		$(BUILD_BASE)/wasm-generic/lib/libre.a \
		$(BUILD_BASE)/wasm-generic/lib/libsodium.a \
		$(BUILD_BASE)/wasm-generic/lib/libssl.a \
		$(BUILD_BASE)/wasm-generic/lib/libcrypto.a \
		-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "addFunction", "UTF8ToString", "lengthBytesUTF8", "stringToUTF8"]' \
		-s EXPORTED_FUNCTIONS='["_wcall_init", "_malloc", "_free"]' \
                -s TOTAL_MEMORY=134217728 \
		-s RESERVED_FUNCTION_POINTERS=128 \
		-s SINGLE_FILE=1 \
		-s MODULARIZE=1 \
		-s EXPORT_NAME='avs_core'

$(DIST_WASM_PC_TARGET):
	@mkdir -p $(BUILD_DIST_WASM)
	@cp -r wasm/. $(BUILD_DIST_WASM)

.PHONY: $(DIST_WASM_PKG_TARGET)
$(DIST_WASM_PKG_TARGET):
	(rm -f $@ && jq '.version = "$(DIST_WASM_PKG_VERSION)"' > $@) < $@

$(DIST_WASM_WC_TARGET):
	@mkdir -p $(BUILD_DIST_WASM)
	@./wasm/wcall2ts.py include/avs_wcall.h wasm/avs_wcall.template $@

$(DIST_WASM_TARGETS): $(DIST_WASM_JS_TARGET) $(DIST_WASM_PC_TARGET) $(DIST_WASM_WC_TARGET) \
	$(DIST_WASM_PKG_TARGET)
	@( cd $(dir $@) && npm ci && npm pack)


#--- Phony Targets ---

.PHONY: dist_android dist_ios dist_osx dist_linux dist_wasm dist dist_host dist_clean
dist_android: $(DIST_AND_TARGETS)
dist_ios: $(DIST_IOS_TARGETS) $(DIST_IOSSIM_TARGETS) $(BUILD_DIST_IOS)/$(BUILD_LIB_REL).xcframework.zip
dist_osx: $(DIST_OSX_TARGETS)
dist_linux: $(DIST_LINUX_TARGETS)
dist_wasm: $(DIST_WASM_TARGETS)
dist_clean:
	@rm -rf $(BUILD_DIST_BASE)

ifeq ($(HOST_OS),linux)
dist: dist_linux
dist_host: dist_linux
else
dist: dist_android dist_ios dist_osx dist_wasm
dist_host: dist_osx
endif
