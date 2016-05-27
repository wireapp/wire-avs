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
#include <avs.h>
#include <gtest/gtest.h>


TEST(confpos, basic)
{
	struct conf_part *a, *b;
	struct mediaflow *mf=0;
	uint32_t pos_a;
	int err;

	err = conf_part_add(&a, NULL, "a", mf);
	ASSERT_EQ(0, err);

	/* save the position hash */
	pos_a = a->pos;

	err = conf_part_add(&b, NULL, "b", mf);
	ASSERT_EQ(0, err);

	/* verify that A's position hash did not change */
	ASSERT_EQ(pos_a, a->pos);

	mem_deref(b);
	mem_deref(a);
}


/* should be in descending order */
static bool is_sorted(const struct list *partl)
{
	struct le *le;
	uint32_t pos_prev = ~0;

	for (le = list_head(partl); le; le = le->next) {
		struct conf_part *part = (struct conf_part *)le->data;

		if (part->pos > pos_prev) {
			warning("part: pos %u > prev %u\n",
				part->pos, pos_prev);
			return false;
		}

		pos_prev = part->pos;
	}

	return true;
}


TEST(confpos, sorted_participants)
{
	struct list partl = LIST_INIT;
	struct conf_part *part;
	struct le *le;
	int err;

	ASSERT_EQ(0, list_count(&partl));

	part = conf_part_find(&partl, "60fcea5b-6b85-435f-bad4-b002e7df9792");
	ASSERT_TRUE(part == NULL);

	/* add 4 participants with same userid */
	err = conf_part_add(NULL, &partl,
			    "60fcea5b-6b85-435f-bad4-b002e7df9792", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "60fcea5b-6b85-435f-bad4-b002e7df9792", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "60fcea5b-6b85-435f-bad4-b002e7df9792", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "60fcea5b-6b85-435f-bad4-b002e7df9792", NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(4, list_count(&partl));

	conf_pos_sort(&partl);
	ASSERT_TRUE(is_sorted(&partl));

	/* add 4 participants with different userid */

	err = conf_part_add(NULL, &partl,
			    "3e2e9ea3-ec0f-49a1-bc5a-cb829050dada", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "eabf0c4f-d8c4-4508-90c2-06565de7d3d7", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "02f206d8-dfc5-492a-a743-3efdf5c5ea22", NULL);
	ASSERT_EQ(0, err);
	err = conf_part_add(NULL, &partl,
			    "3f9479a7-5b40-4e2a-aa79-a38abd412a96", NULL);
	ASSERT_EQ(0, err);

	ASSERT_EQ(8, list_count(&partl));

	conf_pos_sort(&partl);
	ASSERT_TRUE(is_sorted(&partl));

	/* remove the first */
	mem_deref(list_ledata(list_head(&partl)));
	ASSERT_EQ(7, list_count(&partl));

	conf_pos_sort(&partl);
	ASSERT_TRUE(is_sorted(&partl));

	/* remove the last */
	mem_deref(list_ledata(list_tail(&partl)));
	ASSERT_EQ(6, list_count(&partl));

	conf_pos_sort(&partl);
	ASSERT_TRUE(is_sorted(&partl));

	part = conf_part_find(&partl, "60fcea5b-6b85-435f-bad4-b002e7df9792");
	ASSERT_TRUE(part != NULL);

#if 0
	re_printf("%H", conf_pos_print, &partl);
#endif

	list_flush(&partl);
}
