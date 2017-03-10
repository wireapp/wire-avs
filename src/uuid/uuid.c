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
#include "avs_log.h"
#include "avs_uuid.h"


int uuid_v4(char **uuidp)
{
	char buf[64];
	char *uuid;
	uint64_t v1 = rand_u64();
	uint64_t v2 = rand_u64();
	int len;
	int i;
	int j;

	len = re_snprintf(buf, sizeof(buf), "%016llx%016llx", v1, v2);
	len += 6;

	uuid = mem_zalloc(len, NULL);
	if (uuid == NULL) {
		return ENOMEM;
	}

	j = 0;
	for (i = 0; i < len - 1; ++i) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			uuid[i] = '-';
		}
		else {
			uuid[i] = buf[j++];
		}
	}
	uuid[i] = '\0';

	if (uuidp) {
		*uuidp = uuid;
	}

	return 0;
}


/*
 * "9cba9672-834b-45ac-924d-1af7a6425e0d"
 */
bool uuid_isvalid(const char *uuid)
{
	if (str_len(uuid) != 36)
		return false;

	return uuid[8] == '-' &&
		uuid[13] == '-' &&
		uuid[18] == '-' &&
		uuid[23] == '-';
}
