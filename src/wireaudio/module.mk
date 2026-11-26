#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= wireaudio
$(MOD)_SRCS	+= wireaudio.c
$(MOD)_SRCS	+= resamp.c

include mk/mod.mk
