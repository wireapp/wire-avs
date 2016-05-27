#
# mod.mk
#

AVS_SRCS += \
	vie/decode.cpp \
	vie/encode.cpp \
	vie/shared.cpp \
	vie/vie.cpp \
	vie/sdp.cpp \
	vie/stats.cpp \
	vie/vie_renderer.cpp \
	vie/capture_router.cpp


AVS_CPPFLAGS_src/vie := \
	-Imediaengine

