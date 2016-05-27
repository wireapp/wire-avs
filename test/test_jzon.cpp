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


TEST(jzon, invalid_arguments)
{
	struct json_object *jobj = NULL;
	struct json_object *o = 0, *a = 0;
	char *str = 0;
	int err, v = 0;

	err = jzon_decode(&jobj, "{}", 2);
	ASSERT_EQ(0, err);

	ASSERT_EQ(EINVAL, jzon_strdup(NULL, jobj, "key"));
	ASSERT_EQ(EINVAL, jzon_strdup(&str, NULL, "key"));
	ASSERT_EQ(EINVAL, jzon_strdup(&str, jobj, NULL));

	ASSERT_EQ(EINVAL, jzon_int(NULL, jobj, "key"));
	ASSERT_EQ(EINVAL, jzon_int(&v, NULL, "key"));
	ASSERT_EQ(EINVAL, jzon_int(&v, jobj, NULL));

	ASSERT_EQ(EINVAL, jzon_object(NULL, jobj, "key"));
	ASSERT_EQ(EINVAL, jzon_object(&o, NULL, "key"));
	ASSERT_EQ(EINVAL, jzon_object(&o, jobj, NULL));

	ASSERT_EQ(EINVAL, jzon_array(NULL, jobj, "key"));
	ASSERT_EQ(EINVAL, jzon_array(&a, NULL, "key"));
	ASSERT_EQ(EINVAL, jzon_array(&a, jobj, NULL));

	/* verify that JZON did not tamper with the pointers */
	ASSERT_TRUE(o == NULL);
	ASSERT_TRUE(a == NULL);
	ASSERT_TRUE(str == NULL);
	ASSERT_TRUE(v == 0);

	mem_deref(jobj);
}


TEST(jzon, non_existent_objects)
{
	struct json_object *jobj = NULL;
	struct json_object *o, *a;
	char *str;
	int err, v;

	err = jzon_decode(&jobj, "{}", 2);
	ASSERT_EQ(0, err);

	/* verify that JZON returns ENOENT for non-existing objects */
	ASSERT_TRUE(NULL == jzon_str(jobj, "non-existent-string"));
	ASSERT_EQ(ENOENT, jzon_strdup(&str, jobj, "also-non-existent-string"));
	ASSERT_EQ(ENOENT, jzon_int(&v, jobj, "non-existent-int"));
	ASSERT_EQ(ENOENT, jzon_object(&o, jobj, "non-existent-object"));
	ASSERT_EQ(ENOENT, jzon_array(&a, jobj, "non-existent-array"));

	mem_deref(jobj);
}


TEST(jzon, valid_objects)
{
	struct json_object *jobj = NULL;
	struct json_object *o, *a;
	char *str;
	bool bval;
	int err, v;

	static const char json_str[] =
		"{\r\n"
		"  \"string\":\"string\",\r\n"
		"  \"null_string\":null,\r\n"
		"  \"int\":42,\r\n"
		"  \"object\":{},\r\n"
		"  \"array\":[],\r\n"
		"  \"bool0\" : false,"
		"  \"bool1\" : true"
		"}\r\n";

	err = jzon_decode(&jobj, json_str, strlen(json_str));
	ASSERT_EQ(0, err);

	ASSERT_STREQ("string", jzon_str(jobj, "string"));
	ASSERT_TRUE(NULL == jzon_str(jobj, "null_string"));
	ASSERT_EQ(0, jzon_strdup(&str, jobj, "string"));
	ASSERT_EQ(0, jzon_int(&v, jobj, "int"));
	ASSERT_EQ(0, jzon_object(&o, jobj, "object"));
	ASSERT_EQ(0, jzon_array(&a, jobj, "array"));

	ASSERT_EQ(0, jzon_bool(&bval, jobj, "bool0"));
	ASSERT_FALSE(bval);
	ASSERT_EQ(0, jzon_bool(&bval, jobj, "bool1"));
	ASSERT_TRUE(bval);

	ASSERT_STREQ("string", str);
	ASSERT_EQ(42, v);

	mem_deref(str);

	mem_deref(jobj);
}


TEST(jzon, createf_succeeds)
{
	struct json_object *jin = NULL;
	char *jstr = NULL;
	struct json_object *jout = NULL;
	int intv;
	double doublev;
	bool bval;
	int err;

	err = jzon_creatf(&jin, "snifbb",
			  "string", "stringvalue",
			  "null",
			  "int", 42,
			  "double", 42.,
			  "bool0", (int)false,
			  "bool1", (int)true
			  );
	ASSERT_EQ(0, err);

	err = jzon_encode(&jstr, jin);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(jstr != NULL);

	err = jzon_decode(&jout, jstr, strlen(jstr));
	ASSERT_EQ(0, err);

	ASSERT_STREQ("stringvalue", jzon_str(jout, "string"));
	ASSERT_EQ(0, jzon_int(&intv, jout, "int"));
	ASSERT_EQ(intv, 42);
	ASSERT_EQ(0, jzon_double(&doublev, jout, "double"));
	ASSERT_EQ(doublev, 42.);

	ASSERT_EQ(0, jzon_bool(&bval, jout, "bool0"));
	ASSERT_FALSE(bval);
	ASSERT_EQ(0, jzon_bool(&bval, jout, "bool1"));
	ASSERT_TRUE(bval);

	mem_deref(jstr);
	mem_deref(jin);
	mem_deref(jout);
}


