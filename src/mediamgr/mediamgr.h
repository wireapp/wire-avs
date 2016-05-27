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

#ifndef MEDIAMGR_H
#define MEDIAMGR_H

#define MM_INTENSITY_THRES_ALL 100
#define MM_INTENSITY_THRES_SOME 50
#define MM_INTENSITY_THRES_NONE  0

struct sound {
	const char *path;
	const char *format;
	bool loop;
	bool mixing;	
	bool incall;
	int intensity;
	int priority;
	bool is_call_media;
	void *arg;

	struct le le; /* member of sounds list */
};

int sound_alloc(struct sound **sndp,
		const char *path, const char *fmt,
		bool loop, bool mixing, bool incall, int intensity, bool is_call_media);

const char *MMroute2Str(enum mediamgr_auplay route);

#endif

