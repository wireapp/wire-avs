# mediaengine - Build system
#
# webrtc/base
#
# Note: This only contains what avs needs for linking. For reference, all
#       the other files are given as UNSRCS below so you can easily pull
#       some of them in should they become necessary.
#
MENG_CPPFLAGS += \
	-DHAVE_OPENSSL_SSL_H=1

MENG_SRCS += \
	webrtc/base/asyncfile.cc \
	webrtc/base/asyncresolverinterface.cc \
	webrtc/base/asyncsocket.cc \
	webrtc/base/bitbuffer.cc \
	webrtc/base/buffer.cc \
	webrtc/base/bufferqueue.cc \
	webrtc/base/bytebuffer.cc \
	webrtc/base/checks.cc \
	webrtc/base/common.cc \
	webrtc/base/criticalsection.cc \
	webrtc/base/event.cc \
	webrtc/base/event_tracer.cc \
	webrtc/base/exp_filter.cc \
	webrtc/base/ipaddress.cc \
	webrtc/base/logging.cc \
	webrtc/base/md5.cc \
	webrtc/base/md5digest.cc \
	webrtc/base/messagehandler.cc \
	webrtc/base/messagequeue.cc \
	webrtc/base/nethelpers.cc \
	webrtc/base/nullsocketserver.cc \
	webrtc/base/physicalsocketserver.cc \
	webrtc/base/platform_file.cc \
	webrtc/base/platform_thread.cc \
	webrtc/base/random.cc \
	webrtc/base/rate_statistics.cc \
	webrtc/base/ratetracker.cc \
	webrtc/base/safe_conversions.h \
	webrtc/base/scoped_autorelease_pool.h \
	webrtc/base/sharedexclusivelock.cc \
	webrtc/base/signalthread.cc \
	webrtc/base/sigslot.cc \
	webrtc/base/socketaddress.cc \
	webrtc/base/stringencode.cc \
	webrtc/base/stringutils.cc \
	webrtc/base/systeminfo.cc \
	webrtc/base/thread.cc \
	webrtc/base/thread_checker_impl.cc \
	webrtc/base/timeutils.cc

# Unused files -- these may require additional defines or libraries.
#

UNSRCS	+= \
	webrtc/base/asyncfile.cc \
	webrtc/base/asynchttprequest.cc \
	webrtc/base/asyncinvoker.cc \
	webrtc/base/asyncsocket.cc \
	webrtc/base/asynctcpsocket.cc \
	webrtc/base/asyncudpsocket.cc \
	webrtc/base/autodetectproxy.cc \
	webrtc/base/bandwidthsmoother.cc \
	webrtc/base/base64.cc \
	webrtc/base/bytebuffer.cc \
	webrtc/base/cpumonitor.cc \
	webrtc/base/crc32.cc \
	webrtc/base/diskcache.cc \
	webrtc/base/event.cc \
	webrtc/base/filelock.cc \
	webrtc/base/fileutils.cc \
	webrtc/base/firewallsocketserver.cc \
	webrtc/base/flags.cc \
	webrtc/base/helpers.cc \
	webrtc/base/httpbase.cc \
	webrtc/base/httpclient.cc \
	webrtc/base/httpcommon.cc \
	webrtc/base/httprequest.cc \
	webrtc/base/httpserver.cc \
	webrtc/base/ipaddress.cc \
	webrtc/base/json.cc \
	webrtc/base/latebindingsymboltable.cc \
	webrtc/base/logging.cc \
	webrtc/base/md5.cc \
	webrtc/base/messagedigest.cc \
	webrtc/base/messagehandler.cc \
	webrtc/base/messagequeue.cc \
	webrtc/base/multipart.cc \
	webrtc/base/natserver.cc \
	webrtc/base/natsocketfactory.cc \
	webrtc/base/nattypes.cc \
	webrtc/base/nethelpers.cc \
	webrtc/base/network.cc \
	webrtc/base/nssidentity.cc \
	webrtc/base/nssstreamadapter.cc \
	webrtc/base/optionsfile.cc \
	webrtc/base/pathutils.cc \
	webrtc/base/physicalsocketserver.cc \
	webrtc/base/posix.cc \
	webrtc/base/profiler.cc \
	webrtc/base/proxydetect.cc \
	webrtc/base/proxyinfo.cc \
	webrtc/base/proxyserver.cc \
	webrtc/base/ratelimiter.cc \
	webrtc/base/ratetracker.cc \
	webrtc/base/sha1.cc \
	webrtc/base/sharedexclusivelock.cc \
	webrtc/base/signalthread.cc \
	webrtc/base/socketadapters.cc \
	webrtc/base/socketaddress.cc \
	webrtc/base/socketaddresspair.cc \
	webrtc/base/socketpool.cc \
	webrtc/base/socketstream.cc \
	webrtc/base/ssladapter.cc \
	webrtc/base/sslfingerprint.cc \
	webrtc/base/sslidentity.cc \
	webrtc/base/sslsocketfactory.cc \
	webrtc/base/sslstreamadapter.cc \
	webrtc/base/sslstreamadapterhelper.cc \
	webrtc/base/stream.cc \
	webrtc/base/stringencode.cc \
	webrtc/base/stringutils.cc \
	webrtc/base/systeminfo.cc \
	webrtc/base/task.cc \
	webrtc/base/taskparent.cc \
	webrtc/base/taskrunner.cc \
	webrtc/base/testclient.cc \
	webrtc/base/thread.cc \
	webrtc/base/thread_checker_impl.cc \
	webrtc/base/timing.cc \
	webrtc/base/transformadapter.cc \
	webrtc/base/unixfilesystem.cc \
	webrtc/base/urlencode.cc \
	webrtc/base/versionparsing.cc \
	webrtc/base/virtualsocketserver.cc \
	webrtc/base/worker.cc

ifeq ($(AVS_OS),android)
UNSRCS	+= \
	webrtc/base/ifaddrs-android.cc \
	webrtc/base/linux.cc
endif
ifeq ($(AVS_OS),ios)
MENG_SRCS += \
	webrtc/base/scoped_autorelease_pool.mm \
	webrtc/base/maccocoathreadhelper.mm

UNSRCS	+= \
	webrtc/base/iosfilesystem.mm
endif
ifeq ($(AVS_OS),linux)
UNSRCS	+= \
	webrtc/base/dbus.cc \
	webrtc/base/libdbusglibsymboltable.cc \
	webrtc/base/linuxfdwalk.c \
	webrtc/base/linux.cc
endif
ifeq ($(AVS_OS),osx)
MENG_SRCS += \
	webrtc/base/logging_mac.mm

UNSRCS	+= \
	webrtc/base/macasyncsocket.cc \
	webrtc/base/maccocoasocketserver.mm \
	webrtc/base/maccocoathreadhelper.mm \
	webrtc/base/macconversion.cc \
	webrtc/base/macsocketserver.cc \
	webrtc/base/macutils.cc \
	webrtc/base/macwindowpicker.cc \
	webrtc/base/scoped_autorelease_pool.mm
endif
ifneq ($(USE_X11),)
UNSRCS	+= \
	webrtc/base/x11windowpicker.cc
endif
ifneq ($(AVS_OS),ios)
UNSRCS	+= \
	webrtc/base/openssladapter.cc \
	webrtc/base/openssldigest.cc \
	webrtc/base/opensslidentity.cc \
	webrtc/base/opensslstreamadapter.cc
endif
