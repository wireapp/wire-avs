#
# mod.mk
#

AVS_SRCS += \
	keystore/keystore.c 

ifneq ($(HAVE_WEBRTC),1)
AVS_SRCS += \
	keystore/hkdf.c
endif

