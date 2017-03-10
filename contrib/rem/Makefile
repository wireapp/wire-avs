#
# Makefile
#
# Copyright (C) 2010 Creytiv.com
#

# Master version number
VER_MAJOR := 0
VER_MINOR := 5
VER_PATCH := 0

PROJECT   := rem
VERSION   := 0.5.0
OPT_SPEED := 1

LIBRE_MK  := $(shell [ -f ../re/mk/re.mk ] && \
	echo "../re/mk/re.mk")
ifeq ($(LIBRE_MK),)
LIBRE_MK  := $(shell [ -f /usr/share/re/re.mk ] && \
	echo "/usr/share/re/re.mk")
endif
ifeq ($(LIBRE_MK),)
LIBRE_MK  := $(shell [ -f /usr/local/share/re/re.mk ] && \
	echo "/usr/local/share/re/re.mk")
endif

include $(LIBRE_MK)

# List of modules
MODULES += fir
MODULES += g711
MODULES += aubuf aufile auresamp autone
MODULES += au auconv

ifneq ($(HAVE_LIBPTHREAD),)
MODULES += aumix vidmix
endif

MODULES += vid vidconv

LIBS    += -lm

INSTALL := install
ifeq ($(DESTDIR),)
PREFIX  := /usr/local
else
PREFIX  := /usr
endif
ifeq ($(LIBDIR),)
LIBDIR  := $(PREFIX)/lib
endif
INCDIR  := $(PREFIX)/include/rem
CFLAGS	+= -I$(LIBRE_INC) -Iinclude


# XXX
ifneq ($(HAVE_ARMV6),)
CFLAGS		+= -DHAVE_ARMV6=1
endif
ifneq ($(HAVE_NEON),)
CFLAGS		+= -DHAVE_NEON=1
endif


MODMKS	:= $(patsubst %,src/%/mod.mk,$(MODULES))
SHARED  := librem$(LIB_SUFFIX)
STATIC	:= librem.a


include $(MODMKS)


OBJS	?= $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(SRCS)))
OBJS	+= $(patsubst %.S,$(BUILD)/%.o,$(filter %.S,$(SRCS)))


all: $(SHARED) $(STATIC)


-include $(OBJS:.o=.d)


$(SHARED): $(OBJS)
	@echo "  LD      $@"
	@$(LD) $(LFLAGS) $(SH_LFLAGS) $^ -L$(LIBRE_SO) -lre $(LIBS) -o $@


$(STATIC): $(OBJS)
	@echo "  AR      $@"
	@$(AR) $(AFLAGS) $@ $^
ifneq ($(RANLIB),)
	@$(RANLIB) $@
endif

librem.pc:
	@echo 'prefix='$(PREFIX) > librem.pc
	@echo 'exec_prefix=$${prefix}' >> librem.pc
	@echo 'libdir=$${prefix}/lib' >> librem.pc
	@echo 'includedir=$${prefix}/include/rem' >> librem.pc
	@echo '' >> librem.pc
	@echo 'Name: librem' >> librem.pc
	@echo 'Description: Audio and video processing media library' \
		>> librem.pc
	@echo 'Version: '$(VERSION) >> librem.pc
	@echo 'URL: http://creytiv.com/rem.html' >> librem.pc
	@echo 'Libs: -L$${libdir} -lrem -lre' >> librem.pc
	@echo 'Cflags: -I$${includedir}' >> librem.pc

$(BUILD)/%.o: src/%.c $(BUILD) Makefile $(MK) $(MODMKS)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)


$(BUILD)/%.o: src/%.S $(BUILD) Makefile $(MK) $(MODMKS)
	@echo "  AS      $@"
	@$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)


$(BUILD): Makefile $(MK) $(MODMKS)
	@mkdir -p $(patsubst %,$(BUILD)/%,$(sort $(dir $(SRCS))))
	@touch $@


.PHONY: clean
clean:
	@rm -rf $(SHARED) $(STATIC) librem.pc test.d test.o test $(BUILD)


install: $(SHARED) $(STATIC) librem.pc
	@mkdir -p $(DESTDIR)$(LIBDIR) $(DESTDIR)$(LIBDIR)/pkgconfig \
		$(DESTDIR)$(INCDIR)
	$(INSTALL) -m 0644 $(shell find include -name "*.h") \
		$(DESTDIR)$(INCDIR)
	$(INSTALL) -m 0755 $(SHARED) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0755 $(STATIC) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0644 librem.pc $(DESTDIR)$(LIBDIR)/pkgconfig

.PHONY: uninstall
uninstall:
	@rm -rf $(DESTDIR)$(INCDIR)
	@rm -f $(DESTDIR)$(LIBDIR)/$(SHARED)
	@rm -f $(DESTDIR)$(LIBDIR)/$(STATIC)
	@rm -f $(DESTDIR)$(LIBDIR)/pkgconfig/librem.pc

-include test.d

test.o:	test.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)

test$(BIN_SUFFIX): test.o $(SHARED) $(STATIC)
	@echo "  LD      $@"
	@$(LD) $(LFLAGS) $< -L. -lrem -lre $(LIBS) -o $@
