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
#ifndef AVS_LOCKED_QUEUE_H
#define AVS_LOCKED_QUEUE_H

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


struct locked_queue_t;

int locked_queue_alloc(struct locked_queue_t **lqp, bool blocking);

int locked_queue_push(struct locked_queue_t *q, struct le *element,
		      void *item);

int locked_queue_pop(struct locked_queue_t *q, struct le **element);


#ifdef __cplusplus
}
#endif

#endif //AVS_LOCKED_QUEUE_H
