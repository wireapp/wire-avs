
MENG_CPPFLAGS_webrtc/modules/video_render/ := \
	-DWEBRTC_INCLUDE_INTERNAL_VIDEO_RENDER

MENG_SRCS += \
	webrtc/modules/video_render/external/video_render_external_impl.cc \
	webrtc/modules/video_render/video_render_internal_impl.cc \
	webrtc/modules/video_render/video_render_impl.cc

ifeq ($(AVS_OS),android)
MENG_SRCS += \
	webrtc/modules/video_render/android/video_render_android_impl.cc \
	webrtc/modules/video_render/android/video_render_android_native_opengl2.cc \
	webrtc/modules/video_render/android/video_render_android_surface_view.cc \
	webrtc/modules/video_render/android/video_render_opengles20.cc

else ifeq ($(AVS_OS),ios)
MENG_OCPPFLAGS_webrtc/modules/video_render/ios/ += \
	-fobjc-arc

MENG_SRCS += \
	webrtc/modules/video_render/ios/open_gles20.mm \
	webrtc/modules/video_render/ios/video_render_ios_channel.mm \
	webrtc/modules/video_render/ios/video_render_ios_gles20.mm \
	webrtc/modules/video_render/ios/video_render_ios_impl.mm \
	webrtc/modules/video_render/ios/video_render_ios_view.mm

else ifeq ($(AVS_OS),linux)
MENG_SRCS += \
	webrtc/modules/video_render/linux/video_render_linux_impl.cc \
	webrtc/modules/video_render/linux/video_x11_channel.cc \
	webrtc/modules/video_render/linux/video_x11_render.cc

else ifeq ($(AVS_OS),osx)
MENG_CPPFLAGS_webrtc/modules/video_render/ += \
	-DCOCOA_RENDERING=1

MENG_SRCS += \
	webrtc/modules/video_render/mac/cocoa_full_screen_window.mm \
	webrtc/modules/video_render/mac/cocoa_render_view.mm \
	webrtc/modules/video_render/mac/video_render_agl.cc \
	webrtc/modules/video_render/mac/video_render_mac_carbon_impl.cc \
	webrtc/modules/video_render/mac/video_render_mac_cocoa_impl.mm \
	webrtc/modules/video_render/mac/video_render_nsopengl.mm

endif 

