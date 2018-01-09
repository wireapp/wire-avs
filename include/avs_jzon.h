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
/* libavs
 *
 * JSON helpers
 */


/* must be kept opaque (hidden) */
struct json_object;

typedef bool (jzon_apply_h)(const char *key, struct json_object *jobj,
			    void *arg);

struct json_object *jzon_alloc_object(void);
struct json_object *jzon_alloc_array(void);

const char *jzon_str(struct json_object *obj, const char *key);

int jzon_strdup(char **dst, struct json_object *obj, const char *key);
int jzon_strrepl(char **dst, struct json_object *obj, const char *key);
int jzon_int(int *dst, struct json_object *obj, const char *key);
int jzon_u32(uint32_t *dst, struct json_object *obj, const char *key);
int jzon_double(double *dst, struct json_object *obj, const char *key);
int jzon_bool(bool *dst, struct json_object *obj, const char *key);
bool jzon_is_object(struct json_object *jobj);
bool jzon_is_array(struct json_object *jobj);
int  jzon_is_null(struct json_object *jobj, const char *key);

int jzon_strdup_opt(char **dst, struct json_object *obj, const char *key,
		    const char *dflt);
int jzon_int_opt(int *dst, struct json_object *obj, const char *key,
		 int dflt);
bool jzon_bool_opt(struct json_object *obj, const char *key, bool dflt);

int jzon_object(struct json_object **dstp, struct json_object *jobj,
		const char *key);
int jzon_array(struct json_object **dstp, struct json_object *jobj,
		const char *key);

int jzon_creatf(struct json_object **jobjp, const char *format, ...);
int jzon_vcreatf(struct json_object **jobjp, const char *format, va_list ap);


int jzon_print(struct re_printf *pf, struct json_object *jobj);
int jzon_encode_odict_pretty(struct re_printf *pf, const struct odict *o);
int jzon_encode(char **strp, struct json_object *jobj);
int jzon_decode(struct json_object **jobjp, const char *buf, size_t len);
struct json_object *jzon_apply(struct json_object *jobj,
			       jzon_apply_h *ah, void *arg);

int jzon_add_str(struct json_object *jobj, const char *key,
		 const char *fmt, ...);
int jzon_add_int(struct json_object *jobj, const char *key, int32_t val);
int jzon_add_bool(struct json_object *jobj, const char *key, bool val);
int jzon_add_base64(struct json_object *jobj, const char *key,
		    const uint8_t *buf, size_t len);

void jzon_dump(struct json_object *jobj);
struct odict *jzon_get_odict(struct json_object *jobj);


/*
 * emulation of JSON-C api
 */
struct json_object *json_object_new_object(void);
struct json_object *json_object_new_array(void);
struct json_object *json_object_new_string(const char *s);
struct json_object* json_object_new_int(int32_t i);
struct json_object* json_object_new_double(double d);
struct json_object *json_object_new_boolean(bool b);

void json_object_object_add(struct json_object *obj, const char *key,
			    struct json_object *val);

int  json_object_array_add(struct json_object *obj, struct json_object *val);
int  json_object_array_length(struct json_object *obj);
struct json_object *json_object_array_get_idx(struct json_object *obj,
					      int idx);

bool json_object_object_get_ex(struct json_object *obj, const char *key,
			       struct json_object **value);
const char * json_object_get_string(struct json_object *obj);
int32_t      json_object_get_int(struct json_object *obj);
double       json_object_get_double(struct json_object *obj);
bool         json_object_get_boolean(struct json_object *obj);
