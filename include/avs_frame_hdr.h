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

size_t frame_hdr_max_size(void);

size_t frame_hdr_write(uint8_t  *buf,
		       size_t   bsz,
		       uint64_t frame,
		       uint64_t key,
		       uint32_t csrc);

size_t frame_hdr_read(const uint8_t *buf,
		      size_t   bsz,
		      uint64_t *frame,
		      uint64_t *key,
		      uint32_t *csrc);

#endif  // FRAME_HDR_H_

