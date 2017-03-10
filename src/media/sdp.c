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

#include <re.h>
#include "priv_mediaflow.h"


/* RFC 4572 */
int sdp_fingerprint_decode(const char *attr, struct pl *hash,
			   uint8_t *md, size_t *sz)
{
	struct pl f;
	const char *p;
	int err;

	if (!attr || !hash)
		return EINVAL;

	err = re_regex(attr, str_len(attr), "[^ ]+ [0-9A-F:]+", hash, &f);
	if (err)
		return err;

	if (md && sz) {
		if (*sz < (f.l+1)/3)
			return EOVERFLOW;

		for (p = f.p; p < (f.p+f.l); p += 3) {
			*md++ = ch_hex(p[0]) << 4 | ch_hex(p[1]);
		}

		*sz = (f.l+1)/3;
	}

	return 0;
}
