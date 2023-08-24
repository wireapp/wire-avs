#
# Makefile Snippet for Android Wrappers
#
# 
# This snippet builds the Java components that live in Android and are
# mainly targeted towards that platform. Note, however, that these are
# regularly built for OSX as part of the Android distribution for
# development of the Android Sync Engine.
#
# The results of this snippet are:
#
#    o  the compiled Java classes in build/$(AVS_PAIR)/classes,
#    
#    o  the same zipped up as build/$(AVS_PAIR)/classes.jar,
#
#    o  the shared library with all AVS NDK code as
#       build/$(AVS_PAIR)/libavs.{so,jnilib}.
#
# The snippet's main target "android" builds all of them. There are separate
# targets "android_jar" for the Java class stuff and "android_shared" for
# only the NDK shared library.
#
# The snippet requires the java and jni sources to be listed in
# android/java/srcs.mk and android/jni/srcs.mk. Only source files listed
# there explicitely will be built.
#
# The snippet defines the usual argument variables using the prefix "AND_".
# See mk/target.mk for information on these.
#

#--- Variable Definitions ---

AND_JNI_MK := android/jni/srcs.mk
AND_MKS := $(OUTER_MKS) mk/android.mk

include $(AND_JNI_MK)

AND_BOOTCLASSPATH := $(ANDROID_SDK_ROOT)/platforms/$(ANDROID_SDK_VER)/android.jar:$(ANDROID_SDK_ROOT)/tools/support/annotations.jar
AND_CLASSPATH := android/java

AND_CLS_TARGET := $(BUILD_TARGET)/classes
AND_OBJ_TARGET := $(BUILD_TARGET)/obj/jni

AND_SHARED := $(BUILD_TARGET)/lib/libavs$(JNI_SUFFIX)
AND_SHARED_STRIPPED := $(BUILD_TARGET)/lib/libavs.stripped$(JNI_SUFFIX)
AND_JAR := $(BUILD_TARGET)/classes.jar
AND_JAVADOC := $(BUILD_DIST_BASE)/android/javadoc.jar
AND_JAVADOC_PATH := $(BUILD_DIST_BASE)/android/javadoc

ifeq ($(AVS_OS),android)
ifneq ($(AVS_PROJECT),avsopen)
#AND_DEPS := $(CONTRIB_BREAKPAD_TARGET)
#AND_LIBS := $(CONTRIB_BREAKPAD_LIBS)
#AND_CPPFLAGS += -DUSE_BREAKPAD=1
endif
endif

ADB       := $(ANDROID_SDK_ROOT)/platform-tools/adb


#--- Dependency Targets ---

$(AND_OBJS): $(TOOLCHAIN_MASTER) $(AVS_DEPS) $(MENG_DEPS) $(AND_DEPS)

ifeq ($(SKIP_MK_DEPS),)
#$(AND_CLSS): $(AND_MKS)
$(AND_OBJS): $(AND_MKS)
endif

-include $(AND_OBJS:.o=.d)

#--- Building Targets ---

#$(AND_CLSS): $(AND_CLS_TARGET)/%.class: $(AND_CLASSPATH)/%.java
#	@echo "  JC   $(AVS_OS)-$(AVS_ARCH) $(AND_CLASSPATH)/$*.java"
#	@mkdir -p $(AND_CLS_TARGET)
#	@$(JAVAC) -Xlint:deprecation -d $(AND_CLS_TARGET) \
#		-classpath $(AND_BOOTCLASSPATH):$(AND_CLASSPATH) \
#		$<

.PHONY: android_classes
android_classes:
	@echo "  GRADLEW"
	@cd android/ && \
	rm -rf lib/build && \
	ANDROID_HOME=$(ANDROID_SDK_ROOT) ./gradlew compileDebugJavaWithJavac


