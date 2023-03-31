#
# Makefile Snippet for Third-party Libraries
#
#
# This snippet allows building all the third-party libraries that are
# contained in this here repository.
#
# The snippet has a main target "contrib" that builds all the libraries
# for the current configuration. However, there is normally no need to
# use this target. All the other bits and bops have dependencies on the
# libraries they need, so by building them you'll also build those
# contrib libraries necessary.
#
# The rules for all the contrib libraries follow a similar pattern: For
# each library there is a CONTRIB_<library>_TARGET that either points to
# the static library or, in case of OpenSSL where there is two, a stamp
# file. This target depends on CONTRIB_<library>_FILES. This one is
# collected by running "git ls-files" over the sources to ensure that
# the library is rebuilt if the sources change.
#
# Because of this it is very important that when upgrading a contrib
# library to make sure that none of the files that change upon each
# rebuild are checked into git. The source distribution may contain such
# files, so carefully weed them all out. Also make sure that
# contrib/.gitignore contains all files produced by the build process.
#
# When using the contrib libraries, add $(CONTRIB_<library>_TARGET) to
# your object dependencies. The linker flags for linking with the
# libraries are in $(CONTRIB_<library>_LIBS) and a list of the static
# libraries complete with correct path in $(CONTRIB_<library>_LIB_FILES).
#
# Currently, no other argument variables are being defined. That may
# change later.
#
# The include files are all installed into build/$(AVS_PAIR)/include as
# if that were /usr/include. The $(CPPFLAGS) already contain an -I flag
# for this path, so you should be all set.
#


JOBS	:= -j8


# Select what we build based on the target platform
#
CONTRIB_PARTS_android := \
	LIBRE LIBREW SODIUM
CONTRIB_PARTS_ios := \
	GTEST LIBRE LIBREW SODIUM
CONTRIB_PARTS_iossim := \
	GTEST LIBRE LIBREW SODIUM
CONTRIB_PARTS_linux := \
	GTEST LIBRE LIBREM LIBREW \
	CRYPTOBOX
CONTRIB_PARTS_osx := \
	GTEST LIBRE LIBREM LIBREW CRYPTOBOX SODIUM
CONTRIB_PARTS_wasm := \
	LIBRE SODIUM

CONTRIB_PARTS := $(CONTRIB_PARTS_$(AVS_OS))
ifeq ($(CONTRIB_PARTS),)
$(error contrib: Unknown target OS '$(AVS_OS)'.)
endif

CONTRIB_BASE := $(shell pwd)/contrib

CONTRIB_INCLUDE_PATH := $(CONTRIB_BASE)/include

#--- OpenSSL ---

ifneq ($(HAVE_WEBRTC),1)

CONTRIB_PARTS += OPENSSL

CONTRIB_OPENSSL_PATH := $(CONTRIB_BASE)/openssl
CONTRIB_OPENSSL_BUILD_PATH := $(BUILD_TARGET)/contrib/openssl

CONTRIB_OPENSSL_OPTIONS := \
	no-bf \
	no-blake2 \
	no-camellia \
	no-capieng \
	no-cast \
	no-comp \
	no-dso \
	no-engine \
        no-err \
	no-gost \
	no-heartbeats \
	no-hw \
	no-idea \
	no-md2 \
	no-md4 \
	no-mdc2 \
	no-psk \
	no-rc2 \
	no-rc4 \
	no-rc5 \
	no-sctp \
	no-seed \
	no-shared \
	no-srp \
	no-ssl3 \
	no-async

