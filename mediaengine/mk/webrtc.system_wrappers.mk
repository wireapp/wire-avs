
MENG_CPPFLAGS += \
	-Iwebrtc/system_wrappers/interface

MENG_CPPFLAGS_webrtc/system_wrappers/source/ += \
	-Iwebrtc/system_wrappers/source/spreadsortlib

MENG_SRCS += \
	webrtc/system_wrappers/source/aligned_malloc.cc \
	webrtc/system_wrappers/source/clock.cc \
	webrtc/system_wrappers/source/cpu_info.cc \
	webrtc/system_wrappers/source/cpu_features.cc \
	webrtc/system_wrappers/source/data_log_c.cc \
	webrtc/system_wrappers/source/event.cc \
    webrtc/system_wrappers/source/event_timer_posix.cc \
	webrtc/system_wrappers/source/file_impl.cc \
	webrtc/system_wrappers/source/field_trial_default.cc \
	webrtc/system_wrappers/source/logging.cc \
	webrtc/system_wrappers/source/metrics_default.cc \
	webrtc/system_wrappers/source/rtp_to_ntp.cc \
	webrtc/system_wrappers/source/rw_lock.cc \
	webrtc/system_wrappers/source/rw_lock_posix.cc \
	webrtc/system_wrappers/source/sleep.cc \
	webrtc/system_wrappers/source/sort.cc \
	webrtc/system_wrappers/source/timestamp_extrapolator.cc \
	webrtc/system_wrappers/source/trace_impl.cc \
	webrtc/system_wrappers/source/trace_posix.cc

ifneq ($(ENABLE_DATA_LOGGING),)
MENG_SRCS += \
	webrtc/system_wrappers/source/data_log.cc

else
MENG_SRCS += \
	webrtc/system_wrappers/source/data_log_no_op.cc

endif

ifeq ($(AVS_OS),android)
MENG_CPPFLAGS += \
	-DWEBRTC_THREAD_RR -DWEBRTC_CLOCK_TYPE_REALTIME

MENG_SRCS += \
	webrtc/system_wrappers/source/logcat_trace_context.cc \
	webrtc/system_wrappers/source/atomic32_posix.cc

else ifeq ($(AVS_OS),linux)
MENG_CPPFLAGS += \
	-DWEBRTC_THREAD_RR

MENG_SRCS += \
	webrtc/system_wrappers/source/atomic32_posix.cc

else ifeq ($(AVS_OS),osx)
MENG_CPPFLAGS += \
	-DWEBRTC_THREAD_RR -DWEBRTC_CLOCK_TYPE_REALTIME

MENG_SRCS += \
	webrtc/system_wrappers/source/atomic32_mac.cc


else ifeq ($(AVS_OS),ios)
MENG_CPPFLAGS += \
	-DWEBRTC_THREAD_RR -DWEBRTC_CLOCK_TYPE_REALTIME

MENG_SRCS += \
	webrtc/system_wrappers/source/atomic32_mac.cc


endif
