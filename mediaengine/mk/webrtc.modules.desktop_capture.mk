
MENG_SRCS += \
	webrtc/modules/desktop_capture/desktop_and_cursor_composer.cc \
	webrtc/modules/desktop_capture/desktop_frame.cc \
	webrtc/modules/desktop_capture/desktop_geometry.cc \
	webrtc/modules/desktop_capture/desktop_capture_options.cc \
	webrtc/modules/desktop_capture/desktop_region.cc \
	webrtc/modules/desktop_capture/differ.cc \
	webrtc/modules/desktop_capture/differ_block.cc \
	webrtc/modules/desktop_capture/mouse_cursor.cc \
	webrtc/modules/desktop_capture/screen_capturer_helper.cc \
	webrtc/modules/desktop_capture/shared_desktop_frame.cc \
	webrtc/modules/desktop_capture/shared_memory.cc

ifeq ($(AVS_OS),osx)
MENG_SRCS += \
	webrtc/modules/desktop_capture/mac/desktop_configuration.mm \
	webrtc/modules/desktop_capture/mac/desktop_configuration_monitor.cc \
	webrtc/modules/desktop_capture/mac/full_screen_chrome_window_detector.cc \
	webrtc/modules/desktop_capture/mac/scoped_pixel_buffer_object.cc \
	webrtc/modules/desktop_capture/mac/window_list_utils.cc \
	webrtc/modules/desktop_capture/mouse_cursor_monitor_mac.mm \
	webrtc/modules/desktop_capture/screen_capturer_mac.mm \
	webrtc/modules/desktop_capture/window_capturer_mac.mm \

endif

ifneq ($(USE_X11),)
MENG_SRCS += \
	webrtc/modules/desktop_capture/mouse_cursor_monitor_x11.cc \
	webrtc/modules/desktop_capture/screen_capturer_x11.cc \
	webrtc/modules/desktop_capture/window_capturer_x11.cc \
	webrtc/modules/desktop_capture/x11/shared_x_display.cc \
	webrtc/modules/desktop_capture/x11/x_error_trap.cc \
	webrtc/modules/desktop_capture/x11/x_server_pixel_buffer.cc

endif

ifneq ($(AVS_OS),osx)
ifeq  ($(USE_X11),)
MENG_SRCS += \
	webrtc/modules/desktop_capture/mouse_cursor_monitor_null.cc \
	webrtc/modules/desktop_capture/screen_capturer_null.cc \
	webrtc/modules/desktop_capture/window_capturer_null.cc

endif
endif

ifeq ($(AVS_FAMILY),x86)
ifneq ($(AVS_OS),ios)
MENG_SRCS += \
	webrtc/modules/desktop_capture/differ_block_sse2.cc

endif
endif

