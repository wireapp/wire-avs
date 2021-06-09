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


#define json_type_object  ODICT_OBJECT
#define json_type_array   ODICT_ARRAY
#define json_type_string  ODICT_STRING
#define json_type_int     ODICT_INT
#define json_type_double  ODICT_DOUBLE
#define json_type_boolean ODICT_BOOL
#define json_type_null    ODICT_NULL


/* This struct must be kept opaque (hidden) on purpose.
 */
struct json_object {
	/* base class -- can be casted to this type */
	struct odict_entry entry;

	/* Dont add more members here */
};


bool                jzon_is_container(const struct json_object *obj);
struct odict       *jzon_odict(const struct json_object *obj);
struct json_object *jzon_container_alloc(enum odict_type type);

enum odict_type json_object_get_type(struct json_object *obj);
int             json_object_is_type(struct json_object *obj,
				    enum odict_type type);


