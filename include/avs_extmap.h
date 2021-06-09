/*
* Wire
* Copyright (C) 2018 Wire Swiss GmbH
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
/* libavs
 *
 * Extmap
 */

struct extmap;

struct videnc_fmt_data {
	struct sdp_format *ref_fmt;
	struct extmap *extmap;
};

int extmap_alloc(struct extmap **extmapp);
int extmap_set(struct extmap *extmap, const char *value);
int extmap_append(struct extmap *extmap, const char *value);
const char *extmap_lookup(struct extmap *extmap, const char *key, bool append_if_missing);

