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
#       armv7 and i386 architectures. 64 bit architectures will follow as
#       soon as certain build troubles are sorted out.
#    
#    o  a Zip archive build/dist/android/avs.zip with the NDK shared
#       libraries for Android armv7 and i386 as well as for OSX x86_64, and
#       the class files in non-jar form.
#
# The targets "dist_ios" and "dist_osx" build the following:
#
#    o  an iOS/OSX framework build/dist/{ios,osx}/avs.framework.zip with
#       the release code of the iosx libraries,
#
#    o  an iOS/OSX framework build/dist/{ios,osx}/avsstub.framework.zip
#       with the stub code of the iosx libraries,
#
#    o  a tar ball build/dist/{ios,osx}/avs.tar.bz2 with both the release
#       and stub libraries.
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
	DIST_ARCH := $(ALL_AVS_ARCH)
endif

DIST_ARCH_android := $(filter armv7 i386 osx,$(DIST_ARCH))
DIST_ARCH_ios := $(filter armv7 arm64 i386 x86_64,$(DIST_ARCH))

DIST_FMWK_VERSION := A
DIST_BUNDLE_LIB_NAME := AVS Library
DIST_BUNDLE_LIB_IDENTIFIER := com.wire.avs
DIST_BUNDLE_LIB_INFO := $(DIST_BUNDLE_LIB_NAME)
DIST_BUNDLE_STUB_NAME := AVS Library Stub
DIST_BUNDLE_STUB_IDENTIFIER := com.wire.avsstub
DIST_BUNDLE_STUB_INFO := $(DIST_BUNDLE_STUB_NAME)
DIST_BUNDLE_VERSION := $(BUILDVERSION)
DIST_BUNDLE_SIGNATURE := wavs
DIST_BUNDLE_PACKAGE_TYPE := FMWK
DIST_BUNDLE_COPYRIGHT := © 2014 Wire Swiss GmbH

BUILD_DIST_AND := $(BUILD_DIST_BASE)/android
BUILD_DIST_IOS := $(BUILD_DIST_BASE)/ios
BUILD_DIST_OSX := $(BUILD_DIST_BASE)/osx
BUILD_DIST_LINUX := $(BUILD_DIST_BASE)/linux

BUILD_LIB_REL := avs
BUILD_LIB_REL_V := $(BUILD_LIB_REL)/Versions/$(DIST_FMWK_VERSION)
BUILD_STUB_REL := avsstub
BUILD_STUB_REL_V := $(BUILD_STUB_REL)/Versions/$(DIST_FMWK_VERSION)
BUILD_BALL_REL := avsball
BUILD_STUBBALL_REL := avsstubball

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
	DTSDKName="iphoneos10.2";\
	DTSDKBuild="14C89";\
	DTPlatformName="iphoneos";\
	DTPlatformVersion="10.2";\
	DTPlatformBuild="14C89";\
	DTCompiler="com.apple.compilers.llvm.clang.1_0";\
	DTXCodeBuild="8C1002";\
	MinimumOSVersion="8.0";\
	UIDeviceFamily=(1, 2);\
	BuildMachineOSBuild="16D32";\
	CFBundleDevelopmentRegion="en";\
	CFBundleSupportedPlatforms=(iPhoneOS);\
	CFBundleInfoDictionaryVersion="6.0";}


DIST_BUNDLE_STUB := \
	{CFBundleName="$(DIST_BUNDLE_STUB_NAME)";\
	CFBundleIdentifier="$(DIST_BUNDLE_STUB_IDENTIFIER)";\
	CFBundleVersion="$(DIST_BUNDLE_VERSION)";\
	CFBundleSignature="$(DIST_BUNDLE_SIGNATURE)";\
	CFBundlePackageType="$(DIST_BUNDLE_PACKAGE_TYPE)";\
	NSHumanReadableCopyright="$(DIST_BUNDLE_COPYRIGHT)";\
	CFBundleGetInfoString="$(DIST_BUNDLE_STUB_INFO)";}


#--- Target Definitions

DIST_AND_TARGETS := $(BUILD_DIST_AND)/avs.aar \
		    $(BUILD_DIST_AND)/avs.zip \
		    $(BUILD_DIST_AND)/avs.tar.bz2

DIST_IOS_TARGETS := \
	$(BUILD_DIST_IOS)/$(BUILD_LIB_REL).framework.zip \
	$(BUILD_DIST_IOS)/$(BUILD_STUB_REL).framework.zip \
	$(BUILD_DIST_IOS)/avs.tar.bz2 \
	$(BUILD_DIST_IOS)/avsstub.tar.bz2



