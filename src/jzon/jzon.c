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


struct json_object *jzon_alloc_object(void)
{
	return jzon_container_alloc(ODICT_OBJECT);
}


struct json_object *jzon_alloc_array(void)
{
	return jzon_container_alloc(ODICT_ARRAY);
}


const char *jzon_str(struct json_object *obj, const char *key)
{
	struct json_object *value;
	bool r;

	r = json_object_object_get_ex(obj, key, &value);
	if (!r)
		return NULL;

	return json_object_get_string(value);
}


int jzon_strdup(char **dst, struct json_object *obj, const char *key)
{
	struct json_object *value;
	enum odict_type type;

	if (!dst || !obj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(obj, key, &value))
		return ENOENT;
	type = json_object_get_type(value);
	if (type == json_type_string) {
		return str_dup(dst, json_object_get_string(value));
	}
	else if (type == json_type_null) {
		*dst = NULL;
		return 0;
	}
	else {
		return EPROTO;
	}
}


int jzon_strrepl(char **dst, struct json_object *obj, const char *key)
{
	char *tmp;
	int err;

	err = jzon_strdup(&tmp, obj, key);
	if (err)
		return err;
	mem_deref(*dst);
	*dst = tmp;
	return 0;
}


int jzon_strdup_opt(char **dst, struct json_object *obj, const char *key,
		    const char *dflt)
{
	int err;

	err = jzon_strdup(dst, obj, key);
	if (err == ENOENT) {
		if (dflt != NULL) {
			return str_dup(dst, dflt);
		}
		else {
			*dst = NULL;
			return 0;
		}
	}
	return err;
}


int jzon_int(int *dst, struct json_object *obj, const char *key)
{
	struct json_object *value;

	if (!dst || !obj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(obj, key, &value))
		return ENOENT;
	if (json_object_get_type(value) != json_type_int)
		return EPROTO;

	*dst = json_object_get_int(value);
	return 0;
}


int jzon_u32(uint32_t *dst, struct json_object *obj, const char *key)
{
	struct json_object *value;

	if (!dst || !obj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(obj, key, &value))
		return ENOENT;
	if (json_object_get_type(value) != json_type_int)
		return EPROTO;

	*dst = (uint32_t) json_object_get_int(value);
	return 0;
}


int jzon_int_opt(int *dst, struct json_object *obj, const char *key, int dflt)
{
	int err;

	err = jzon_int(dst, obj, key);
	if (err == ENOENT) {
		*dst = dflt;
		return 0;
	}
	return err;
}


int jzon_double(double *dst, struct json_object *obj, const char *key)
{
	struct json_object *value;
	enum odict_type type;

	if (!dst || !obj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(obj, key, &value))
		return ENOENT;
	type = json_object_get_type(value);
	if (type != json_type_double && type != json_type_int)
		return EPROTO;

	*dst = json_object_get_double(value);
	return 0;
}


int jzon_bool(bool *dst, struct json_object *obj, const char *key)
{
	struct json_object *value;

	if (!dst || !obj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(obj, key, &value))
		return ENOENT;
	if (json_object_get_type(value) != json_type_boolean)
		return EPROTO;

	*dst = json_object_get_boolean(value) ? true : false;
	return 0;
}


bool jzon_bool_opt(struct json_object *obj, const char *key, bool dflt)
{
	bool res;
	int err;

	err = jzon_bool(&res, obj, key);
	if (err)
		return dflt;
	else
		return res;
}


static int generic_object(struct json_object **dstp, struct json_object *jobj,
		const char *key, enum odict_type type)
{
	struct json_object *dst;

	if (!dstp || !jobj || !key)
		return EINVAL;

	if (!json_object_object_get_ex(jobj, key, &dst)) {
		return ENOENT;
	}
	if (!json_object_is_type(dst, type)) {
		return EPROTO;
	}
	*dstp = dst;
	return 0;
}


int jzon_object(struct json_object **dstp, struct json_object *jobj,
		const char *key)
{
	return generic_object(dstp, jobj, key, json_type_object);
}


int jzon_array(struct json_object **dstp, struct json_object *jobj,
		const char *key)
{
	return generic_object(dstp, jobj, key, json_type_array);
}


bool jzon_is_object(struct json_object *jobj)
{
	return json_type_object == json_object_get_type(jobj);
}


bool jzon_is_array(struct json_object *jobj)
{
	return json_type_array == json_object_get_type(jobj);
}


int jzon_is_null(struct json_object *jobj, const char *key)
{
	struct json_object *o;
	int err;

	err = generic_object(&o, jobj, key, json_type_null);

	return err;
}


int jzon_creatf(struct json_object **jobjp, const char *format, ...)
{
	va_list ap;
	int err;

	if (!jobjp || !format)
		return EINVAL;

	va_start(ap, format);
	err = jzon_vcreatf(jobjp, format, ap);
	va_end(ap);

	return err;
}


