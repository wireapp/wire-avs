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

#include "avs.h"
#include "flowmgr.h"


static void destructor(void *arg)
{
	struct log_elem *logel = arg;

	mem_deref(logel->mb);
}


int log_elem_alloc(struct log_elem **logelp)
{
	struct log_elem *logel;
	int err = 0;

	if (!logelp)
		return EINVAL;

	logel = mem_zalloc(sizeof(*logel), destructor);
	if (!logel)
		return ENOMEM;

	if (err)
		goto out;
 out:
	if (err)
		mem_deref(logel);
	else
		*logelp = logel;

	return err;
}
