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
#include "avs_dict.h"


struct dict {
	struct hash *hmap;
	bool flushing;      /* XXX: workaround to avoid double-free */
};

struct dict_entry {
	struct le le;
	char *key;
	void *value;
};


static bool hmap_handler(struct le *le, void *arg)
{
	struct dict_entry *entry = le->data;
	const char *key = arg;

	return entry != NULL && str_cmp(entry->key, key) == 0;
}


static void destructor(void *arg)
{
	struct dict *dict = arg;

	dict_flush(dict);
	mem_deref(dict->hmap);
}


static void entry_destructor(void *arg)
{
	struct dict_entry *e = arg;

	hash_unlink(&e->le);
	mem_deref(e->key);

	// todo: should not be called if called from destr.
	if (mem_nrefs(e->value) > 0)
		mem_deref(e->value);
}


static struct dict_entry *entry_lookup(const struct dict *dict,
				       const char *key)
{
	struct le *le;
	uint32_t hkey;

	if (!dict || !key) {
		return NULL;
	}

	hkey = hash_joaat_str_ci(key);

	le = hash_lookup(dict->hmap, hkey, hmap_handler, (void *)key);
	if (le) {
		return (struct dict_entry *)le->data;
	}
	else {
		return NULL;
	}
}


int dict_alloc(struct dict **dictp)
{
	struct dict *dict = mem_zalloc(sizeof(*dict), destructor);
	int err;

	if (dict == NULL) {
		return ENOMEM;
	}

	err = hash_alloc(&dict->hmap, 32);
	if (err) {
		goto out;
	}

out:
	if (err) {
		mem_deref(dict);
	}
	else {
		*dictp = dict;
	}

	return err;
}


void *dict_lookup(const struct dict *dict, const char *key)
{
	struct dict_entry *entry;

	if (!dict || !key) {
		return NULL;
	}

	entry = entry_lookup(dict, key);

	return entry ? entry->value : NULL;
}


int dict_add(struct dict *dict, const char *key, void *val)
{
	struct dict_entry *entry;
	uint32_t hkey;

	if (!dict || !key) {
		return EINVAL;
	}

	entry = entry_lookup(dict, key);
	if (entry) {
		return EADDRINUSE;
	}

	hkey = hash_joaat_str_ci(key);

	entry = mem_zalloc(sizeof(*entry), entry_destructor);
	if (entry == NULL) {
		return ENOMEM;
	}

	str_dup(&entry->key, key);
	entry->value = mem_ref(val);

	hash_append(dict->hmap, hkey, &entry->le, entry);

	return 0;
}


void dict_remove(struct dict *dict, const char *key)
{
	struct dict_entry *entry;

	if (!dict)
		return;

	/* entry is already being flushed, no need to remote it */
	if (dict->flushing) {
		return;
	}

	entry = entry_lookup(dict, key);
	if (entry) {
		mem_deref(entry);
	}
}


struct dict_apply_data {
	dict_apply_h *h;
	void *arg;
};


static bool hash_apply_handler(struct le *le, void *arg)
{
	struct dict_apply_data *data = arg;
	struct dict_entry *entry = le->data;

	return data->h(entry->key, entry->value, data->arg);
}


/* Returns the value of the entry where traversing stopped or NULL.
 */
void *dict_apply(const struct dict *dict, dict_apply_h *h, void *arg)
{
	struct dict_apply_data data;
	struct le *le;

	if (!dict || !h)
		return NULL;

	data.h = h;
	data.arg = arg;

	le = hash_apply(dict->hmap, hash_apply_handler, &data);
	if (le)
		return ((struct dict_entry *) le->data)->value;
	else
		return NULL;
}


void dict_flush(struct dict *dict)
{
	if (!dict)
		return;

	dict->flushing = true;
	hash_flush(dict->hmap);
	dict->flushing = false;
}


uint32_t dict_count(const struct dict *dict)
{
	uint32_t i, count = 0;

	if (!dict)
		return 0;

	for (i=0; i<hash_bsize(dict->hmap); i++) {
		count += list_count(hash_list(dict->hmap, i));
	}

	return count;
}


void dict_dump(const struct dict *dict)
{
	uint32_t i;

	if (!dict)
		return;

	re_printf("dictionary at %p:\n", dict);

	for (i=0; i<hash_bsize(dict->hmap); i++) {
		struct list *lst = hash_list(dict->hmap, i);

		re_printf("%02u:  %u entries\n", i, list_count(lst));
	}
}
