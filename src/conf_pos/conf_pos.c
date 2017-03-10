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
#include "avs_conf_pos.h"


static void cp_destructor(void *arg)
{
	struct conf_part *cp = arg;

	list_unlink(&cp->le);
	mem_deref(cp->uid);
}


int conf_part_add(struct conf_part **cpp, struct list *partl,
		  const char *userid, void *data)
{
	struct conf_part *cp;
	int err;

	cp = mem_zalloc(sizeof(*cp), cp_destructor);
	if (!cp)
		return ENOMEM;

	cp->data = data;
	err = str_dup(&cp->uid, userid);
	if (err)
		goto out;

	cp->pos = conf_pos_calc(userid);

	list_append(partl, &cp->le, cp);

 out:
	if (err)
		mem_deref(cp);
	else if (cpp)
		*cpp = cp;

	return err;
}


struct conf_part *conf_part_find(const struct list *partl, const char *userid)
{
	struct le *le;

	if (!partl || !userid)
		return NULL;

	for (le = list_head(partl); le; le = le->next) {
		struct conf_part *part = le->data;

		if (0 == str_casecmp(part->uid, userid))
			return part;
	}

	return NULL;
}


static bool sort_handler(struct le *le1, struct le *le2, void *arg)
{
	struct conf_part *cp1 = le1 ? le1->data : NULL;
	struct conf_part *cp2 = le2 ? le2->data : NULL;

	(void)arg;

	if (!cp1 || !cp2)
		return true;

	return cp1->pos >= cp2->pos;
}


uint32_t conf_pos_calc(const char *uid)
{
	uint32_t hp;

	hp = hash_joaat_str_ci(uid);

	return hp;
}


/* Sort list of participants in order of postion,
 * from left to right.
 * This algorithm maps positions deterministically from the user IDs.
 * Another approach would be to make position depend on order of joining,
 * where new joiners go in the middle.
 * In that case it's a good idea to keep track of user IDs that have left the
 * call, so that the can be positioned in the same
 * location if the user comes back. However, a user who leaves and comes back
 * will not get the same positions himself, as this history is cleared at the
 * end of a call. Same thing when a user transfers the call to another device.
 */
void conf_pos_sort(struct list *partl)
{
	struct conf_part *cp;
	int n;
	struct le *elem;

	if (!partl)
		return;

	n = list_count(partl);
	if (n == 0)
		return;

	/* treat single participant separately to avoid divide by zero later */
	if (n == 1) {
		elem = partl->head;
		cp = elem->data;
		if (cp)
			cp->pos = 0;
		return;
	}

	/* Iterate over list and compute hash values */
	LIST_FOREACH(partl, elem) {
		cp = elem->data;
		if (cp) {
			cp->pos = conf_pos_calc(cp->uid);
		}
	}

	/* Sort by position value */
	list_sort(partl, sort_handler, NULL);
}


int conf_pos_print(struct re_printf *pf, const struct list *partl)
{
	struct le *le;
	int err = 0;

	err |= re_hprintf(pf, "conf_participants: (%u)\n", list_count(partl));

	for (le = list_head(partl); le; le = le->next) {
		struct conf_part *part = le->data;
		err |= re_hprintf(pf, "....userid=%s  pos=%u  data=%p\n",
				  part->uid, part->pos, part->data);
	}

	return err;
}
