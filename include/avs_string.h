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

#ifndef AVS_STRING_H
#define AVS_STRING_H    1

#include <string.h>

#define streq(a, b) ((*(a) == *(b)) && (strcmp(a, b) == 0))
#define strcaseeq(a, b) (str_casecmp(a, b) == 0)


struct str_wordexp {
	size_t wordc;
	char **wordv;
};

int  str_wordexp(struct str_wordexp *we, const char *str);
void str_wordfree(struct str_wordexp *we);

struct stringlist_info {
	char *str;
	struct le le;
};

int stringlist_append(struct list *list, const char *str);
int stringlist_clone(const struct list *from, struct list *to);

#endif //#ifndef AVS_STRING_H

