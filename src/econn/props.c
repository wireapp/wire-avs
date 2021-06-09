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
#include "avs_log.h"
#include "avs_zapi.h"
#include "avs_econn.h"
#include "econn.h"


static void props_destructor(void *data)
{
	struct econn_props *props = data;

	mem_deref(props->dict);
}


int econn_props_alloc(struct econn_props **epp, struct odict *dict)
{
	struct econn_props *props;
	int err = 0;

	props = mem_zalloc(sizeof(*props), props_destructor);
	if (!props)
		return ENOMEM;

	if (dict)
		props->dict = mem_ref(dict);
	else {
		err = odict_alloc(&props->dict, 32);
		if (err)
			goto out;
	}

 out:
	if (err)
		mem_deref(props);
	else
		*epp = props;

	return err;
}


int econn_props_add(struct econn_props *props, const char *key,
		    const char *val)
{
	if (!props || !key || !val)
		return EINVAL;

	return odict_entry_add(props->dict, key, ODICT_STRING, val);
}


int econn_props_update(struct econn_props *props, const char *key,
		       const char *val)
{
	struct odict_entry *de;
	int err;

	de = (struct odict_entry *)odict_lookup(props->dict, key);
	if (de) {
		mem_deref(de->u.str);
		err = str_dup(&de->u.str, val);
	}
	else {
		err = econn_props_add(props, key, val);
	}

	return err;
}


const char *econn_props_get(const struct econn_props *props, const char *key)
{
	const struct odict_entry *de;

	if (!props || !str_isset(key))
		return NULL;

	de = odict_lookup(props->dict, key);
	if (!de)
		return NULL;

	return de->u.str;
}


int econn_props_print(struct re_printf *pf, const struct econn_props *props)
{
	int err = 0;

	if (!props)
		return 0;

	err = odict_debug(pf, props->dict);

	return err;
}