$(AND_JAR): android_classes
	@mkdir -p $(AND_CLS_TARGET)
	@echo "  UNZ webrtc jars"
	@unzip -o contrib/webrtc/$(WEBRTC_VER)/java/base.jar -d $(AND_CLS_TARGET)
	@unzip -o contrib/webrtc/$(WEBRTC_VER)/java/audiodev.jar -d $(AND_CLS_TARGET)
	@rm -rf $(AND_CLS_TARGET)/META-INF
	cp -R android/lib/build/intermediates/javac/debug/classes/* $(AND_CLS_TARGET)
	@echo "  ZIP  $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@( cd $(AND_CLS_TARGET) && zip -r $@ * )

$(AND_CC_OBJS): $(AND_OBJ_TARGET)/%.o: android/jni/%.cc
	@echo "  CXX  $(AVS_OS)-$(AVS_ARCH) android/jni/$*.cc"
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(JNICPPFLAGS) \
		$(AVS_CPPFLAGS) $(AVS_CXXFLAGS) \
		$(AND_CPPFLAGS) $(AND_CXXFLAGS) \
		-c $< -o $@ $(DFLAGS)

$(AND_SHARED):  $(AND_OBJS) $(AVS_STATIC) $(MENG_STATIC)
	@echo "  LD   $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@$(LD) $(SH_LFLAGS) $(LFLAGS) $(AND_LFLAGS) \
		$(AND_OBJS) $(AVS_STATIC) $(MENG_STATIC) \
		$(SH_LIBS) $(LIBS) $(AVS_LIBS) $(MENG_LIBS) $(AND_LIBS) -o $@
#       @$(STRIP) --strip-unneeded $@

$(AND_SHARED_STRIPPED): $(AND_SHARED)
	@echo "  STR  $(AVS_OS)-$(AVS_ARCH) $@"
	@mkdir -p $(dir $@)
	@cp $< $@
	# @$(STRIP) --strip-unneeded $@


#--- Phony Targets ---

.PHONY: android android_jar android_shared android_clean
android: $(AND_JAR) $(AND_SHARED) $(AND_SHARED_STRIPPED)
android: $(AND_JAR) $(AND_SHARED) $(AND_SHARED_STRIPPED)
android_jar: $(AND_JAR)
android_shared: $(AND_SHARED) $(AND_SHARED_STRIPPED)
android_clean:
	@rm -rf $(AND_CLS_TARGET)
	@rm -f $(AND_JAR)
	@rm -f $(AND_SHARED)
	@rm -f $(AND_SHARED_STRIPPED)

.PHONY: android_dist_javadoc
android_dist_javadoc: $(AND_JAVADOC)

.PHONY: android_javadoc
android_javadoc:
	$(JAVADOC) -Xdoclint:none -public -d $(AND_JAVADOC_PATH) \
		-classpath $(AND_BOOTCLASSPATH):$(AND_CLASSPATH) \
		-sourcepath android/java/ com.waz.audioeffect com.waz.avs com.waz.call com.waz.log com.waz.media.manager

.PHONY: android_dist_sources
android_dist_sources:

..PHONY: android_emulator
android_emulator:
	@$(ANDROID_SDK_ROOT)/tools/emulator -avd test


TARGET_PATH=/data/local/tmp

.PHONY: android_install
android_install:
	$(STRIP) -S build/android-armv7/bin/ztest
	@$(ADB) shell mkdir -p $(TARGET_PATH)/test/data
	@for n in $(shell ls test/data/); do \
		$(ADB) push test/data/$${n} $(TARGET_PATH)/test/data/$${n} ; \
	done
	$(ADB) push build/android-armv7/bin/ztest $(TARGET_PATH)/
	@$(ADB) shell \
		"cd $(TARGET_PATH) && ./ztest --gtest_filter=*.*"


.PHONY: android_zcall
android_zcall:
	$(STRIP) -S build/android-armv7/bin/zcall
	@$(ADB) shell mkdir -p $(TARGET_PATH)/test/data
	$(ADB) push build/android-armv7/bin/zcall $(TARGET_PATH)/


.PHONY: android_shell
android_shell:
	@$(ADB) shell

