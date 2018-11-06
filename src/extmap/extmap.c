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

#include <re.h>
#include <avs.h>


struct extmap {
	struct dict *dict;
	int last_id;
};

static void destructor(void *arg)
{
	struct extmap *extmap = arg;

	dict_flush(extmap->dict);
	mem_deref(extmap->dict);
}

int extmap_alloc(struct extmap **extmapp)
{
	struct extmap *extmap;
	int err = 0;

	if (!extmapp) {
		return EINVAL;
	}

	extmap = mem_zalloc(sizeof(struct extmap), destructor);
	if (!extmap) {
		return ENOMEM;
	}

	err = dict_alloc(&extmap->dict);
	if (err) {
		goto out;
	}
	*extmapp = extmap;
out:
	return err;
}

int extmap_append(struct extmap *extmap, const char *key)
{
	int err =0;
	int maxlen;
	char *value;

	if (!extmap || !key) {
		return EINVAL;
	}
	extmap->last_id++;

	maxlen = strlen(key) + 16;
	value = mem_zalloc(maxlen, NULL);

	snprintf(value, maxlen - 1, "%d %s", extmap->last_id, key);
	err = dict_add(extmap->dict, key, value);

	mem_deref(value);
	return err;
}

int extmap_set(struct extmap *extmap, const char *value)
{
	int err =0;

	if (!extmap || !value) {
		return EINVAL;
	}

	const char *spc = strchr(value, ' ');
	if (!spc) {
		return EINVAL;
	}

	char *val;

	str_dup(&val, value);
	err = dict_add(extmap->dict, spc+1, val);
	if (err == EADDRINUSE) {
		err = 0;
	}
	else if (err) {
		goto out;
	}

	int idx = atoi(value);
	if (idx > extmap->last_id) {
		extmap->last_id = idx;
	}

out:
	mem_deref(val);
	return err;
}

const char *extmap_lookup(struct extmap *extmap, const char *key, bool append_if_missing)
{
	const char *value;

	value = dict_lookup(extmap->dict, key);
	if (value || !append_if_missing) {
		return value;
	}

	extmap_append(extmap, key);
	return dict_lookup(extmap->dict, key);
}

