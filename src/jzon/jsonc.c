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
#include "avs_jzon.h"
#include "priv_jzon.h"


enum {
	HASH_SIZE = 16
};


static void destructor(void *data)
{
	struct json_object *json = data;

	mem_deref(json->entry.key);

	switch (json->entry.type) {

	case ODICT_OBJECT:
	case ODICT_ARRAY:
		mem_deref(json->entry.u.odict);
		break;

	case ODICT_STRING:
		mem_deref(json->entry.u.str);
		break;

	default:
		break;
	}
}


struct odict *jzon_odict(const struct json_object *jobj)
{
	if (!jzon_is_container(jobj)) {
		warning("jzon: get_odict: not a container\n");
		return NULL;
	}
	return jobj->entry.u.odict;
}


bool jzon_is_container(const struct json_object *obj)
{
	if (!obj)
		return false;

	return odict_type_iscontainer(obj->entry.type);
}


struct json_object *jzon_container_alloc(enum odict_type type)
{
	struct json_object *jobj;
	int err;

	jobj = mem_zalloc(sizeof(*jobj), destructor);
	if (!jobj)
		return NULL;

	jobj->entry.type = type;

	err = odict_alloc(&jobj->entry.u.odict, HASH_SIZE);
	if (err)
		jobj = mem_deref(jobj);

	return jobj;
}


static struct json_object *entry_alloc(enum odict_type type)
{
	struct json_object *jobj;

	jobj = mem_zalloc(sizeof(*jobj), destructor);
	if (!jobj)
		return NULL;

	jobj->entry.type = type;

	return jobj;
}


struct json_object *json_object_new_object(void)
{
	return jzon_container_alloc(ODICT_OBJECT);
}


struct json_object *json_object_new_array(void)
{
	return jzon_container_alloc(ODICT_ARRAY);
}


struct json_object *json_object_new_string(const char *str)
{
	struct json_object *jobj;
	int err = 0;

	if (!str)
		return NULL;

	jobj = entry_alloc(ODICT_STRING);
	if (!jobj)
		return NULL;

	err = str_dup(&jobj->entry.u.str, str);
	if (err)
		goto out;

 out:
	if (err)
		jobj = mem_deref(jobj);

	return jobj;
}


struct json_object *json_object_new_int(int32_t i)
{
	struct json_object *jobj;

	jobj = entry_alloc(ODICT_INT);
	if (!jobj)
		return NULL;

	jobj->entry.u.integer = i;

	return jobj;
}


struct json_object *json_object_new_double(double d)
{
	struct json_object *o;

	o = entry_alloc(ODICT_DOUBLE);
	if (o)
		o->entry.u.dbl = d;

	return o;
}


struct json_object *json_object_new_boolean(bool b)
{
	struct json_object *o;

	o = entry_alloc(ODICT_BOOL);
	if (o)
		o->entry.u.boolean = b;

	return o;
}


static int add_entry(struct json_object *jobj, const char *key,
		     struct json_object *val)
{
	struct odict *odict;
	struct odict_entry *e;
	int err = 0;

	if (!jobj || !val)
		return EINVAL;

	odict = jzon_odict(jobj);
	if (!jobj)
		return EINVAL;
	e = &val->entry;

	switch (e->type) {

	case ODICT_OBJECT:
	case ODICT_ARRAY:
		err = odict_entry_add(odict, key, e->type, e->u.odict);
		if (err)
			return err;
		e->u.odict = mem_deref(e->u.odict);
		break;

	case ODICT_STRING:
		err = odict_entry_add(odict, key, e->type, e->u.str);
		if (err)
			return err;
		e->u.str = mem_deref(e->u.str);
		break;

	case ODICT_INT:
		err = odict_entry_add(odict, key, e->type, e->u.integer);
		break;

	case ODICT_DOUBLE:
		err = odict_entry_add(odict, key, e->type, e->u.dbl);
		break;

	case ODICT_BOOL:
		err = odict_entry_add(odict, key, e->type, (int)e->u.boolean);
		break;

	case ODICT_NULL:
		err = odict_entry_add(odict, key, ODICT_NULL);
		break;

	default:
		warning("jzon: entry_add: invalid entry type %d\n", e->type);
		return EINVAL;
	}

	return err;
}


/* note: "val" is optional */
void json_object_object_add(struct json_object *jobj, const char *key,
			    struct json_object *val)
{
	int err = 0;

