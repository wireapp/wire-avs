/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#define _POSIX_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>

#include <re/re.h>

#include "avs.h"

#include "mediamgr.h"


static void destructor(void *arg)
{
	struct sound *snd = arg;

	mem_deref((void *)snd->path);
	mem_deref((void *)snd->format);
}

int sound_alloc(struct sound **sndp,
		const char *path, const char *fmt,
		bool loop, bool mixing, bool incall, int intensity, bool is_call_media)
{
	struct sound *snd;
	int err = 0;
	
	snd = mem_zalloc(sizeof(*snd), destructor);
	if (!snd)
		return ENOMEM;
	
	str_dup((char **)&snd->path, path);
	str_dup((char **)&snd->format, fmt);
	snd->loop = loop;
	snd->mixing = mixing;
	snd->intensity = intensity;
	snd->is_call_media = is_call_media;

	if (err)
		mem_deref(snd);
	else
		*sndp = snd;

	return err;
}
