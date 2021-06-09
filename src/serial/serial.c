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
#include <avs.h>

struct serial {
	struct mbuf mb;
};

struct serial *serial_alloc(size_t size)
{
	return (struct serial*)mbuf_alloc(size);
}

struct serial *serial_alloc_ref(struct serial *slr)
{
	return (struct serial*)mbuf_alloc_ref(&slr->mb);
}

void serial_reset(struct serial *sl)
{
	mbuf_reset(&sl->mb);
}

int serial_resize(struct serial *sl, size_t size)

{
	return mbuf_resize(&sl->mb, size);
}

void serial_trim(struct serial *sl)
{
	mbuf_trim(&sl->mb);
}

int serial_shift(struct serial *sl, ssize_t shift)
{
	return mbuf_shift(&sl->mb, shift);
}

void serial_tostart(struct serial *sl)
{
	sl->mb.pos = 0;
}

int serial_write_mem(struct serial *sl, const uint8_t *buf, size_t size)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_MEM);
	mbuf_write_u32(&sl->mb, size);
	return mbuf_write_mem(&sl->mb, buf, size);
}

int serial_write_bool(struct serial *sl, bool v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_BOOL);
	mbuf_write_u32(&sl->mb, sizeof(uint8_t));
	return mbuf_write_u8(&sl->mb, v ? 1 : 0);
}

int serial_write_u8(struct serial *sl, uint8_t v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_U8);
	mbuf_write_u32(&sl->mb, sizeof(uint8_t));
	return mbuf_write_u8(&sl->mb, v);
}

int serial_write_u16(struct serial *sl, uint16_t v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_U16);
	mbuf_write_u32(&sl->mb, sizeof(uint16_t));
	return mbuf_write_u16(&sl->mb, v);
}

int serial_write_u32(struct serial *sl, uint32_t v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_U32);
	mbuf_write_u32(&sl->mb, sizeof(uint32_t));
	return mbuf_write_u32(&sl->mb, v);
}

int serial_write_u64(struct serial *sl, uint64_t v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_U64);
	mbuf_write_u32(&sl->mb, sizeof(uint64_t));
	return mbuf_write_u64(&sl->mb, v);
}

int serial_write_ptr(struct serial *sl, void *v)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_PTR);
	mbuf_write_u32(&sl->mb, sizeof(uint64_t));
	return mbuf_write_u64(&sl->mb, (uint64_t)v);
}

int serial_write_str(struct serial *sl, const char *str)
{
	size_t size = strlen(str) + 1;

	mbuf_write_u32(&sl->mb, SERIAL_TYPE_STR);
	mbuf_write_u32(&sl->mb, size);
	return mbuf_write_mem(&sl->mb, (uint8_t*)str, size);
}

int serial_write_end(struct serial *sl)
{
	mbuf_write_u32(&sl->mb, SERIAL_TYPE_END);
	mbuf_write_u32(&sl->mb, 0);
	return 0;
}

int serial_read_mem(struct serial *sl, uint8_t **buf, size_t *size)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_MEM) {
		return EINVAL;
	}

	uint32_t len = mbuf_read_u32(&sl->mb);

	uint8_t *tmpb = mem_alloc(len, NULL);
	if (!tmpb) {
		return ENOMEM;
	}

	mbuf_read_mem(&sl->mb, tmpb, len);

	*buf = tmpb;
	*size = len;

	return 0;
}

int serial_read_bool(struct serial *sl, bool *v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_BOOL) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint8_t)) {
		return EINVAL;
	}

	*v = (mbuf_read_u8(&sl->mb) != 0);
	return 0;
}

int serial_read_u8(struct serial *sl, uint8_t *v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_U8) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint8_t)) {
		return EINVAL;
	}

	*v = mbuf_read_u8(&sl->mb);
	return 0;
}

int serial_read_u16(struct serial *sl, uint16_t *v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_U16) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint16_t)) {
		return EINVAL;
	}

	*v = mbuf_read_u16(&sl->mb);
	return 0;
}

int serial_read_u32(struct serial *sl, uint32_t *v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_U32) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint32_t)) {
		return EINVAL;
	}

	*v = mbuf_read_u32(&sl->mb);
	return 0;
}

int serial_read_u64(struct serial *sl, uint64_t *v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_U64) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint64_t)) {
		return EINVAL;
	}

	*v = mbuf_read_u64(&sl->mb);
	return 0;
}

int serial_read_ptr(struct serial *sl, void **v)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_PTR) {
		return EINVAL;
	}

	uint32_t size = mbuf_read_u32(&sl->mb);
	if (size != sizeof(uint64_t)) {
		return EINVAL;
	}

	uint64_t v64 = mbuf_read_u64(&sl->mb);
	*v = (void*)v64;

	return 0;
}

int serial_read_str(struct serial *sl, char **strp)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	if (type != SERIAL_TYPE_STR) {
		return EINVAL;
	}

	uint32_t len = mbuf_read_u32(&sl->mb);

	return mbuf_strdup(&sl->mb, strp, len);
}

enum serial_type serial_peek_type(struct serial *sl)
{
	uint32_t type = mbuf_read_u32(&sl->mb);
	sl->mb.pos -= sizeof(uint32_t);
	return (enum serial_type) type;
}

int serial_debug(struct re_printf *pf, const struct serial *sl)
{
	return mbuf_debug(pf, &sl->mb);
}

