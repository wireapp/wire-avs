
IOSX_LIB_SRCS += \
	mediamgr/AVSMediaManager.m \
	mediamgr/AVSMediaManager+Client.m \
	mediamgr/AVSSound.m

IOSX_STUB_SRCS += \
	mediamgr/AVSMediaManagerStub.m

#ifeq ($(BASE_TARGET),osx)
#IOSX_ANY_SRCS += mediamgr/AVSAudioRouter+OSX.m
#endif
#
#ifeq ($(BASE_TARGET),ios)
#IOSX_ANY_SRCS += mediamgr/AVSAudioRouter+iOS.m
#endif
