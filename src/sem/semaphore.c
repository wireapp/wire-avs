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
#include "avs_semaphore.h"

#if __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

struct avs_sem {
#if __APPLE__
	dispatch_semaphore_t sem;
#else
	sem_t sem;
#endif
};

static void destructor(void *arg)
{
	struct avs_sem *s = arg;
#if __APPLE__
	if (s->sem) {
		dispatch_release(s->sem);
	}
#else
	sem_destroy(&s->sem);
#endif
}

int avs_sem_alloc(struct avs_sem **sp, int value)
{
	struct avs_sem *s;
	int err = 0;

	if (!sp)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), destructor);
	if (!s)
		return ENOMEM;

#if __APPLE__
	s->sem = dispatch_semaphore_create((long)value);
	if (s->sem == NULL) {
		err = ENODEV;
		goto out;
	}
#else
	err = sem_init(&s->sem, 0, value);
	if (err != 0) {
		goto out;
	}
#endif
	*sp = s;

out:
	if (err) {
		mem_deref(s);
		return err;
	}
	else {
		return 0;
	}
}

#if __APPLE__

int avs_sem_post(struct avs_sem *s)
{
	dispatch_semaphore_signal(s->sem);
	return 0;
}

int avs_sem_wait(struct avs_sem *s)
{
	dispatch_semaphore_wait(s->sem, DISPATCH_TIME_FOREVER);
	return 0;
}

#else

int avs_sem_post(struct avs_sem *s)
{
	if (sem_post(&s->sem) == 0) {
		return 0;
	}
	else {
		return errno;
	}
}

int avs_sem_wait(struct avs_sem *s)
{
	if (sem_wait(&s->sem) == 0) {
		return 0;
	}
	else {
		return errno;
	}
}

#endif

