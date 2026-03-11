#
# srcs.mk All application source files.
#

TEST_SRCS	+= main.cpp

TEST_SRCS	+= util.cpp

# Add first in the list
TEST_SRCS	+= test_version.cpp

# Testcases in alphabetical order
TEST_SRCS	+= test_cert.cpp
TEST_SRCS	+= test_chunk.cpp
TEST_SRCS	+= test_confpos.cpp
TEST_SRCS	+= test_cookie.cpp
TEST_SRCS	+= test_dict.cpp
#TEST_SRCS	+= test_ecall.cpp
TEST_SRCS	+= test_econn.cpp
TEST_SRCS	+= test_econn_fmt.cpp
TEST_SRCS	+= test_engine.cpp
TEST_SRCS	+= test_frame_hdr.cpp
TEST_SRCS	+= test_http.cpp
TEST_SRCS	+= test_jzon.cpp
TEST_SRCS	+= test_keystore.cpp
TEST_SRCS	+= test_libre.cpp
TEST_SRCS	+= test_login.cpp
TEST_SRCS	+= test_msystem.cpp
TEST_SRCS	+= test_network.cpp
TEST_SRCS	+= test_nevent.cpp
TEST_SRCS	+= test_resampler.cpp
TEST_SRCS	+= test_rest.cpp
#TEST_SRCS	+= test_sdp.cpp
TEST_SRCS	+= test_string.cpp
TEST_SRCS	+= test_uuid.cpp
TEST_SRCS	+= test_userlist.cpp
#TEST_SRCS	+= test_wcall.cpp
TEST_SRCS	+= test_zapi.cpp
TEST_SRCS	+= test_ztime.cpp
TEST_SRCS	+= test_stats.cpp


TEST_SLOW_SRCS	+= main.cpp
TEST_SLOW_SRCS	+= util.cpp
TEST_SLOW_SRCS  += test_network_quality_handler.cpp

# Conditional tests
ifeq ($(AVS_OS),android)
TEST_SRCS	+= test_android.cpp
else
TEST_SRCS	+= test_mediamgr.cpp
endif

ifneq ($(HAVE_PROTOBUF),)
TEST_SRCS	+= test_protobuf.cpp
endif
ifneq ($(HAVE_CRYPTOBOX),)
TEST_SRCS	+= test_cryptobox.cpp
endif


# Fakes/mocks in alphabetical order
TEST_SRCS	+= fake_backend.cpp
TEST_SRCS	+= fake_cert.c
TEST_SRCS	+= fake_httpsrv.cpp
TEST_SRCS	+= fake_stunsrv.cpp
TEST_SRCS	+= nw_simulator.cpp
TEST_SRCS	+= turn/fake_turnsrv.cpp \
	turn/alloc.c \
	turn/chan.c \
	turn/perm.c \
	turn/turn.c \
	\
	turn/stun.c \
	turn/tcp.c

TEST_CPPFLAGS	+= -Itest
