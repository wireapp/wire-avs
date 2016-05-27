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


class DictTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		err = dict_alloc(&dict);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(dict != NULL);
	}

	virtual void TearDown() override
	{
		ASSERT_EQ(0, err);
		mem_deref(dict);
	}

protected:
	struct dict *dict = NULL;
	int err = 0;
};


TEST_F(DictTest, alloc_and_free)
{
	ASSERT_TRUE(dict != NULL);
}


TEST_F(DictTest, lookup_not_found)
{
	ASSERT_TRUE(NULL == dict_lookup(dict, "non-exist-key"));
}


TEST_F(DictTest, add_and_lookup)
{
	char *str;
	void *val;

	err = str_dup(&str, "value");
	ASSERT_EQ(0, err);

	err = dict_add(dict, "key", (void *)str);
	ASSERT_EQ(0, err);

	val = dict_lookup(dict, "key");

	ASSERT_TRUE(val != NULL);
	ASSERT_TRUE(0 == str_cmp((char *)val, str));

	mem_deref(str);
}


TEST_F(DictTest, add_and_remove)
{
	char *str;

	err = str_dup(&str, "value");
	ASSERT_EQ(0, err);

	err = dict_add(dict, "key", (void *)str);
	ASSERT_EQ(0, err);

	dict_remove(dict, "key");

	ASSERT_TRUE(NULL == dict_lookup(dict, "key"));

	mem_deref(str);
}


TEST_F(DictTest, count)
{
	ASSERT_EQ(0, dict_count(dict));
}


TEST_F(DictTest, count_many)
{
	char *str;

	err = str_dup(&str, "value");
	ASSERT_EQ(0, err);

	err |= dict_add(dict, "0..abdkelkalk", str);
	err |= dict_add(dict, "1..panjhalkkk", str);
	err |= dict_add(dict, "2..mmmmmmmzmm", str);
	err |= dict_add(dict, "3..plaplaplap", str);
	err |= dict_add(dict, "4..zzzpmldskk", str);

	ASSERT_EQ(5, dict_count(dict));

	mem_deref(str);
}


struct object {
	struct dict **dict;
	char key[16];
};


static void destructor(void *arg)
{
	struct object *obj = (struct object *)arg;

	/* check if the dictionary is still valid */
	if (*obj->dict)
		dict_remove(*obj->dict, obj->key);
}


TEST_F(DictTest, many_objects)
{
	#define NUM_OBJECTS 100

	struct {
		struct object *obj;
	} testv[NUM_OBJECTS];
	unsigned i;

	for (i=0; i<NUM_OBJECTS; i++) {

		struct object *obj;

		obj = (struct object *)mem_zalloc(sizeof(*obj), destructor);
		ASSERT_TRUE(obj != NULL);
		obj->dict = &dict;
		re_snprintf(obj->key, sizeof(obj->key), "%u", i);

		testv[i].obj = obj;

		err = dict_add(dict, obj->key, testv[i].obj);
		ASSERT_EQ(0, err);

		/* Object is now owned by the dictionary */
		mem_deref(obj);
	}
}


TEST_F(DictTest, two_entries_to_same_object)
{
	struct object *obj;
	obj = (struct object *)mem_zalloc(sizeof(*obj), NULL);
	ASSERT_TRUE(obj != NULL);

	err |= dict_add(dict, "a", obj);
	err |= dict_add(dict, "b", obj);
	ASSERT_EQ(0, err);

	mem_deref(obj);
}


TEST_F(DictTest, many_objects_in_two_containers)
{
#define N 256
	struct object *objv[N];
	unsigned i;

	for (i=0; i<N; i++) {

		struct object *obj;

		obj = (struct object *)mem_zalloc(sizeof(*obj), destructor);
		ASSERT_TRUE(obj != NULL);
		obj->dict = &dict;
		rand_str(obj->key, sizeof(obj->key));

		objv[i] = obj;  /* ref 1 */

		err = dict_add(dict, obj->key, obj);  /* ref 2 */
		ASSERT_EQ(0, err);
	}

	/* flush the dictionary first */
	dict = (struct dict *)mem_deref(dict);

	/* then deref all objects */
	for (i=0; i<N; i++) {
		mem_deref(objv[i]);
	}
}