DIST_OSX_TARGETS := \
	$(BUILD_DIST_OSX)/$(BUILD_LIB_REL).framework.zip \
	$(BUILD_DIST_OSX)/$(BUILD_STUB_REL).framework.zip \
	$(BUILD_DIST_OSX)/avs.tar.bz2 \
	$(BUILD_DIST_OSX)/avsstub.tar.bz2 \
	$(BUILD_DIST_OSX)/avscore.tar.bz2

DIST_LINUX_TARGETS := \
	$(BUILD_DIST_LINUX)/avscore.tar.bz2


#--- Android ---


DIST_AND_BUILDINFO := <?xml version="1.0" encoding="utf-8"?><resources><string name="avs_version">$(BUILDVERSION)</string></resources>

.PHONY: $(BUILD_DIST_AND)/avs.aar
$(BUILD_DIST_AND)/avs.aar:
	@mkdir -p $(BUILD_DIST_AND)/aar
	@$(MAKE) android_jar AVS_OS=android AVS_ARCH=armv7
	@cp $(BUILD_BASE)/android-armv7/classes.jar \
		$(BUILD_DIST_AND)/aar
ifneq ($(filter armv7,$(DIST_ARCH)),)
	@mkdir -p $(BUILD_DIST_AND)/aar/jni/armeabi-v7a
	@$(MAKE) toolchain AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) contrib AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) tools test AVS_OS=android AVS_ARCH=armv7
	@cp $(BUILD_BASE)/android-armv7/lib/libavs.stripped.so \
		$(BUILD_DIST_AND)/aar/jni/armeabi-v7a/libavs.so
endif
ifneq ($(filter i386,$(DIST_ARCH)),)
	@mkdir -p $(BUILD_DIST_AND)/aar/jni/x86
	@$(MAKE) toolchain AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) contrib AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=i386
	@cp $(BUILD_BASE)/android-i386/lib/libavs.stripped.so \
		$(BUILD_DIST_AND)/aar/jni/x86/libavs.so
endif
ifneq ($(filter osx,$(DIST_ARCH)),)
	@mkdir -p $(BUILD_DIST_AND)/aar/jni/darwin
	@$(MAKE) toolchain AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) contrib AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) avs AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) android_shared AVS_OS=osx AVS_ARCH=x86_64
	@cp $(BUILD_BASE)/osx-x86_64/lib/libavs.jnilib \
		$(BUILD_DIST_AND)/aar/jni/darwin/libavs.dylib
endif

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
	@$(MAKE) toolchain AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) contrib AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=armv7 && \
	$(MAKE) tools test AVS_OS=android AVS_ARCH=armv7
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/armeabi-v7a
	@cp $(BUILD_BASE)/android-armv7/lib/libavs.so \
		$(BUILD_DIST_AND)/zip/libs/armeabi-v7a
endif
ifneq ($(filter i386,$(DIST_ARCH)),)
	@$(MAKE) toolchain AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) contrib AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) $(JOBS) avs AVS_OS=android AVS_ARCH=i386 && \
	$(MAKE) android_shared AVS_OS=android AVS_ARCH=i386
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/x86
	@cp $(BUILD_BASE)/android-i386/lib/libavs.so \
		$(BUILD_DIST_AND)/zip/libs/x86
endif
ifneq ($(filter osx,$(DIST_ARCH)),)
	@$(MAKE) toolchain AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) contrib AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) avs AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) android_shared AVS_OS=osx AVS_ARCH=x86_64
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/osx
	@cp $(BUILD_BASE)/osx-x86_64/lib/libavs.jnilib \
		$(BUILD_DIST_AND)/zip/libs/osx
	@mkdir -p $(BUILD_DIST_AND)/zip/libs/darwin
	@cp $(BUILD_BASE)/osx-x86_64/lib/libavs.jnilib \
		$(BUILD_DIST_AND)/zip/libs/darwin/libavs.dylib

endif
#	@echo 'GEN BUILD INFO > $(BUILD_DIST_AND)/zip/version.buildinfo'
#	@echo `genbuildinfo -b BUILDCONTROL -c ../buildcomponents/android`
#	@echo '$(DIST_AND_BUILDINFO)' \
#		> $(BUILD_DIST_AND)/zip/version.buildinfo
#	@genbuildinfo -b BUILDCONTROL -c ../build/components/android \
#		-o $(BUILD_DIST_AND)/zip/version.buildinfo
	@( cd $(BUILD_DIST_AND)/zip && zip -r $@ * )

