#
# mod.mk
#

AVS_SRCS += \
	vie/decode.cpp \
	vie/encode.cpp \
	vie/device.cpp \
	vie/shared.cpp \
	vie/vie.cpp \
	vie/sdp.cpp \
	vie/stats.cpp \
	vie/vie_renderer.cpp \
	vie/vie_render_view_utils.cpp \
	vie/vie_timeout_image_data.cpp


AVS_CPPFLAGS_src/vie := \
	-Imediaengine

ifeq ($(AVS_OS),ios)

AVS_SRCS += \
	vie/vie_render_view_ios.mm

else ifeq ($(AVS_OS),osx)

AVS_SRCS += \
	vie/vie_render_view_osx.mm

else ifeq ($(AVS_OS),android)

AVS_SRCS += \
	vie/vie_render_view_android.cpp

else

AVS_SRCS += \
	vie/vie_render_view_dummy.cpp

endif