CONTRIB_OPENSSL_COMPILER_android_armv7 := android
CONTRIB_OPENSSL_COMPILER_android_i386  := android-x86
CONTRIB_OPENSSL_COMPILER_ios_armv7     := cc
CONTRIB_OPENSSL_COMPILER_ios_armv7s    := cc
CONTRIB_OPENSSL_COMPILER_ios_arm64     := cc
CONTRIB_OPENSSL_COMPILER_ios_i386      := darwin-i386-cc
CONTRIB_OPENSSL_COMPILER_ios_x86_64    := darwin64-x86_64-cc
CONTRIB_OPENSSL_COMPILER_linux_x86_64  := linux-x86_64
CONTRIB_OPENSSL_COMPILER_linux_i386    := linux-generic32
CONTRIB_OPENSSL_COMPILER_linux_armv6   := cc
CONTRIB_OPENSSL_COMPILER_linux_armv7   := cc
CONTRIB_OPENSSL_COMPILER_linux_armv7l  := cc
CONTRIB_OPENSSL_COMPILER_osx_x86_64    := darwin64-x86_64-cc
CONTRIB_OPENSSL_COMPILER_wasm_generic  := linux-generic32
CONTRIB_OPENSSL_OPTIONS_android        := \
	threads \
	--cross-compile-prefix=$(BIN_PATH)-
CONTRIB_OPENSSL_OPTIONS_linux          := threads
CONTRIB_OPENSSL_OPTIONS_osx            := threads
CONTRIB_OPENSSL_OPTIONS_ios            := threads
CONTRIB_OPENSSL_OPTIONS_wasm	       := \
	no-threads \
	no-asm \
	--cross-compile-prefix=$(BIN_PATH)/em
ifeq ($(AVS_OS),wasm)
CONTRIB_OPENSSL_CC 		       := CC=cc
endif

CONTRIB_OPENSSL_FILES := $(shell git ls-files contrib/openssl | grep -v opensslconf.h )
CONTRIB_OPENSSL_TARGET := $(BUILD_TARGET)/stamp/openssl

CONTRIB_OPENSSL_LIBS := -lssl -lcrypto 
CONTRIB_OPENSSL_LIB_FILES := \
	$(BUILD_TARGET)/lib/libssl.a \
	$(BUILD_TARGET)/lib/libcrypto.a


$(CONTRIB_OPENSSL_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_OPENSSL_FILES)
	@mkdir -p $(CONTRIB_OPENSSL_BUILD_PATH)
	@rsync -aq $(CONTRIB_OPENSSL_PATH)/ $(CONTRIB_OPENSSL_BUILD_PATH)/
	@$(MAKE) -C $(CONTRIB_OPENSSL_BUILD_PATH) clean || echo ""
	cd $(CONTRIB_OPENSSL_BUILD_PATH) && \
	        $(CONTRIB_OPENSSL_CC) ./Configure --prefix="$(BUILD_TARGET)" \
		$(CONTRIB_OPENSSL_OPTIONS) \
		$(CONTRIB_OPENSSL_OPTIONS_$(AVS_OS)) \
		$(CONTRIB_OPENSSL_COMPILER_$(AVS_OS)_$(AVS_ARCH)) \
		"$(CPPFLAGS) $(CFLAGS)" -D__STDC_NO_ATOMICS__ -DPURIFY
	@$(MAKE) -C $(CONTRIB_OPENSSL_BUILD_PATH) clean
	@$(MAKE) -C $(CONTRIB_OPENSSL_BUILD_PATH) depend
	CROSS_SYSROOT="$(TOOLCHAIN_PATH)/sysroot/" \
		$(MAKE) -C $(CONTRIB_OPENSSL_BUILD_PATH) \
		build_libs
	@$(MAKE) -C $(CONTRIB_OPENSSL_BUILD_PATH) install_dev
	mkdir -p $(dir $@)
	touch $@

contrib_openssl: $(CONTRIB_OPENSSL_TARGET)

contrib_openssl_clean:
	rm -f $(CONTRIB_OPENSSL_TARGET)
	rm -rf $(CONTRIB_OPENSSL_BUILD_PATH)

endif

#--- breakpad ---

CONTRIB_BREAKPAD_PATH   := $(CONTRIB_BASE)/breakpad
CONTRIB_BREAKPAD_BUILD_PATH := $(BUILD_TARGET)/breakpad
CONTRIB_BREAKPAD_TARGET := $(BUILD_TARGET)/lib/libbreakpad.a
CONTRIB_BREAKPAD_FILES  := $(shell git ls-files $(CONTRIB_BREAKPAD_PATH) | sed -e 's/ /\\ /g')

CONTRIB_BREAKPAD_LIBS := -lbreakpad
CONTRIB_BREAKPAD_LIB_FILES := $(CONTRIB_BREAKPAD_TARGET)


