IOSX_LIB_SRCS += \
	flowmgr/AVSFlowManager.m \
	flowmgr/AVSCapturer.mm \
	flowmgr/AVSVideoPreview.m

IOSX_STUB_SRCS += \
	flowmgr/AVSFlowManagerStub.m

ifeq ($(AVS_OS),ios)
IOSX_LIB_SRCS += \
	flowmgr/AVSVideoView.m
endif
ifeq ($(AVS_OS),osx)
IOSX_LIB_SRCS += \
	flowmgr/AVSVideoViewOSX.m
endif

