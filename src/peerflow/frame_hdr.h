/*
* Wire
* Copyright (C) 2019 Wire Swiss GmbH
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


#ifndef FRAME_HDR_H_
#define FRAME_HDR_H_

#ifdef __cplusplus
extern "C" {
#endif

struct frame_hdr
{
	uint32_t magic;
	uint32_t key;
	uint32_t psize;
};

void frame_hdr_init(struct frame_hdr *hdr, uint32_t key, uint32_t psize);
bool frame_hdr_check_magic(struct frame_hdr *hdr);
uint32_t frame_hdr_get_key(struct frame_hdr *hdr);
uint32_t frame_hdr_get_psize(struct frame_hdr *hdr);

#ifdef __cplusplus
};
#endif

#endif  // FRAME_HDR_H_