$(CONTRIB_BREAKPAD_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_BREAKPAD_FILES)
	@rm -rf $(CONTRIB_BREAKPAD_BUILD_PATH)
	@mkdir -p $(CONTRIB_BREAKPAD_BUILD_PATH)
	cd $(CONTRIB_BREAKPAD_BUILD_PATH) && \
		PATH=$(TOOLCHAIN_PATH)/bin:$(PATH) \
		CC="$(CC)" \
		CXX="$(CXX)" \
		AR="$(AR)" \
		RANLIB="$(RANLIB)" \
		CFLAGS="$(CPPFLAGS) $(CFLAGS)" \
		CXXFLAGS="$(CPPFLAGS) $(CXXFLAGS)" \
		LDFLAGS="$(LFLAGS) $(LIBS)" \
		$(CONTRIB_BREAKPAD_PATH)/configure --prefix="$(BUILD_TARGET)" \
			$(HOST_OPTIONS) \
			--disable-processor \
			--disable-tools && \
		PATH=$(TOOLCHAIN_PATH)/bin:$(PATH) $(MAKE) -j4
	mkdir -p $(BUILD_TARGET)/include/breakpad
	cp $(CONTRIB_BREAKPAD_PATH)/src/hockey/dumpcall.h \
		$(BUILD_TARGET)/include/breakpad
	mkdir -p $(BUILD_TARGET)/lib
	cp $(CONTRIB_BREAKPAD_BUILD_PATH)/src/client/linux/libbreakpad_client.a \
		$(BUILD_TARGET)/lib/libbreakpad.a


#--- gtest ---

CONTRIB_GTEST_PATH := $(CONTRIB_BASE)/googletest/googletest/
CONTRIB_GTEST_BUILD_PATH := $(BUILD_TARGET)/contrib/googletest/googletest/
CONTRIB_GTEST_TARGET := $(BUILD_TARGET)/lib/libgtest.a
CONTRIB_GTEST_FILES := $(shell git ls-files $(CONTRIB_GTEST_PATH))

CONTRIB_GTEST_CXXFLAGS_OS_android := -DGTEST_HAS_RTTI=0
CONTRIB_GTEST_CXXFLAGS_OS := $(CONTRIB_GTEST_CXXFLAGS_OS_$(AVS_OS))

CONTRIB_GTEST_LIBS := -lgtest
CONTRIB_GTEST_LIB_FILES := $(CONTRIB_GTEST_TARGET)


$(CONTRIB_GTEST_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_GTEST_FILES)
	@mkdir -p $(CONTRIB_GTEST_BUILD_PATH)
	@rsync -aq $(CONTRIB_GTEST_PATH)/ $(CONTRIB_GTEST_BUILD_PATH)/
	rm -f $(CONTRIB_GTEST_BUILD_PATH)/gtest-all.o
	cd $(CONTRIB_GTEST_BUILD_PATH) && \
		$(CXX) \
		-isystem include \
		-I. \
		$(CPPFLAGS) $(CXXFLAGS) $(CONTRIB_GTEST_CXXFLAGS_OS) \
		-pthread \
		-c src/gtest-all.cc
	@mkdir -p $(BUILD_TARGET)/lib
	cd $(CONTRIB_GTEST_BUILD_PATH) && \
		$(AR) -rv $(BUILD_TARGET)/lib/libgtest.a gtest-all.o
	@cp -R $(CONTRIB_GTEST_BUILD_PATH)/include $(BUILD_TARGET)

contrib_gtest: $(CONTRIB_GTEST_TARGET)



#--- prebuilt webrtc libs ---

ifeq ($(HAVE_WEBRTC),1)

