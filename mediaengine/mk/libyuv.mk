
# XXX Looks like we should add libjpeg_turbo for this.
#     For now, though, we just don't define HAVE_JPEG and are good.


MENG_CPPFLAGS_libyuv/source/ += \
	-Imediaengine/libyuv/include

MENG_SRCS += \
	libyuv/source/compare.cc \
	libyuv/source/compare_common.cc \
	libyuv/source/compare_gcc.cc \
	libyuv/source/compare_neon.cc \
	libyuv/source/compare_neon64.cc \
	libyuv/source/compare_win.cc \
	libyuv/source/convert.cc \
	libyuv/source/convert_argb.cc \
	libyuv/source/convert_from.cc \
	libyuv/source/convert_from_argb.cc \
	libyuv/source/convert_jpeg.cc \
	libyuv/source/convert_to_argb.cc \
	libyuv/source/convert_to_i420.cc \
	libyuv/source/cpu_id.cc \
	libyuv/source/mjpeg_decoder.cc \
	libyuv/source/mjpeg_validate.cc \
	libyuv/source/planar_functions.cc \
	libyuv/source/rotate.cc \
	libyuv/source/rotate_any.cc \
	libyuv/source/rotate_argb.cc \
	libyuv/source/rotate_common.cc \
	libyuv/source/rotate_gcc.cc \
	libyuv/source/rotate_neon.cc \
	libyuv/source/rotate_neon64.cc \
	libyuv/source/rotate_win.cc \
	libyuv/source/row_any.cc \
	libyuv/source/row_common.cc \
	libyuv/source/row_gcc.cc \
	libyuv/source/row_neon.cc \
	libyuv/source/row_neon64.cc \
	libyuv/source/row_win.cc \
	libyuv/source/scale.cc \
	libyuv/source/scale_any.cc \
	libyuv/source/scale_argb.cc \
	libyuv/source/scale_common.cc \
	libyuv/source/scale_gcc.cc \
	libyuv/source/scale_neon.cc \
	libyuv/source/scale_neon64.cc \
	libyuv/source/scale_win.cc \
	libyuv/source/video_common.cc \

ifdef HAVE_JPEG
MENG_SRCS += \
	libyuv/source/convert_jpeg.cc \
	libyuv/source/mjpeg_decoder.cc
endif
