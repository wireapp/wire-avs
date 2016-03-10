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
/*
 * RTP Header Extensions (RFC 5285)
 */

struct rtpext {
	struct rtpext_elem {
		unsigned id:4;   /* Identifier in the range 1-14 inclusive */
		size_t len;      /* Length of value, number of bytes */
		uint8_t val[16]; /* Element value (1-16 bytes) */
	} elemv[4];
	unsigned elemc;
};

int rtpext_decode(struct rtpext *ext, const uint8_t *buf, size_t sz);
struct rtpext_elem *rtpext_find(struct rtpext *ext, uint8_t id);
void rtpext_dump(const struct rtpext *ext);
