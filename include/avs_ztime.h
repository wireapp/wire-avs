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

#ifndef _AVS_ZTIME_H_
#define _AVS_ZTIME_H_

#include <time.h>


struct ztime {
	time_t    sec;
	uint32_t msec;
};


int     ztime_decode(struct ztime *ztime, const char *str);
int     ztime_get(struct ztime *ztime);
int64_t ztime_diff(const struct ztime *za, const struct ztime *zb);

#endif
