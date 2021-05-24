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


#include <re.h>
#include <avs.h>

#define MAX_EXTS 16
/*

Header

                     1 1 1 1 1 1
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| V=0 |E|  RES  |S|LEN  |1|KLEN |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     KID...  (length=KLEN)     |
+-------------------------------+
|      CTR... (length=LEN)      |
+-------------------------------+

Extensions

 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+---------------------------+
|M| ELN |  EID  |   VAL... (length=ELEN)    |
+-+-+-+-+-+-+-+-+---------------------------+
*/

#define HDR_VERSION 0

size_t frame_hdr_max_size(void)
{
	return 18 + 9 * 8;
}

static uint8_t calc_len(uint32_t val) {
	uint8_t l = 0;

	if (val == 0) {
		return 1;
	}

	while (val != 0) {
		l++;
		val >>= 8;
	}
	return l;
}

static size_t write_bytes(uint8_t *buf,
			  uint8_t len,
			  uint64_t val)
{
	switch (len) {
	case 8:
		*buf = (val >> 56) & 0xff;
		buf++;
	case 7:
		*buf = (val >> 48) & 0xff;
		buf++;
	case 6:
		*buf = (val >> 40) & 0xff;
		buf++;
	case 5:
		*buf = (val >> 32) & 0xff;
		buf++;
	case 4:
		*buf = (val >> 24) & 0xff;
		buf++;
	case 3:
		*buf = (val >> 16) & 0xff;
		buf++;
	case 2:
		*buf = (val >> 8) & 0xff;
		buf++;
	case 1:
		*buf = val & 0xff;
		break;
	default:
		return 0;
	}
	return len;
}

static size_t read_bytes(const uint8_t *buf,
			 uint8_t len,
			 uint64_t *val)
{
	uint8_t l = len;
	uint64_t v = 0;
	while (l > 0) {
		v <<= 8;
		v |= *buf;
		buf++;
		l--;
	}
	*val = v;
	return len;
}

size_t frame_hdr_write(uint8_t  *buf,
		       size_t   bsz,
		       uint64_t frame,
		       uint64_t key,
		       uint32_t csrc)
{
	uint8_t sig = 0;
	uint8_t x = 0;
	uint8_t klen, flen;
	size_t p = 2;
	uint8_t ext = csrc ? 1 : 0;
	size_t xsz = csrc ? 5 : 0;
	size_t hsz, ksz;

	if (key > 7) {
		x = 1;
		ksz = calc_len(key);
		klen = ksz - 1;
	}
	else {
		ksz = 0;
		klen = key;
	}

	flen = calc_len(frame);

	hsz = 2 + ksz + flen + xsz;
	if (bsz < hsz)
		return 0;

	buf[0] = HDR_VERSION << 5 | ext << 4;
	buf[1] = sig << 7 | (flen-1) << 4 | x << 3 | klen;
	if (x) {
		p += write_bytes(buf + p, klen+1, key);
	}
	p += write_bytes(buf + p, flen, frame);

	if (csrc) {
		buf[p] = 0x31;
		p++;
		write_bytes(buf + p, 4, csrc);
		p += 4;
	}

	return p;
}


size_t frame_hdr_read(const uint8_t *buf,
		      size_t   bsz,
		      uint64_t *frameid,
		      uint64_t *key,
		      uint32_t *csrc)
{
	uint8_t x = 0;
	uint8_t klen, flen, elen;
	size_t p = 2;
	uint8_t ext, b, eid;
	uint64_t csrc64;
	uint32_t i = 0;

	ext = buf[0] & 0x10;
	flen = ((buf[1] >> 4) & 7) + 1;
	x = (buf[1] >> 3) & 1;
	klen = buf[1] & 7;

	if (x) {
		p += read_bytes(buf + p, klen+1, key);
	}
	else {
		*key = klen;
	}

	p += read_bytes(buf + p, flen, frameid);

	while(ext && p < bsz && i < MAX_EXTS) {
		b = *(buf + p);
		ext = b & 0x80;
		elen = ((b >> 4) & 7) + 1;
		if (p + elen + 1 >= bsz)
			break;
		eid = b &  0x0f;

		if (eid == 0x01 && elen == 4) {
			read_bytes(buf + p + 1, 4, &csrc64);
			*csrc = csrc64;
		}
		p += elen + 1;
		i++;
	}

	return p;
}

