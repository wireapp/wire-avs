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

#ifndef AVS_BASE_H
#define AVS_BASE_H    1

enum {
	AVS_FLAG_EXPERIMENTAL   = 1<<0,
	AVS_FLAG_AUDIO_TEST     = 1<<1,
	AVS_FLAG_VIDEO_TEST     = 1<<2
};


int  avs_init(uint64_t flags);
int  avs_start(const char *token);
void avs_close(void);
uint64_t  avs_get_flags(void);
const char *avs_get_token(void);

#endif //#ifndef AVS_BASE_H