TEST(jzon, valid_objects2)
{
	struct odict *od;
	const struct odict_entry *e;
	int err;

	static const char json_str[] =
		"{\r\n"
		"  \"string\":\"string\",\r\n"
		"  \"int\":42,\r\n"
		"  \"object\":{},\r\n"
		"  \"array\":[]\r\n"
		"}\r\n";

	err = json_decode_odict(&od, 32, json_str, strlen(json_str), 8);
	ASSERT_EQ(0, err);

	e = odict_lookup(od, "string");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(ODICT_STRING, e->type);
	ASSERT_STREQ("string", e->u.str);

	e = odict_lookup(od, "int");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(ODICT_INT, e->type);
	ASSERT_EQ(42, e->u.integer);

	e = odict_lookup(od, "object");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(ODICT_OBJECT, e->type);
	ASSERT_TRUE(e->u.odict != NULL);

	e = odict_lookup(od, "array");
	ASSERT_TRUE(e != NULL);
	ASSERT_EQ(ODICT_ARRAY, e->type);
	ASSERT_TRUE(e->u.odict != NULL);

	mem_deref(od);
}


/*
 * the response from GET connections has a special payload which is
 * an array, normally a JSON payload has {} as the outer tokens.
 */
TEST(jzon, parse_connections)
{
	static const char *str =
	"  \t\t  [  \t\t"
	"  {"
	"  \"status\":\"blocked\","
	"  \"conversation\":\"a24411ea-29c5-4fa9-bfc4-28a803a9d450\","
	"  \"to\":\"0796c99c-d19e-4431-96d9-1ff151e6bdd1\","
	"  \"from\":\"9aad484e-5827-4b78-a8eb-08b7b1c3167f\","
	"  \"last_update\":\"2015-10-19T14:42:04.734Z\","
	"  \"message\":\"Hi Tjorn Tordbo,\nLet's connect.\nAlfred Edge0\""
	"  }"
	","
	"  {"
	"  \"status\":\"accepted\","
	"  \"conversation\":\"b888e9d3-82a7-47c0-a8f2-b0017204551a\","
	"  \"to\":\"1ddba185-2a80-4c48-8007-a74ac0413e9b\","
	"  \"from\":\"9aad484e-5827-4b78-a8eb-08b7b1c3167f\","
	"  \"last_update\":\"2015-10-29T14:13:50.241Z\","
	"  \"message\":\"Hi Alfred Edge0,\nLet's connect.\nDavid Call 1\""
	"  }"
	"]";

	struct json_object *jobj;
	int i, count;
	int err;

	err = jzon_decode(&jobj, str, strlen(str));
	ASSERT_EQ(0, err);

	ASSERT_TRUE(jzon_is_array(jobj));

	count = json_object_array_length(jobj);
	ASSERT_EQ(2, count);

	for (i = 0; i < count; i++) {
		struct json_object *jitem;

		jitem = json_object_array_get_idx(jobj, i);
		ASSERT_TRUE(jzon_is_object(jitem));

		/* verify that at least one entry exist */
		ASSERT_TRUE(jzon_str(jitem, "message"));
	}

	mem_deref(jobj);
}


#define MAGIC 0x5001dfcb
struct test {
	uint32_t magic;
	size_t count;
};


static bool jzon_apply_handler(const char *key,
			       struct json_object *jobj, void *arg)
{
	struct test *test = (struct test *)arg;
	const char *val;
	char key_str[64], val_str[64];

	if (test->magic != MAGIC) {
		warning("invalid magic 0x%08x\n", test->magic);
		return true;
	}

	val = json_object_get_string(jobj);

	re_snprintf(key_str, sizeof(key_str), "key%zu", test->count);
	re_snprintf(val_str, sizeof(val_str), "val%zu", test->count);

	if (0 != str_cmp(key_str, key)) {
		warning("apply: key mismatch (%s)\n", key);
		return true;
	}
	if (0 != str_cmp(val_str, val)) {
		warning("apply: value mismatch ('%s' != '%s')\n",
			val_str, val);
		return true;
	}

	++test->count;

	return false;
}


static bool jzon_apply_handler2(const char *key,
				struct json_object *jobj, void *arg)
{
	(void)key;
	(void)jobj;
	(void)arg;
	return true;
}


TEST(jzon, apply_handler)
{
#define COUNT 4
	struct json_object *jobj, *robj;
	struct test test;
	int err;

	memset(&test, 0, sizeof(test));
	test.magic = MAGIC;

	err = jzon_creatf(&jobj, "ssss",
			  "key0", "val0",
			  "key1", "val1",
			  "key2", "val2",
			  "key3", "val3");
	ASSERT_EQ(0, err);

	/* handler should be called COUNT times, return NULL */
	robj = jzon_apply(jobj, jzon_apply_handler, &test);
	ASSERT_TRUE(robj == NULL);
	ASSERT_EQ(COUNT, test.count);

	/* handler should be called 1 time, and return first object */
	robj = jzon_apply(jobj, jzon_apply_handler2, &test);
	ASSERT_TRUE(robj != NULL);

	mem_deref(jobj);
}


TEST(jzon, add_base64)
{
	struct json_object *jobj;
	static const uint8_t data[] = {1,2,3,4,5,6,7,8};
	int err;

	jobj = json_object_new_object();

	err = jzon_add_base64(jobj, "base64", data, sizeof(data));
	ASSERT_EQ(0, err);

	ASSERT_STREQ("AQIDBAUGBwg=", jzon_str(jobj, "base64"));

	mem_deref(jobj);
}
