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
 * Serial buffer
 */

struct serial;

enum serial_type {
	SERIAL_TYPE_UNKNOWN = 0,
	SERIAL_TYPE_BOOL,
	SERIAL_TYPE_U8,
	SERIAL_TYPE_U16,
	SERIAL_TYPE_U32,
	SERIAL_TYPE_U64,
	SERIAL_TYPE_PTR,
	SERIAL_TYPE_STR,
	SERIAL_TYPE_MEM,
	SERIAL_TYPE_END,
};

struct serial *serial_alloc(size_t size);
struct serial *serial_alloc_ref(struct serial *slr);
void           serial_reset(struct serial *sl);
int            serial_resize(struct serial *sl, size_t size);
void           serial_trim(struct serial *sl);
int            serial_shift(struct serial *sl, ssize_t shift);
void           serial_tostart(struct serial *sl);

int            serial_write_mem(struct serial *sl, const uint8_t *buf, size_t size);
int            serial_write_bool(struct serial *sl, bool v);
int            serial_write_u8(struct serial *sl, uint8_t v);
int            serial_write_u16(struct serial *sl, uint16_t v);
int            serial_write_u32(struct serial *sl, uint32_t v);
int            serial_write_u64(struct serial *sl, uint64_t v);
int            serial_write_ptr(struct serial *sl, void *v);
int            serial_write_str(struct serial *sl, const char *str);
int            serial_write_end(struct serial *sl);

int            serial_read_mem(struct serial *sl, uint8_t **buf, size_t *size);
int            serial_read_bool(struct serial *sl, bool *v);
int            serial_read_u8(struct serial *sl, uint8_t *v);
int            serial_read_u16(struct serial *sl, uint16_t *v);
int            serial_read_u32(struct serial *sl, uint32_t *v);
int            serial_read_u64(struct serial *sl, uint64_t *v);
int            serial_read_ptr(struct serial *sl, void **v);
int            serial_read_str(struct serial *sl, char **str);

enum serial_type serial_peek_type(struct serial *sl);

int            serial_debug(struct re_printf *pf, const struct serial *sl);