	if (!jobj || !key)
		return;

	if (!jzon_is_container(jobj)) {
		warning("jzon: object_add: expected container-type,"
			" but obj is of type '%s' (key=%s)\n",
			odict_type_name(jobj->entry.type), key);
		return;
	}

	if (val) {
		err = add_entry(jobj, key, val);
	}
	else {
		err = odict_entry_add(jobj->entry.u.odict, key, ODICT_NULL);
	}
	if (err) {
		warning("jzon: json_object_object_add('%s',%p) failed (%m)\n",
			key, val, err);
		return;
	}

#if 1
	/* NOTE: ownership transferred to obj */
	val = mem_deref(val);
#endif
}


/* return 0 for success, -1 for errors */
int json_object_array_add(struct json_object *jobj, struct json_object *val)
{
	char key[16];
	int err;

	if (!jobj)
		return -1;

	if (jobj->entry.type != ODICT_ARRAY) {
		warning("jzon: array_add: not an array\n");
		return -1;
	}

	if (re_snprintf(key, sizeof(key), "%zu",
			odict_count(jzon_odict(jobj), false)) < 0)
		return -1;

	if (val) {
		err = add_entry(jobj, key, val);
	}
	else {
		err = odict_entry_add(jobj->entry.u.odict, key, ODICT_NULL);
	}

	if (err) {
		warning("jzon: array_add: add_entry failed (%m)\n", err);
		return -1;
	}

#if 1
	/* NOTE: ownership transferred to the container */
	mem_deref(val);
#endif

	return 0;
}


int json_object_array_length(struct json_object *jobj)
{
	size_t n;

	if (!jobj)
		return 0;

	if (jobj->entry.type != ODICT_ARRAY || !jobj->entry.u.odict) {
		warning("jzon: array_length: not an array\n");
		return 0;
	}

	n = odict_count(jobj->entry.u.odict, false);

	return (int)n;
}


struct json_object *json_object_array_get_idx(struct json_object *obj, int idx)
{
	char key[16];

	if (!obj || idx<0)
		return NULL;

	if (obj->entry.type != ODICT_ARRAY || !obj->entry.u.odict) {
		warning("jzon: array_get_idx: not an array\n");
		return 0;
	}

	if (re_snprintf(key, sizeof(key), "%d", idx) < 0)
		return NULL;

	return (struct json_object *)odict_lookup(jzon_odict(obj), key);
}


bool json_object_object_get_ex(struct json_object *obj, const char *key,
			       struct json_object **value)
{
	const struct odict_entry *entry;

	if (!obj || !key)
		return false;

	if (obj->entry.type != ODICT_OBJECT) {
		warning("jzon: object_get_ex: not an object\n");
		return false;
	}

	entry = odict_lookup(jzon_odict(obj), key);
	if (!entry)
		return false;

	if (value)
		*value = (struct json_object *)entry;

	return true;
}


const char *json_object_get_string(struct json_object *jobj)
{
	if (!jobj)
		return NULL;

	switch (jobj->entry.type) {

	case ODICT_STRING:
		return jobj->entry.u.str;

	case ODICT_NULL:
		return NULL;

	default:
		warning("jzon: get_string: not a string\n");
		break;
	}

	return NULL;
}


enum odict_type json_object_get_type(struct json_object *obj)
{
	if (!obj)
		return (enum odict_type)-1;

	return obj->entry.type;
}


int32_t json_object_get_int(struct json_object *obj)
{
	if (!obj)
		return 0;

	if (obj->entry.type != ODICT_INT) {
		warning("jzon: get_int: not an int\n");
		return 0;
	}

	return (int32_t)obj->entry.u.integer;
}


double json_object_get_double(struct json_object *obj)
{
	if (!obj)
		return .0;

	if (obj->entry.type != ODICT_DOUBLE) {
		warning("jzon: get_double: not a double\n");
		return .0;
	}

	return obj->entry.u.dbl;
}


bool json_object_get_boolean(struct json_object *obj)
{
	if (!obj)
		return false;

	if (obj->entry.type != ODICT_BOOL) {
		warning("jzon: get_bool: not a bool\n");
		return .0;
	}

	return obj->entry.u.boolean;
}


int json_object_is_type(struct json_object *obj, enum odict_type type)
{
	if (!obj)
		return 0;

	return type == obj->entry.type;
}