$(BUILD_DIST_AND)/avs.tar.bz2: $(BUILD_DIST_AND)/avs.zip
	@( cd $(BUILD_DIST_AND)/zip && tar cfj $@ * )


#--- iOSX Frameworks ---

# Resources/Info.plist
#
.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Info.plist
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/Info.plist
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Info.plist:
	@mkdir -p $(dir $@)
	@rm -f $@
	@defaults write $@ '$(DIST_BUNDLE_LIB)'

.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_STUB_REL_V)/Resources/Info.plist
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_STUB_REL_V)/Resources/Info.plist
$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL_V)/Resources/Info.plist:
	@mkdir -p $(dir $@)
	@rm -f $@
	@defaults write $@ '$(DIST_BUNDLE_STUB)'

# Headers
#
.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Headers
.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_STUB_REL_V)/Headers
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/Headers
.SECONDARY: $(BUILD_DIST_OSX)/$(BUILD_STUB_REL_V)/Headers
$(BUILD_DIST_BASE)/%/$(BUILD_LIB_REL)/Headers:
	@mkdir $@
	@touch $@
	@cp -a iosx/include/* $@
	@cp -a include/avs_wcall.h $@

$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL_V)/Headers:
	@mkdir $@
	@touch $@
	@cp -a iosx/include/* $@
	@cp -a include/avs_wcall.h $@

.SECONDARY: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/Modules
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
		$(MAKE) $(JOBS) mediaengine AVS_OS=ios AVS_ARCH=$$arch && \
		$(MAKE) $(JOBS) avs AVS_OS=ios AVS_ARCH=$$arch && \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch ; \
	done
	@mkdir -p $(dir $@)
	lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/avs.framework/avs)

dist_test: $(BUILD_DIST_IOS)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)

.PHONY: $(BUILD_DIST_IOS)/$(BUILD_STUB_REL_V)/$(BUILD_STUB_REL)
$(BUILD_DIST_IOS)/$(BUILD_STUB_REL_V)/$(BUILD_STUB_REL):
	@for arch in $(DIST_ARCH_ios) ; do \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch ; \
	done
	@mkdir -p $(dir $@)
	@lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/libavsobjcstub.dylib)

.PHONY: $(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL)
$(BUILD_DIST_OSX)/$(BUILD_LIB_REL)/$(BUILD_LIB_REL):
	@$(MAKE) contrib AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) mediaengine AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) $(JOBS) avs AVS_OS=osx AVS_ARCH=x86_64 && \
	$(MAKE) iosx AVS_OS=osx AVS_ARCH=x86_64
	@mkdir -p $(dir $@)
	@cp $(BUILD_BASE)/osx-x86_64/lib/avs.framework/avs $@

.PHONY: $(BUILD_DIST_OSX)/$(BUILD_STUB_REL_V)/$(BUILD_STUB_REL)
$(BUILD_DIST_OSX)/$(BUILD_STUB_REL_V)/$(BUILD_STUB_REL):
	@$(MAKE) iosx AVS_OS=osx AVS_ARCH=x86_64
	@mkdir -p $(dir $@)
	@cp $(BUILD_BASE)/osx-x86_64/lib/avs.framework/avs $@


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
	@( cd $(BUILD_DIST_BASE)/$* && \
		zip --symlinks -r $@ Carthage )

$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL).framework.zip: \
		$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL_V)/Resources/Info.plist \
		$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL_V)/Headers \
		$(BUILD_DIST_BASE)/%/$(BUILD_STUB_REL_V)/$(BUILD_STUB_REL)
	@rm -f  $(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Versions/Current \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Headers \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/$(BUILD_STUB_REL) \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Resources
	@ln -s $(DIST_FMWK_VERSION) \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Versions/Current
	@ln -s Versions/Current/Headers \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Headers
	@ln -s Versions/Current/$(BUILD_STUB_REL) \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/$(BUILD_STUB_REL)
	@ln -s Versions/Current/Resources \
		$(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL)/Resources
	@( cd $(BUILD_DIST_BASE)/$*/$(BUILD_STUB_REL) && \
		zip --symlinks -r $@ * )


#--- iOSX Tarballs ---

