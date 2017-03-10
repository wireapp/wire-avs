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

#ifndef AVS_CONF_POS_H
#define AVS_CONF_POS_H    1

struct conf_part {
	char *uid;
	void *data;

	uint32_t pos;

	struct le le;
};


/* Init function for the above structure. */
int conf_part_add(struct conf_part **cpp, struct list *partl,
		  const char *uid, void *data);

struct conf_part *conf_part_find(const struct list *partl, const char *userid);

/* Return position associated with user id */
uint32_t conf_pos_calc(const char *uid);

/* Sort list of participants in order of postion,
 * from left to right, and assign
 */
void conf_pos_sort(struct list *partl);


int conf_pos_print(struct re_printf *pf, const struct list *partl);

#endif // #ifndef AVS_CONF_POS_H
