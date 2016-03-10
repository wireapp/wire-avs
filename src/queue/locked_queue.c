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
#include <re.h>
#include "avs_lockedqueue.h"
#include "avs_semaphore.h"

struct locked_queue_t {
	struct list list;
	struct lock *lock;
	struct avs_sem *sem;
};

static void locked_queue_destructor(void *arg)
{
	struct locked_queue_t *q = (struct locked_queue_t *)arg;

	list_flush(&q->list);
	mem_deref(q->lock);
	mem_deref(q->sem);
}

int locked_queue_alloc(struct locked_queue_t **lqp, bool blocking)
{
	struct locked_queue_t *q;
	int err = 0;

	if (!lqp)
		return EINVAL;

	q = mem_zalloc(sizeof(*q), locked_queue_destructor);
	if (!q)
		return ENOMEM;

	list_init(&q->list);
	err = lock_alloc(&q->lock);
	if (err) {
		goto out;
	}

	if (blocking) {
		err = avs_sem_alloc(&q->sem, 0);
		if (err) {
			goto out;
		}
	}
out:
	if (err) {
		mem_deref(q);
	}
	else
		*lqp = q;

	return err;
}


int locked_queue_push(struct locked_queue_t* q, struct le* element, void *item)
{
	if (!q || !element)
		return EINVAL;

	lock_write_get(q->lock);
	list_append(&q->list, element, item);
	lock_rel(q->lock);

	if (q->sem) {
		avs_sem_post(q->sem);
	}
	return 0;
}


int locked_queue_pop(struct locked_queue_t* q, struct le** element)
{
	struct le *list_elem;

	if (!q)
		return EINVAL;

	if (q->sem) {
		avs_sem_wait(q->sem);
	}
	lock_write_get(q->lock);

	list_elem = list_head(&q->list);
	if (list_elem) {
		list_unlink(list_elem);
	}
	lock_rel(q->lock);

	if (!list_elem) {
		return ENODATA;
	}

	*element = list_elem;

	return 0;
}

