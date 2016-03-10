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

#include <string.h>
#include <re.h>
#include "avs_rtpext.h"


/* note: assumes that "ext" is uninitialized */
int rtpext_decode(struct rtpext *ext, const uint8_t *buf, size_t sz)
{
	const uint8_t *p = buf;
	const uint8_t *pmax = buf + sz;
	unsigned n = 0;

	if (!ext || !buf || !sz)
		return EINVAL;

	if (sz < 4)
		return EBADMSG;

	if (p[0] != 0xbe || p[1] != 0xde)
		return EPROTO;

	p += 4;

	while (p < pmax) {
		uint8_t id;
		uint8_t len;
		uint8_t b = *p++;

		id  = (b >> 4) & 0x0f;
		len = (b & 0x0f) + 1;

		if (id == 0)
			continue;
		else if (id == 15)
			break;

		ext->elemv[n].id = id;
		ext->elemv[n].len = len;
		memcpy(ext->elemv[n].val, p, len);

		p += len;

		++n;

		if (n > ARRAY_SIZE(ext->elemv))
			return ENOMEM;
	}

	ext->elemc = n;

	return 0;
}


struct rtpext_elem *rtpext_find(struct rtpext *ext, uint8_t id)
{
	if (!ext)
		return NULL;

	for (unsigned i=0; i<ext->elemc; i++) {

		struct rtpext_elem *elem = &ext->elemv[i];

		if (elem->id == id)
			return elem;
	}

	return NULL;
}


void rtpext_dump(const struct rtpext *ext)
{
	if (!ext)
		return;

	re_printf("number of elements:  %zu\n", ext->elemc);

	for (size_t i=0; i<ext->elemc; i++) {

		re_printf("id=%u, %zu bytes [%w]\n",
			  ext->elemv[i].id,
			  ext->elemv[i].len,
			  ext->elemv[i].val,
			  ext->elemv[i].len);
	}
}