int jzon_vcreatf(struct json_object **jobjp, const char *format, va_list ap)
{
	struct json_object *jobj;
	const char *f;
	int err = 0;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	for (f = format; *f != '\0'; ++f) {
		const char *key = va_arg(ap, char *);

		if (!key) {
			err = EINVAL;
			goto out;
		}

		if (*f == 's') {
			const char *value = va_arg(ap, char *);

			if (!value)
				json_object_object_add(jobj, key, NULL);
			else
				json_object_object_add(jobj, key,
					json_object_new_string(value));
		}
		else if (*f == 'n') {
			json_object_object_add(jobj, key, NULL);
		}
		else if (*f == 'i') {
			json_object_object_add(jobj, key,
				json_object_new_int(
					(int32_t) va_arg(ap, int)));
		}
		else if (*f == 'f') {
			json_object_object_add(jobj, key,
				json_object_new_double(va_arg(ap, double)));
		}
		else if (*f == 'b') {
			bool bval = va_arg(ap, int); /* NOTE: using int */
			json_object_object_add(jobj, key,
					       json_object_new_boolean(bval));
		}
		else {
			err = EINVAL;
			goto out;
		}
	}

	*jobjp = jobj;

 out:
	if (err)
		mem_deref(jobj);

	return err;
}


int jzon_print(struct re_printf *pf, struct json_object *jobj)
{
	if (!jobj)
		return 0;

	if (!jzon_is_container(jobj))
		return EINVAL;

	/* NOTE: array will be printed with {} tokens */
	return re_hprintf(pf, "%H", json_encode_odict, jobj->entry.u.odict);
}


int jzon_encode(char **strp, struct json_object *jobj)
{
	if (!strp || !jobj)
		return EINVAL;

	return re_sdprintf(strp, "%H", jzon_print, jobj);
}


int jzon_decode(struct json_object **jobjp, const char *buf, size_t len)
{
	struct json_object *jobj = NULL;
	struct pl pl;
	enum odict_type type;
	int err = 0;

	if (!buf || !len)
		return EINVAL;

	if (0 != re_regex(buf, len, "[^ \t\r\n]1", &pl)) {
		warning("jzon_decode: first token not found\n");
		return EBADMSG;
	}

	switch (pl.p[0]) {

	case '{':
		type = ODICT_OBJECT;
		break;

	case '[':
		type = ODICT_ARRAY;
		break;

	default:
		warning("jzon: decode: invalid start-token (%r)\n", &pl);
		return EBADMSG;
	}

	jobj = jzon_container_alloc(type);
	if (!jobj)
		return ENOMEM;

	jobj->entry.u.odict = mem_deref(jobj->entry.u.odict);
	err = json_decode_odict(&jobj->entry.u.odict, 16, buf, len, 8);
	if (err)
		goto out;

	if (jobjp)
		*jobjp = jobj;

 out:
	if (err)
		mem_deref(jobj);

	return err;
}


struct json_object *jzon_apply(struct json_object *jobj,
			       jzon_apply_h *ah, void *arg)
{
	struct odict *odict;
	struct le *le;

	if (!jobj)
		return NULL;

	odict = jzon_odict(jobj);
	if (!odict)
		return NULL;

	le = list_head(&odict->lst);
	while (le) {
		struct json_object *robj = le->data;
		le = le->next;

		if (ah && ah(robj->entry.key, robj, arg))
			return robj;
	}

	return NULL;
}


int jzon_add_str(struct json_object *jobj, const char *key,
		 const char *fmt, ...)
{
	char *str = NULL;
	va_list ap;
	int err;

	if (!jobj || !key)
		return EINVAL;

	va_start(ap, fmt);
	err = re_vsdprintf(&str, fmt, ap);
	va_end(ap);

	if (err)
		return err;

	json_object_object_add(jobj, key, json_object_new_string(str));

	mem_deref(str);

	return 0;
}


int jzon_add_int(struct json_object *jobj, const char *key, int32_t val)
{
	if (!jobj || !key)
		return EINVAL;

	json_object_object_add(jobj, key, json_object_new_int(val));

	return 0;
}


int jzon_add_bool(struct json_object *jobj, const char *key, bool val)
{
	if (!jobj || !key)
		return EINVAL;

	json_object_object_add(jobj, key, json_object_new_boolean(val));

	return 0;
}


int jzon_add_base64(struct json_object *jobj, const char *key,
		   const uint8_t *buf, size_t len)
{
	char *b64;
	size_t b64_len;
	int err;

	if (!jobj || !key || !buf)
		return EINVAL;

	b64_len = 4 * ((len + 2)/3);
	b64     = mem_zalloc(b64_len + 1, NULL);
	if (!b64)
		return ENOMEM;

	err = base64_encode(buf, len, b64, &b64_len);
	if (err)
		goto out;
	b64[b64_len] = '\0';

	err  = jzon_add_str(jobj, key, b64);
	if (err)
		goto out;

 out:
	mem_deref(b64);

	return err;
}


void jzon_dump(struct json_object *jobj)
{
	bool cont;

	if (!jobj)
		return;

	cont = jzon_is_container(jobj);

	re_fprintf(stderr, "jzon_dump(%p|%s):\n", jobj,
		   cont ? "CONTAINER" : "ENTRY");

	if (cont) {
		re_fprintf(stderr, "%H\n", odict_debug, jzon_odict(jobj));
	}
	else {
		re_fprintf(stderr, "%H\n", odict_entry_debug, &jobj->entry);
	}
	re_fprintf(stderr, "\n");
}


struct odict *jzon_get_odict(struct json_object *jobj)
{
	if (!jobj)
		return NULL;

	return jobj->entry.u.odict;
}