$(BUILD_DIST_IOS)/$(BUILD_BALL_REL)/lib/libavsobjc.a:
	@for arch in $(DIST_ARCH_ios) ; do \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch ; \
	done
	@mkdir -p $(dir $@)
	@lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/libavsobjc.a)

$(BUILD_DIST_IOS)/$(BUILD_STUBBALL_REL)/lib/libavsobjcstub.a:
	@for arch in $(DIST_ARCH_ios) ; do \
		$(MAKE) iosx AVS_OS=ios AVS_ARCH=$$arch ; \
	done
	@mkdir -p $(dir $@)
	@lipo -create -output $@ \
		$(foreach arch,$(DIST_ARCH_ios),\
		-arch $(arch) $(BUILD_BASE)/ios-$(arch)/lib/libavsobjcstub.a)

$(BUILD_DIST_OSX)/$(BUILD_BALL_REL)/lib/libavsobjc.a:
	@mkdir -p $(dir $@)
	@cp $(BUILD_BASE)/osx-x86_64/lib/libavsobjc.a $@

$(BUILD_DIST_OSX)/$(BUILD_STUBBALL_REL)/lib/libavsobjcstub.a:
	@mkdir -p $(dir $@)
	@cp $(BUILD_BASE)/osx-x86_64/lib/libavsobjcstub.a $@

$(BUILD_DIST)/%/lib/libavsobjc.stripped.a: $(BUILD_DIST)/%/lib/libavsobjc.a
	strip -S -o $@ $^

$(BUILD_DIST_BASE)/%/avs.tar.bz2: \
		$(BUILD_DIST_BASE)/%/$(BUILD_BALL_REL)/lib/libavsobjc.a \
		$(BUILD_DIST_BASE)/%/$(BUILD_BALL_REL)/lib/libavsobjc.stripped.a
	@mkdir -p $(dir $@)
	@cp -a iosx/include $(BUILD_DIST_BASE)/$*/$(BUILD_BALL_REL)
	@cp -a include/avs_wcall.h $(BUILD_DIST_BASE)/$*/$(BUILD_BALL_REL)
	@genbuildinfo -b BUILDCONTROL \
		-o $(BUILD_DIST_BASE)/$*/$(BUILD_BALL_REL)/version.buildinfo
	@( cd $(BUILD_DIST_BASE)/$*/$(BUILD_BALL_REL) && tar cfj $@ * )

$(BUILD_DIST_BASE)/%/avsstub.tar.bz2: \
		$(BUILD_DIST_BASE)/%/$(BUILD_STUBBALL_REL)/lib/libavsobjcstub.a
	@mkdir -p $(dir $@)
	@cp -a iosx/include $(BUILD_DIST_BASE)/$*/$(BUILD_STUBBALL_REL)
	@cp -a include/avs_wcall.h $(BUILD_DIST_BASE)/$*/$(BUILD_STUBBALL_REL)
	@genbuildinfo -b BUILDCONTROL \
		-o $(BUILD_DIST_BASE)/$*/$(BUILD_STUBBALL_REL)/version.buildinfo
	@( cd $(BUILD_DIST_BASE)/$*/$(BUILD_STUBBALL_REL) && tar cfj $@ * )


#--- avscore Tarballs ---

$(BUILD_DIST_BASE)/%/avscore.tar.bz2:
	$(MAKE) contrib_librem AVS_OS=$* AVS_ARCH=x86_64 DIST=1
	@mkdir -p $(dir $@)/avscore
	@cp -a $(BUILD_BASE)/$*-x86_64/bin \
	       $(BUILD_BASE)/$*-x86_64/lib \
	       $(BUILD_BASE)/$*-x86_64/share \
	       $(BUILD_BASE)/$*-x86_64/include \
		$(dir $@)/avscore
	@cp -a include $(dir $@)/avscore/include/avs
	@( cd $(dir $@) && tar cfj $@ avscore)


#--- Phony Targets ---

.PHONY: dist_android dist_ios dist_osx dist_linux dist dist_host dist_clean
dist_android: android_symlinks $(DIST_AND_TARGETS)
dist_ios: $(DIST_IOS_TARGETS)
dist_osx: $(DIST_OSX_TARGETS)
dist_linux: $(DIST_LINUX_TARGETS)
dist_clean:
	@rm -rf $(BUILD_DIST_BASE)

ifeq ($(HOST_OS),linux)
dist: dist_linux
dist_host: dist_linux
else
dist: dist_android dist_ios dist_osx
dist_host: dist_osx
endif
