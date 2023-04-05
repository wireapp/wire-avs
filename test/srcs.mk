#
# srcs.mk All application source files.
#

TEST_SRCS	+= main.cpp

TEST_SRCS	+= util.cpp

# Add first in the list
TEST_SRCS	+= test_version.cpp

# Testcases in alphabetical order
TEST_SRCS	+= test_cert.cpp
TEST_SRCS	+= test_confpos.cpp
TEST_SRCS	+= test_dict.cpp
#TEST_SRCS	+= test_ecall.cpp
TEST_SRCS	+= test_econn.cpp
TEST_SRCS	+= test_frame_hdr.cpp
TEST_SRCS	+= test_jzon.cpp
TEST_SRCS	+= test_keystore.cpp
TEST_SRCS	+= test_libre.cpp
TEST_SRCS	+= test_msystem.cpp
TEST_SRCS	+= test_network.cpp
TEST_SRCS	+= test_resampler.cpp
TEST_SRCS	+= test_sdp.cpp
TEST_SRCS	+= test_string.cpp
TEST_SRCS	+= test_uuid.cpp
TEST_SRCS	+= test_userlist.cpp
#TEST_SRCS	+= test_wcall.cpp
TEST_SRCS	+= test_zapi.cpp
TEST_SRCS	+= test_ztime.cpp

# Conditional tests
ifeq ($(AVS_OS),android)
TEST_SRCS	+= test_android.cpp
else
TEST_SRCS	+= test_mediamgr.cpp
endif

TEST_CPPFLAGS	+= -Itest
