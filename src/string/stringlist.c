/*
* Wire
* Copyright (C) 2021 Wire Swiss GmbH
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
#include "avs_string.h"

static void stringlist_destructor(void *arg)
{
	struct stringlist_info *info = arg;

	list_unlink(&info->le);

	mem_deref(info->str);
}

int stringlist_append(struct list *list, const char *str)
{
	struct stringlist_info *info = NULL;
	int err = 0;

	info = mem_zalloc(sizeof(*info), stringlist_destructor);
	if (!info)
		return ENOMEM;

	err = str_dup(&info->str, str);
	if (err)
		goto out;

	list_append(list, &info->le, info);
out:
	if (err)
		mem_deref(info);

	return err;
}

int stringlist_clone(const struct list *from, struct list *to)
{
	struct le *le;
	int err = 0;

	if (!from || !to)
		return EINVAL;

	list_flush(to);

	LIST_FOREACH(from, le) {
		struct stringlist_info *str = le->data;

		err = stringlist_append(to, str->str);
		if (err)
			goto out;
	}

out:
	if (err)
		list_flush(to);
	return err;
}

