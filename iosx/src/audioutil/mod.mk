ifeq ($(AVS_OS),ios)
IOSX_LIB_SRCS += audioutil/AVSAudioEffect.m
IOSX_STUB_SRCS += audioutil/AVSAudioEffectStub.m
endif
ifeq ($(AVS_OS),iossim)
IOSX_LIB_SRCS += audioutil/AVSAudioEffect.m
IOSX_STUB_SRCS += audioutil/AVSAudioEffectStub.m
endif
