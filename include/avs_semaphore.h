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

#ifndef AVS_SEMAPHORE_H
#define AVS_SEMAPHORE_H

#include <stdlib.h>
#include <stdbool.h>
#include "re.h"

#ifdef __cplusplus
extern "C" {
#endif


struct avs_sem;

int avs_sem_alloc(struct avs_sem **sp, int value);
int avs_sem_post(struct avs_sem *s);
int avs_sem_wait(struct avs_sem *s);

#ifdef __cplusplus
}
#endif

#endif //AVS_SEMAPHORE_H