CONTRIB_WEBRTC_ROOT := $(CONTRIB_BASE)/webrtc
CONTRIB_WEBRTC_CFLAGS := -I$(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/include \
			 -I$(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/include/third_party/opus/src/include \
			 -I$(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/include/third_party/libyuv/include

CONTRIB_WEBRTC_LIBS := -L$(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/lib/$(AVS_PAIR) -lwebrtc


CONTRIB_WEBRTC_ZIP_FILES := \
	$(CONTRIB_WEBRTC_ROOT)/webrtc_$(WEBRTC_VER)_headers.zip \
	$(CONTRIB_WEBRTC_ROOT)/webrtc_$(WEBRTC_VER)_osx.zip \
	$(CONTRIB_WEBRTC_ROOT)/webrtc_$(WEBRTC_VER)_ios.zip \
	$(CONTRIB_WEBRTC_ROOT)/webrtc_$(WEBRTC_VER)_linux.zip \
	$(CONTRIB_WEBRTC_ROOT)/webrtc_$(WEBRTC_VER)_android.zip

$(CONTRIB_WEBRTC_ZIP_FILES):
	@mkdir -p $(CONTRIB_WEBRTC_ROOT) && \
	./scripts/get_webrtc.py $(WEBRTC_VER)

CONTRIB_WEBRTC_TARGET := \
	$(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/lib/$(AVS_PAIR)/libwebrtc.a

$(CONTRIB_WEBRTC_TARGET): $(CONTRIB_WEBRTC_ZIP_FILES)
	@if [ ! -f "$@" ]; then \
		mkdir -p $(CONTRIB_WEBRTC_ROOT) && \
		cd $(CONTRIB_WEBRTC_ROOT) && \
		for zip in $(CONTRIB_WEBRTC_ZIP_FILES); do \
			echo Unpacking `basename $$zip`; \
			unzip -oq $$zip; \
		done; \
		mkdir -p $(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/lib/wasm-generic; \
		touch $(CONTRIB_WEBRTC_ROOT)/$(WEBRTC_VER)/lib/wasm-generic/libwebrtc.a; \
	fi

contrib_webrtc: $(CONTRIB_WEBRT_TARGET)

contrib_webrtc_clean:
	@rm -rf $(CONTRIB_WEBRTC_ROOT)
endif

#--- libre ---

CONTRIB_LIBRE_FAMILY_OPTIONS_arm := RELEASE=1
CONTRIB_LIBRE_FAMILY_OPTIONS_arm64 := RELEASE=1
CONTRIB_LIBRE_OS_OPTIONS_android := \
	HAVE_INTTYPES_H=1 \
	HAVE_LIBRESOLV= \
	HAVE_RESOLV= \
	HAVE_PTHREAD=1 \
	HAVE_PTHREAD_RWLOCK=1 \
	HAVE_EPOLL=1 \
	HAVE_LIBPTHREAD= \
	HAVE_INET_PTON=1 \
	HAVE_GETIFADDRS= \
	PEDANTIC= \
	OS=linux \
	USE_OPENSSL=1 \
	USE_OPENSSL_DTLS= \
	USE_OPENSSL_SRTP= \
	USE_OPENSSL_AES=1 \
	USE_OPENSSL_HMAC=1 \

CONTRIB_LIBRE_OS_OPTIONS_ios := \
	USE_OPENSSL=1 \
	USE_OPENSSL_DTLS= \
	USE_OPENSSL_SRTP= \
	USE_OPENSSL_AES= \
	USE_OPENSSL_HMAC= \
	USE_APPLE_COMMONCRYPTO=1

CONTRIB_LIBRE_OS_OPTIONS_iossim := \
	USE_OPENSSL=1 \
	USE_OPENSSL_DTLS= \
	USE_OPENSSL_SRTP= \
	USE_OPENSSL_AES= \
	USE_OPENSSL_HMAC= \
	USE_APPLE_COMMONCRYPTO=1

CONTRIB_LIBRE_OS_OPTIONS_osx := \
	USE_OPENSSL=1 \
	USE_OPENSSL_DTLS=1 \
	USE_OPENSSL_SRTP=1 \
	USE_OPENSSL_AES=1 \
	USE_OPENSSL_HMAC=1 \
	USE_APPLE_COMMONCRYPTO=

CONTRIB_LIBRE_OS_OPTIONS_linux := \
	HAVE_EPOLL=1 \
	USE_OPENSSL=1 \
	USE_OPENSSL_DTLS=1 \
	USE_OPENSSL_SRTP=1 \
	USE_OPENSSL_AES=1 \
	USE_OPENSSL_HMAC=1

CONTRIB_LIBRE_OS_OPTIONS_wasm := \
	USE_MQUEUE_BYPASS=1 \
	HAVE_EPOLL= \
	HAVE_KQUEUE= \
	HAVE_SELECT= \
	USE_OPENSSL=yes \
	USE_OPENSSL_DTLS= \
	USE_OPENSSL_SRTP= \
	USE_OPENSSL_AES= \
	USE_OPENSSL_HMAC= \
	DISABLE_NETWORK_MODULES=1


CONTRIB_LIBRE_PATH := contrib/re
CONTRIB_LIBRE_TARGET := $(BUILD_TARGET)/lib/libre.a
CONTRIB_LIBRE_FILES := $(shell find $(CONTRIB_LIBRE_PATH) -name "*.[hc]" )

CONTRIB_LIBRE_LIBS := -lre
ifeq ($(AVS_OS),android)
else
CONTRIB_LIBRE_LIBS += -lresolv
endif
CONTRIB_LIBRE_LIB_FILES := \
	$(CONTRIB_LIBRE_TARGET) 

ifeq ($(HAVE_WEBRTC),1)
LIBRE_CFLAGS += "-DUSE_BORINGSSL"
else
CONTRIB_LIBRE_LIBS += $(CONTRIB_OPENSSL_LIBS)
CONTRIB_LIBRE_LIB_FILES += $(CONTRIB_OPENSSL_LIB_FILES)
endif

$(CONTRIB_LIBRE_TARGET): $(TOOLCHAIN_MASTER) \
			 $(CONTRIB_LIBRE_FILES) \
			 $(CONTRIB_WEBRTC_TARGET) \
			 $(CONTRIB_OPENSSL_TARGET)
	cd $(CONTRIB_LIBRE_PATH) && \
		rm -f libre.a && \
		make libre.a $(JOBS) \
		BUILD=build-$(AVS_OS)-$(AVS_ARCH) \
		CC="$(CC)" \
		AR="$(AR)" \
		RANLIB="$(RANLIB)" \
		EXTRA_CFLAGS="$(CPPFLAGS) $(CFLAGS) \
			-DMAIN_DEBUG=0 -DTMR_DEBUG=0 $(LIBRE_CFLAGS)" \
		EXTRA_LFLAGS="$(LFLAGS) $(LIBS)" \
		ARCH="$(AVS_ARCH)" \
		SYSROOT="$(SYSROOT)" \
		SYSROOT_ALT="$(BUILD_TARGET)" \
		PREFIX= USE_ZLIB= \
		DESTDIR=$(BUILD_TARGET) \
		$(CONTRIB_LIBRE_FAMILY_OPTIONS_$(AVS_FAMILY)) \
		$(CONTRIB_LIBRE_OS_OPTIONS_$(AVS_OS))
	mkdir -p \
		$(BUILD_TARGET)/include/re \
		$(BUILD_TARGET)/lib \
		$(BUILD_TARGET)/share/re
	install -m 0644 \
		$(shell find $(CONTRIB_LIBRE_PATH)/include -name "*.h") \
		$(BUILD_TARGET)/include/re
	install -m 0644 $(CONTRIB_LIBRE_PATH)/libre.a $(BUILD_TARGET)/lib
	install -m 0644 $(CONTRIB_LIBRE_PATH)/mk/re.mk $(BUILD_TARGET)/share/re


contrib_libre: $(CONTRIB_LIBRE_TARGET)


#--- librem ---

CONTRIB_LIBREM_OS_OPTIONS_android := \
	HAVE_LIBRESOLV= \
	HAVE_PTHREAD=1 \
	HAVE_PTHREAD_RWLOCK=1 \
	HAVE_LIBPTHREAD= \
	HAVE_INET_PTON=1 \
	PEDANTIC= \
	OS=linux

CONTRIB_LIBREM_PATH := contrib/rem
CONTRIB_LIBREM_TARGET := $(BUILD_TARGET)/lib/librem.a
CONTRIB_LIBREM_FILES := $(shell git ls-files $(CONTRIB_LIBREM_PATH))

CONTRIB_LIBREM_LIBS := -lrem $(CONTRIB_LIBRE_LIBS)
CONTRIB_LIBREM_LIB_FILES := \
	$(CONTRIB_LIBREM_TARGET) \
	$(CONTRIB_LIBRE_LIB_FILES)


$(CONTRIB_LIBREM_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_LIBRE_TARGET) \
			  $(CONTRIB_LIBREM_FILES)
	@cd $(CONTRIB_LIBREM_PATH) && \
		rm -f librem.a && \
		make librem.a $(JOBS) \
		BUILD=build-$(AVS_OS)-$(AVS_ARCH) \
		CC="$(CC)" \
		AR="$(AR)" \
		RANLIB="$(RANLIB)" \
		EXTRA_CFLAGS="$(CPPFLAGS) $(CFLAGS)" \
		EXTRA_LFLAGS="$(LFLAGS) $(LIBS)" \
		SYSROOT="$(SYSROOT)" \
		SYSROOT_ALT="$(BUILD_TARGET)" \
		PREFIX= USE_OPENSSL=yes USE_OPENSSL_DTLS=1 \
		USE_OPENSSL_SRTP=1 USE_ZLIB=yes \
		DESTDIR=$(BUILD_TARGET) \
		$(CONTRIB_LIBREM_OS_OPTIONS_$(AVS_OS))
	mkdir -p \
		$(BUILD_TARGET)/include/rem \
		$(BUILD_TARGET)/lib
	install -m 0644 \
		$(shell find $(CONTRIB_LIBREM_PATH)/include -name "*.h") \
		$(BUILD_TARGET)/include/rem
	install -m 0644 $(CONTRIB_LIBREM_PATH)/librem.a $(BUILD_TARGET)/lib

contrib_librem: $(CONTRIB_LIBREM_TARGET)


#--- librew ---

CONTRIB_LIBREW_OS_OPTIONS_android := \
	HAVE_LIBRESOLV= \
	HAVE_PTHREAD=1 \
	HAVE_PTHREAD_RWLOCK=1 \
	HAVE_LIBPTHREAD= \
	HAVE_INET_PTON=1 \
	HAVE_INTTYPES_H=1 \
	PEDANTIC= \
	OS=linux

CONTRIB_LIBREW_PATH := contrib/rew
CONTRIB_LIBREW_TARGET := $(BUILD_TARGET)/lib/librew.a
CONTRIB_LIBREW_FILES := $(shell find $(CONTRIB_LIBREW_PATH) -name "*.[hc]" )


CONTRIB_LIBREW_LIBS := -lrew $(CONTRIB_LIBRE_LIBS)
CONTRIB_LIBREW_LIB_FILES := \
	$(CONTRIB_LIBREW_TARGET) \
	$(CONTRIB_LIBRE_LIB_FILES)


$(CONTRIB_LIBREW_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_LIBRE_TARGET) \
			  $(CONTRIB_LIBREW_FILES)
	@cd $(CONTRIB_LIBREW_PATH) && \
		rm -f librew.a && \
		$(MAKE) install-static $(JOBS) \
		BUILD=build-$(AVS_OS)-$(AVS_ARCH) \
		CC="$(CC)" \
		AR="$(AR)" \
		RANLIB="$(RANLIB)" \
		EXTRA_CFLAGS="$(CPPFLAGS) $(CFLAGS)" \
		EXTRA_LFLAGS="$(LFLAGS) $(LIBS)" \
		SYSROOT="$(SYSROOT)" \
		SYSROOT_ALT="$(BUILD_TARGET)" \
		PREFIX= USE_OPENSSL=yes USE_OPENSSL_DTLS=1 \
		USE_OPENSSL_SRTP=1 USE_ZLIB=yes \
		DESTDIR=$(BUILD_TARGET) \
		$(CONTRIB_LIBREW_OS_OPTIONS_$(AVS_OS))

contrib_librew: $(CONTRIB_LIBREW_TARGET)


#--- Protobuf ---

HOST_PROTOC	:= protoc-c

CONTRIB_PROTOBUF_PATH := contrib/generic-message-proto

CONTRIB_PROTOBUF_TARGET_PATH := \
	src/protobuf
CONTRIB_PROTOBUF_TARGET := \
	src/protobuf/proto/messages.pb-c.c

CONTRIB_PROTOBUF_FILES := $(shell git ls-files $(CONTRIB_PROTOBUF_PATH))

# NOTE: we depend on a system install protoc-c for now
CONTRIB_PROTOBUF_LIBS := $(shell pkg-config --libs 'libprotobuf-c >= 1.0.0')

PROTO_INPUT	:= $(CONTRIB_PROTOBUF_PATH)/proto/messages.proto


$(CONTRIB_PROTOBUF_TARGET): $(PROTO_INPUT)
	$(HOST_PROTOC) \
		-I$(CONTRIB_PROTOBUF_PATH) \
		--c_out=$(CONTRIB_PROTOBUF_TARGET_PATH) \
		$(PROTO_INPUT)


$(BUILD_TARGET)/include/proto/messages.pb-c.h: \
	$(CONTRIB_PROTOBUF_TARGET_PATH)/proto/messages.pb-c.c
	mkdir -p $(BUILD_TARGET)/include/proto
	cp $(CONTRIB_PROTOBUF_TARGET_PATH)/proto/messages.pb-c.h \
		$(BUILD_TARGET)/include/proto/.


.PHONY: contrib_protobuf
contrib_protobuf: $(CONTRIB_PROTOBUF_TARGET)

contrib_protobuf_clean:
	rm -f $(BUILD_TARGET)/include/proto/messages.pb-c.h
	rm -f $(CONTRIB_PROTOBUF_TARGET_PATH)/proto/messages.pb-c.[hc]


#--- Cryptobox ---

CONTRIB_CRYPTOBOX_PATH := contrib/cryptobox-c
CONTRIB_CRYPTOBOX_TARGET := $(BUILD_TARGET)/lib/libcryptobox.a
CONTRIB_CRYPTOBOX_DEPS := $(CONTRIB_CRYPTOBOX_TARGET)
CONTRIB_CRYPTOBOX_FILES := $(shell git ls-files $(CONTRIB_CRYPTOBOX_PATH))

CONTRIB_CRYPTOBOX_LIBS := -lcryptobox
#CONTRIB_CRYPTOBOX_LIBS += $(shell pkg-config --libs libsodium)

CONTRIB_CRYPTOBOX_LIB_FILES := $(CONTRIB_CRYPTOBOX_TARGET)

$(CONTRIB_CRYPTOBOX_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_CRYPTOBOX_FILES)
	sed -i.bak s/cdylib/staticlib/g $(CONTRIB_CRYPTOBOX_PATH)/Cargo.toml
	$(MAKE) -C $(CONTRIB_CRYPTOBOX_PATH) clean
	$(MAKE) -C $(CONTRIB_CRYPTOBOX_PATH) compile
	mkdir -p $(BUILD_TARGET)/include $(BUILD_TARGET)/lib
	cp $(CONTRIB_CRYPTOBOX_PATH)/src/cbox.h \
		$(BUILD_TARGET)/include/.
	cp $(CONTRIB_CRYPTOBOX_PATH)/target/debug/libcryptobox.a \
		$(BUILD_TARGET)/lib/.
#	sed -i.bak s/staticlib/cdylib/g $(CONTRIB_CRYPTOBOX_PATH)/Cargo.toml
#	rm -f $(CONTRIB_CRYPTOBOX_PATH)/*.bak


.PHONY: contrib_cryptobox
contrib_cryptobox: $(CONTRIB_CRYPTOBOX_TARGET)


contrib_cryptobox_clean:
	rm -f $(CONTRIB_CRYPTOBOX_TARGET)
	$(MAKE) -C $(CONTRIB_CRYPTOBOX_PATH) clean


#--- sodium ---

ifneq ($(AVS_OS),linux)
CONTRIB_SODIUM_PATH := $(CONTRIB_BASE)/sodium
CONTRIB_SODIUM_BUILD_PATH := $(BUILD_TARGET)/sodium
CONTRIB_SODIUM_CONFIG_TARGET := $(CONTRIB_SODIUM_PATH)/configure
CONTRIB_SODIUM_TARGET := $(BUILD_TARGET)/lib/libsodium.a
CONTRIB_SODIUM_FILES := $(shell git ls-files $(CONTRIB_SODIUM_PATH))

CONTRIB_SODIUM_LIBS := -lsodium
CONTRIB_SODIUM_LIB_FILES := $(CONTRIB_SODIUM_TARGET)

CONTRIB_SODIUM_CFLAGS := -I$(CONTRIB_SODIUM_PATH)

ifeq ($(AVS_OS),android)
CONTRIB_SODIUM_CFLAGS += -fPIC
CONTRIB_SODIUM_LDFLAGS += -mrdrnd
endif



$(CONTRIB_SODIUM_CONFIG_TARGET):	$(CONTRIB_SODIUM_PATH)/autogen.sh
	cd $(CONTRIB_SODIUM_PATH) && \
	./autogen.sh


#
# NOTE: cannot use --enable-minimal as wide api used by cryptobox
#
CONTRIB_SODIUM_OS_OPTIONS_android := \
	--enable-static \
	--enable-shared \
	--host=arm-none-eabi

CONTRIB_SODIUM_OS_OPTIONS_ios := \
	--enable-static \
	--disable-shared \
	--host=arm-none-eabi

CONTRIB_SODIUM_OS_OPTIONS_iossim := \
	--enable-static \
	--disable-shared \
	--host=arm-none-eabi

CONTRIB_SODIUM_OS_OPTIONS_osx := \
	--enable-static \
	--disable-shared \
	--host=arm-none-eabi

CONTRIB_SODIUM_OS_OPTIONS_linux := \
	--enable-static \
	--disable-shared \
	--host=arm-none-eabi

CONTRIB_SODIUM_OS_OPTIONS_wasm := \
	--enable-static \
	--disable-shared \
	--host=arm-none-eabi


$(CONTRIB_SODIUM_TARGET): $(TOOLCHAIN_MASTER) $(CONTRIB_SODIUM_CONFIG_TARGET) \
	$(CONTRIB_SODIUM_DEPS) $(CONTRIB_SODIUM_FILES)
	@rm -rf $(CONTRIB_SODIUM_BUILD_PATH)
	@mkdir -p $(CONTRIB_SODIUM_BUILD_PATH)
	cd $(CONTRIB_SODIUM_BUILD_PATH) && \
		CC="$(CC)" \
		CXX="$(CXX)" \
		RANLIB="$(RANLIB)" \
		AR="$(AR)" \
		CFLAGS="$(CPPFLAGS) $(CFLAGS) $(CONTRIB_SODIUM_CFLAGS) -Os" \
		CXXFLAGS="$(CPPFLAGS) $(CXXFLAGS)" \
		LDFLAGS="$(CONTRIB_SODIUM_LDFLAGS)" \
		\
		$(CONTRIB_SODIUM_PATH)/configure \
			$(CONTRIB_SODIUM_OS_OPTIONS_$(AVS_OS)) \
			--prefix="$(BUILD_TARGET)" \
			$(HOST_OPTIONS)
		$(MAKE) -C $(CONTRIB_SODIUM_BUILD_PATH) clean
	$(MAKE) $(JOBS) -C $(CONTRIB_SODIUM_BUILD_PATH)
	$(MAKE) -C $(CONTRIB_SODIUM_BUILD_PATH) install


.PHONY: contrib_sodium
contrib_sodium: $(CONTRIB_SODIUM_TARGET)

contrib_sodium_clean:
	$(MAKE) -C $(CONTRIB_SODIUM_BUILD_PATH) clean
	@rm -f $(CONTRIB_SODIUM_CONFIG_TARGET)
endif

#--- Phony Targets ---

.PHONY: contrib contrib_clean
contrib: $(foreach part,$(CONTRIB_PARTS),$(CONTRIB_$(part)_TARGET))
contrib_clean:
	@rm -rf $(BUILD_TARGET)
	@make -C contrib/re distclean
	@make -C contrib/rem distclean
	@make -C contrib/rew distclean
	@rm -f $(CONTRIB_PROTOBUF_TARGET)


