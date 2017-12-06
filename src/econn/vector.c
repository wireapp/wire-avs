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
#include <sodium.h>
#include <re.h>
#include "avs_base.h"
#include "avs_log.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_econn.h"


static void destructor(void *arg)
{
	struct vector *vec = arg;

	/* wipe the data */
	if (vec->bytes && vec->len)
		sodium_memzero(vec->bytes, vec->len);

	mem_deref(vec->bytes);
}


/* @param bytes optional content */
int vector_alloc(struct vector **vecp, const uint8_t *bytes, size_t len)
{
	struct vector *vec;
	int err = 0;

	if (!vecp || !len)
		return EINVAL;

	vec = mem_zalloc(sizeof(*vec), destructor);
	if (!vec)
		return ENOMEM;

	vec->bytes = mem_alloc(len, NULL);
	if (!vec->bytes) {
		err = ENOMEM;
		goto out;
	}
	vec->len = len;

	if (bytes)
		memcpy(vec->bytes, bytes, len);

 out:
	if (err)
		mem_deref(vec);
	else
		*vecp = vec;

	return err;
}
