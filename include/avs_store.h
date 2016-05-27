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
/* libavs -- simple object store
 */

struct store;
struct sobject;

/*** Stores ***/

int store_alloc(struct store **stp, const char *dir);
int store_set_user(struct store *st, const char *user_id);

/* Flush all information for currently set user.
 */
int store_flush_user(struct store *st);

/* Open a store object in the current user's space in *st*.
 *
 * The object type should be given in *type* and its identifier in *id*.
 * The mode for opening is given in *mode* and uses the same format as
 * fopen(3).
 */
int store_user_open(struct sobject **sop, struct store *st,
		    const char *type, const char *id, const char *mode);

/* Open a store object in the global space of *st*.
 *
 * The object's type and identifier are given through *type* and *id*,
 * respectively. The *mode* uses the same format as fopen(3).
 */
int store_global_open(struct sobject **sop, struct store *st,
		      const char *type, const char *id, const char *mode);


typedef int (store_apply_h)(const char *id, void *arg);

/* Apply *h* to all object identifiers for *type* in the user part of *st*.
 */
int store_user_dir(const struct store *st, const char *type,
		   store_apply_h *h, void *arg);

/* Apply *h* to all object identifiers for *type* in the global part of *st*.
 */
int store_global_dir(const struct store *st, const char *type,
		     store_apply_h *h, void *arg);

/** Delete a store object
 */
int store_user_unlink(struct store *st, const char *type, const char *id);
int store_global_unlink(struct store *st, const char *type, const char *id);


/*** Store Objects ***/

/* Close the store object.
 *
 * It cannot be used afterwards. Just mem_derefing the object also closes
 * the object, but closing may be delayed if there are pending references.
 */
void sobject_close(struct sobject *so);

int sobject_write(struct sobject *so, const uint8_t *buf, size_t size);
int sobject_write_u8(struct sobject *so, uint8_t v);
int sobject_write_u16(struct sobject *so, uint16_t v);
int sobject_write_u32(struct sobject *so, uint32_t v);
int sobject_write_u64(struct sobject *so, uint64_t v);
int sobject_write_dbl(struct sobject *so, double v);
int sobject_write_lenstr(struct sobject *so, const char *str);
int sobject_write_pl(struct sobject *so, const struct pl *pl);

int sobject_read(struct sobject *so, uint8_t *buf, size_t size);
int sobject_read_u8(uint8_t* v, struct sobject *so);
int sobject_read_u16(uint16_t* v, struct sobject *so);
int sobject_read_u32(uint32_t* v, struct sobject *so);
int sobject_read_u64(uint64_t* v, struct sobject *so);
int sobject_read_dbl(double* v, struct sobject *so);
int sobject_read_lenstr(char **strp, struct sobject *so);
int sobject_read_pl(struct pl *pl, struct sobject *so);


/* Store remove */

int store_mkdirf(mode_t mode, const char *fmt, ...);
int store_remove_pathf(const char *fmt, ...);
