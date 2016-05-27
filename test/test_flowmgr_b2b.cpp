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
#include <avs.h>
#include <gtest/gtest.h>
#include "fakes.hpp"
#include "ztest.h"

#define FM_MSYS "audummy"


/*
 * This back-to-back test sets up a flow between A and B.
 *
 *
 *  [ Device A ] -------> [ Belfry ] -------> [ Device B ]
 *
 */

struct test {
	struct list devicel;  /* struct device */
	StunServer stun_server;
	char convid[UUID_SZ];
	int err=0;
	bool send_flowadd = true;
	bool use_stun = false;
};

struct device {
	struct le le;
	struct test *test;
	struct flowmgr *fm;
	char name[32];
	char userid[64];
	bool estab;
	bool expect_estab;

	unsigned n_post_flows;
	unsigned n_put_localsdp;
	unsigned n_put_localcand;
};


static const char *flowid_calc(const struct device *from,
			       const struct device *to)
{
	// TODO: not re-entrant
	static char flowid[64] = "ffffffff-0000-0000-0000-000000000000";
	int x,y;

	if (!from || !to) {
		warning("flowid_calc: no FROM or TO device\n");
		return "???";
	}

	x = from->name[0] - 0x40;
	y = to->name[0] - 0x40;

	flowid[14] = 0x40 + x + y;

	return flowid;
}


static struct device *find_device(const struct test *test, const char *userid)
{
	struct le *le;

	for (le = test->devicel.head; le; le = le->next) {
		struct device *dev = (struct device *)le->data;

		if (0 == str_casecmp(dev->userid, userid))
			return dev;
	}

	return NULL;
}


static struct device *find_device_flow(const struct test *test,
				       const struct device *target,
				       const char *flowid)
{
	struct le *le;

	for (le = test->devicel.head; le; le = le->next) {
		struct device *dev = (struct device *)le->data;

		if (0 == str_casecmp(flowid, flowid_calc(dev, target)))
			return dev;

		if (0 == str_casecmp(flowid, flowid_calc(target, dev)))
			return dev;
	}

	return NULL;
}


static int belfry_send_flowadd(const struct device *from,
			       const struct device *target,
			       const char *convid,
			       const struct zapi_flow *flowv,
			       size_t flowc)
{
	struct json_object *jobj = NULL;
	char *json = NULL;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	err = zapi_flowadd_encode(jobj, convid, flowv, flowc);
	if (err)
		return err;

	err = jzon_encode(&json, jobj);
	if (err)
		goto out;

	flowmgr_process_event(NULL, target->fm, "application/json",
			      json, strlen(json));

 out:
	mem_deref(json);
	mem_deref(jobj);

	return err;
}


static int belfry_reply_200_with_flows(const struct device *target,
				       struct rr_resp *ctx,
				       const struct zapi_flow *flowv,
				       size_t flowc)
{
	struct json_object *jobj;
	char *json = NULL;
	int err;

	if (!target)
		return EINVAL;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	err = zapi_flows_encode(jobj, flowv, flowc);
	if (err)
		goto out;

#if 0
	re_printf("  ~~ REPLY 200:\n");
	jzon_dump(jobj);
#endif

	err = jzon_encode(&json, jobj);
	if (err)
		goto out;

	err = flowmgr_resp(target->fm, 200, "OK", "application/json",
			   json, strlen(json), ctx);

 out:
	mem_deref(json);
	mem_deref(jobj);

	return err;
}


/*
{
  "state":"offer",
  "sdp":"v=0\r\no=- 760957628 627546547 IN IP4 10.0.0.102\r\ns=-\r\nc=IN IP4 10.0.0.102\r\nt=0 0\r\na=OFFER\r\nm=audio 20412 RTP\/SAVPF 96 0 8\r\na=rtpmap:96 opus\/48000\/2\r\na=fmtp:96 stereo=0;sprop-stereo=0\r\na=rtpmap:0 PCMU\/8000\r\na=rtpmap:8 PCMA\/8000\r\na=rtcp:20413\r\na=sendrecv\r\na=rtcp-mux\r\na=ice-ufrag:secret-ufrag\r\na=ice-pwd:secret-pwd\r\na=candidate:1 1 UDP 2130706431 10.0.0.102 20412 typ host\r\na=fingerprint:sha-1 7e:a9:7e:0b:23:a1:c3:eb:e3:47:4f:9d:bc:67:a8:f0:54:aa:b2:18\r\na=setup:actpass\r\n",
  "flow":"cf5dd5de-f958-4a4a-8bf1-d257e3bff3ae",
  "conversation":"292b9669-aca7-4433-9fb9-aad5c087e6e9",
  "type":"call.remote-sdp"
}
*/
static int send_event_remote_sdp(const struct device *from,
				 const struct device *target,
				 const char *state, const char *sdp,
				 const char *flowid, const char *convid)
{
	struct zapi_remote_sdp rsdp;
	struct json_object *jobj;
	char *json = NULL;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	memset(&rsdp, 0, sizeof(rsdp));

	rsdp.state = state;
	rsdp.sdp   = sdp;
	rsdp.flow  = flowid;
	rsdp.conv  = convid;

	err = zapi_remote_sdp_encode(jobj, &rsdp);
	if (err)
		goto out;

	err = jzon_encode(&json, jobj);
	if (err)
		goto out;

	err = flowmgr_process_event(NULL, target->fm, "application/json",
				    json, strlen(json));
	if (err) {
		warning("flowmgr_process_event error (%m)\n", err);
	}

 out:
	mem_deref(json);
	mem_deref(jobj);

	return err;
}


/*
{
  "flow":"cf5dd5de-f958-4a4a-8bf1-d257e3bff3ae",
  "conversation":"292b9669-aca7-4433-9fb9-aad5c087e6e9",
  "type":"call.remote-candidates-update",
  "candidates":[
    {
      "sdp":"a=candidate:1 1 udp 255 54.228.185.181 51781 typ relay\r\n",
      "sdp_mline_index":0,
      "sdp_mid":"audio"
    }
  ]
}
*/
static int send_event_remote_candidates(const struct device *from,
					const struct device *target,
					const char *convid, const char *flowid,
					const char *sdp, int sdp_mline_index,
					const char *sdp_mid)
{
	struct json_object *jobj, *jcands;
	char *json = NULL;
	bool handled = false;
	int err;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	json_object_object_add(jobj, "flow",
			       json_object_new_string(flowid));
	json_object_object_add(jobj, "conversation",
			       json_object_new_string(convid));
	json_object_object_add(jobj, "type",
	       json_object_new_string("call.remote-candidates-update"));

	jcands = json_object_new_array();
	{
		struct zapi_candidate cand = {
			.mid         = sdp_mid,
			.mline_index = sdp_mline_index,
			.sdp         = sdp
		};
		json_object *jcand;

		jcand = json_object_new_object();

		err = zapi_candidate_encode(jcand, &cand);
		if (err)
			goto out;

		json_object_array_add(jcands, jcand);
	}
	json_object_object_add(jobj, "candidates", jcands);

	err = jzon_encode(&json, jobj);
	if (err)
		goto out;
	err = flowmgr_process_event(&handled, target->fm, "application/json",
				    json, strlen(json));
	if (err) {
		warning("flowmgr_process_event() failed (%m)\n", err);
		goto out;
	}

	if (!handled) {
		warning("event not handled\n");
	}

 out:
	mem_deref(json);
	mem_deref(jobj);

	return err;
}


static int relay_sdp(const struct device *from,
		     const struct device *target,
		     const char *convid,
		     const char *flowid, const char *content, size_t clen)
{
	struct json_object *jobj = NULL;
	struct zapi_local_sdp lsdp;
	int err;

	err = jzon_decode(&jobj, content, clen);
	if (err) {
		warning("JSON parse error [%zu bytes]\n", clen);
		goto out;
	}

	err = zapi_local_sdp_decode(&lsdp, jobj);
	if (err)
		goto out;

	err = send_event_remote_sdp(from, target, lsdp.type,
				    lsdp.sdp, flowid, convid);
	if (err)
		goto out;

 out:
	mem_deref(jobj);

	return err;
}


static int relay_candidates(const struct device *from,
			    const struct device *target,
			    const char *convid, const char *flowid,
			    const char *content, size_t clen)
{
	struct json_object *jobj = NULL;
	struct json_object *jcands;
	int i, n;
	int err = 0;

	err = jzon_decode(&jobj, content, clen);
	if (err) {
		warning("JSON parse error [%zu bytes]\n", clen);
		goto out;
	}

	err = jzon_array(&jcands, jobj, "candidates");
	if (err) {
		warning("flow_cand_handler: no 'candidates'\n");
		return err;
	}

	n = json_object_array_length(jcands);

	for (i = 0; i < n; ++i) {
		struct json_object *jcand;
		struct zapi_candidate cand;

		jcand = json_object_array_get_idx(jcands, i);
		if (!jcand) {
			warning("flow_cand_handler: cand[%d] is missing\n", i);
			continue;
		}

		err = zapi_candidate_decode(&cand, jcand);
		if (err)
			goto out;

		err = send_event_remote_candidates(from, target,
						   convid, flowid,
						   cand.sdp, cand.mline_index,
						   cand.mid);
		if (err)
			goto out;
	}

 out:
	mem_deref(jobj);

	return err;
}


static int handle_sdp_body(struct test *test, struct device *dev,
			   const char *convid,
			   const char *content, size_t clen)
{
	struct odict *jcontent = NULL, *jobj;
	const struct odict_entry *e;
	struct le *le;
	int err = 0;

	err = json_decode_odict(&jcontent, 4, content, clen, 8);
	if (err) {
		warning("belfry: JSON decode failed [%zu bytes] (%m)\n",
			clen, err);

		re_printf("- - - - - - - - - - - - - - - - - - - -\n");
		re_printf("%b", content, clen);
		re_printf("- - - - - - - - - - - - - - - - - - - -\n");

		goto out;
	}

	e = odict_lookup(jcontent, "sdp");
	if (!e) {
		warning("object 'sdp' not found\n");
		err = EPROTO;
		goto out;
	}
	jobj = e->u.odict;

	for (le = jobj->lst.head; le; le = le->next) {

		struct odict_entry *entry = (struct odict_entry *)le->data;
		const char *user_id = entry->key;
		struct odict *uobj = entry->u.odict;
		const char *type, *sdp;
		struct device *target;
		const char *flowid;

		target = find_device(test, user_id);
		if (!target) {
			warning("device not found (%s)\n", user_id);
			return ENOENT;
		}

		if (target == dev) {
			warning("target is self!\n");
		}

		type = odict_lookup(uobj, "type")->u.str;
		sdp  = odict_lookup(uobj, "sdp")->u.str;

		flowid = flowid_calc(dev, target);

		/* NOTE: must be sent after flow-add */
		err = send_event_remote_sdp(dev, target, type, sdp,
					    flowid, convid);
		if (err) {
			warning("send_event_remote_sdp failed (%m)\n",
				err);
			return err;
		}
	}

 out:
	mem_deref(jcontent);

	return err;
}


static int belfry_post_flows(struct device *dev, const struct pl *pl_convid,
			     struct rr_resp *ctx,
			     const char *content, size_t clen)
{
#define MAX_FLOWS 4
	struct test *test = dev->test;
	struct zapi_flow flowv[MAX_FLOWS];
	size_t flowc = 0;
	struct le *le;
	char convid[UUID_SZ];
	unsigned i;
	int err;

	info("[ %s ] POST flows\n", dev->name);

	/* Posting for flows will trigger 3 things:
	 *
	 * 0. generate a new Flow ID
	 * 1. sending a "call.flow-add" event to other peer
	 * 2. reply 200 OK with JSON body
	 */
	pl_strcpy(pl_convid, convid, sizeof(convid));

	memset(flowv, 0, sizeof(flowv));

	flowc = 0;

	/* Send call.flow-add to all other devices */
	for (le = test->devicel.head; le; le = le->next) {
		struct device *target = (struct device *)le->data;
		struct zapi_flow *flow = &flowv[flowc];
		const char *flowid = flowid_calc(dev, target);

		if (target == dev)
			continue;

		memset(flow, 0, sizeof(*flow));

		/* populate flow */
		flow->active = false;

		str_ncpy(flow->sdp_step, "pending", sizeof(flow->sdp_step));
		str_ncpy(flow->remote_user, dev->userid,
			 sizeof(flow->remote_user));

		str_ncpy(flow->id, flowid, sizeof(flow->id));

		if (test->send_flowadd) {
			err = belfry_send_flowadd(dev, target, convid,
						  flow, 1);
			if (err) {
				warning("belfry_send_flowadd error (%m)\n",
					err);
				return err;
			}
		}
		else {
			info("skip flowadd\n");
		}

		/* after flowadd; POST reply needs this */
		str_ncpy(flow->remote_user, target->userid,
			 sizeof(flow->remote_user));

		++flowc;

		if (flowc > MAX_FLOWS) {
			warning("reached max flows\n");
			return EBADMSG;
		}
	}

	if (content && clen) {

		err = handle_sdp_body(test, dev, convid, content, clen);
		if (err) {
			warning("belfry: handle_sdp_body failed (%m)\n",
				err);
			return err;
		}
	}

	err = belfry_reply_200_with_flows(dev, ctx, flowv, flowc);
	if (err) {
		warning("belfry_reply_200_with_flows failed (%m)\n", err);
		return err;
	}

	return 0;
}


static int mk_calls_config(char **strp, struct test *test)
{
	struct odict *stun=0, *array=0, *config=0;
	char url[256];
	int err = 0;

	re_snprintf(url, sizeof(url), "stun:%J", &test->stun_server.addr);

	err  = odict_alloc(&stun, 4);
	if (err)
		goto out;
	err |= odict_entry_add(stun, "urls", ODICT_STRING, url);
	if (err)
		goto out;

	err  = odict_alloc(&array, 4);
	if (err)
		goto out;
	err |= odict_entry_add(array, "0", ODICT_OBJECT, stun);
	if (err)
		goto out;

	err  = odict_alloc(&config, 4);
	if (err)
		goto out;
	err |= odict_entry_add(config, "ice_servers", ODICT_ARRAY, array);
	if (err)
		goto out;

	err = re_sdprintf(strp, "%H", json_encode_odict, config);
	if (err)
		goto out;

#if 0
	re_printf("CALLS_CONFIG: %s\n", *strp);
#endif

 out:
	mem_deref(config);
	mem_deref(array);
	mem_deref(stun);

	return err;
}


static int flowmgr_req_handler(struct rr_resp *ctx,
			       const char *path, const char *method,
			       const char *ctype,
			       const char *content, size_t clen, void *arg)
{
	struct device *dev = (struct device *)arg;
	struct test *test = dev->test;
	struct pl pl_convid, pl_flowid;
	struct le *le;
	int err = 0;

	if (!ctx)
		return 0;

	if (streq(method, "POST") &&
	    0 == re_regex(path, strlen(path),
			  "/conversations/[0-9a-f\\-]+/call/flows/v2",
			  &pl_convid)) {

		++dev->n_post_flows;

		err = belfry_post_flows(dev, &pl_convid, ctx, content, clen);
		if (err)
			return err;
	}
	else if (streq(method, "PUT") &&
		 0 == re_regex(path, strlen(path),
			       "/conversations/[0-9a-f\\-]+/call/"
			       "flows/[0-9a-f\\-]+/local_sdp",
			       &pl_convid, &pl_flowid)) {

		char convid[37], flowid[37];
		struct device *target;

		++dev->n_put_localsdp;

		pl_strcpy(&pl_convid, convid, sizeof(convid));
		pl_strcpy(&pl_flowid, flowid, sizeof(flowid));

		/* local_sdp should only be sent to flowid */

		target = find_device_flow(test, dev, flowid);
		if (!target) {
			warning("[ %s ] target not found\n", dev->name);
			return ENOENT;
		}
		else {
			info("[ %s ] flowid=%s mapped to target device"
			     " '%s'\n",
			     dev->name, flowid, target->name);

			err = relay_sdp(dev, target, convid, flowid,
					content, clen);
			if (err)
				return err;
		}

		err = flowmgr_resp(dev->fm, 200, "OK", NULL, NULL,
				   0, ctx);
		if (err)
			return err;

	}
	else if (streq(method, "PUT") &&
		 0 == re_regex(path, strlen(path),
			       "/conversations/[0-9a-f\\-]+/call/"
			       "flows/[0-9a-f\\-]+/local_candidates",
			       &pl_convid, &pl_flowid)) {

		char convid[37], flowid[37];
		struct device *target;

		++dev->n_put_localcand;

		pl_strcpy(&pl_convid, convid, sizeof(convid));
		pl_strcpy(&pl_flowid, flowid, sizeof(flowid));

		target = find_device_flow(test, dev, flowid);
		if (!target) {
			warning("[ %s ] target not found\n", dev->name);
			return ENOENT;
		}

		err = relay_candidates(dev, target, convid, flowid,
				       content, clen);
		if (err)
			return err;

		err = flowmgr_resp(dev->fm, 200, "OK", NULL, NULL, 0, ctx);
	}
	else if (streq(method, "GET") && streq(path, "/calls/config")) {

		char *config;

		if (test->use_stun) {

			err = mk_calls_config(&config, test);
			if (err)
				return err;

			err = flowmgr_resp(dev->fm, 200, "OK",
					   "application/json",
					   config, strlen(config), ctx);

			mem_deref(config);
		}
		else {
			err = flowmgr_resp(dev->fm, 404, "Not Found",
					   NULL, NULL, 0, ctx);
		}

	}
	else {
		warning("test: method/path not found (%s %s)\n", method, path);
		err = flowmgr_resp(dev->fm, 404, "Not Found",
				   NULL, NULL, 0, ctx);
	}

	return err;
}


static bool are_all_established(const struct device *dev)
{
	struct test *test = dev->test;
	struct le *le;

	if (0 == list_count(&test->devicel))
		return false;

	for (le = test->devicel.head; le; le = le->next) {
		struct device *dev = (struct device *)le->data;

		if (dev->expect_estab && !dev->estab)
			return false;
	}

	return true;
}


static void mcat_handler(const char *convid, enum flowmgr_mcat cat, void *arg)
{
	struct device *dev = (struct device *)arg;

	ASSERT_STREQ(dev->test->convid, convid);

	if (cat > FLOWMGR_MCAT_NORMAL) {

		info("[ %s ] mediacat -- established\n", dev->name);

		dev->estab = true;
	}

	if (are_all_established(dev)) {
		info("@@@ ALL DEVICES ARE ESTABLISHED !!! @@@\n");
		re_cancel();
	}
}


static void flowmgr_err_handler(int err, const char *convid, void *arg)
{
	struct device *dev = (struct device *)arg;
	(void)dev;

	warning("flowmgr error handler: %m\n", err);

	dev->test->err = err ? err : ENOSYS;

	re_cancel();
}


static void destructor(void *arg)
{
	struct device *dev = (struct device *)arg;

	list_unlink(&dev->le);

	mem_deref(dev->fm);
}


static int device_alloc(struct device **devp, struct test *test,
			const char *userid, const char *name)
{
	struct device *dev;
	int err;

	dev = (struct device *)mem_zalloc(sizeof(*dev), destructor);
	if (!dev)
		return ENOMEM;

	dev->test = test;

	err = flowmgr_alloc(&dev->fm, flowmgr_req_handler, flowmgr_err_handler,
			    dev);
	if (err)
		goto out;

	flowmgr_set_media_handlers(dev->fm, mcat_handler, NULL, dev);
	str_ncpy(dev->name, name, sizeof(dev->name));

	flowmgr_set_self_userid(dev->fm, userid);
	str_ncpy(dev->userid, userid, sizeof(dev->userid));

	list_append(&test->devicel, &dev->le, dev);

	dev->expect_estab = true;  /* on by default */

 out:
	if (err)
		mem_deref(dev);
	else
		*devp = dev;

	return err;
}


class flowmgr_b2b : public ::testing::Test {

public:
	virtual void SetUp() override
	{
#if 1
		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);
#endif

		list_init(&test.devicel);

		err = flowmgr_init(FM_MSYS, NULL, CERT_TYPE_ECDSA);
		ASSERT_EQ(0, err);

		flowmgr_enable_loopback(true);

		/* conversation between A and B*/
		str_ncpy(test.convid, "cccccccc-0000-0000-0000-000000000000",
			 sizeof(test.convid));
	}

	virtual void TearDown() override
	{
		flowmgr_close();
	}

	void basic_test(bool send_flowadd, bool use_stun);
	void multi_device_test(bool send_flowadd);
	void group_test(bool send_flowadd, bool use_stun);

protected:
	struct test test;
	int err = 0;
};


void flowmgr_b2b::basic_test(bool send_flowadd, bool use_stun)
{
	struct device *a, *b;

	test.send_flowadd = send_flowadd;
	test.use_stun = use_stun;

	err |= device_alloc(&a, &test,
			    "aaaaaaaa-0000-0000-0000-000000000000", "A");
	err |= device_alloc(&b, &test,
			    "bbbbbbbb-0000-0000-0000-000000000000", "B");
	ASSERT_EQ(0, err);

#if 0
	flowmgr_enable_trace(a->fm, 2);
	flowmgr_enable_trace(b->fm, 2);
#endif

	if (use_stun) {
		err  = flowmgr_config_start(a->fm);
		err |= flowmgr_config_start(b->fm);
		ASSERT_EQ(0, err);
	}

	/* connect A and B */
	flowmgr_user_add(a->fm, test.convid, b->userid, b->name);
	flowmgr_user_add(b->fm, test.convid, a->userid, a->name);

	ASSERT_EQ(1, flowmgr_users_count(a->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b->fm, test.convid));

	/* Make an outgoing call from Device A */
	err = flowmgr_acquire_flows(a->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* Accept the incoming call from Device B */
	err = flowmgr_acquire_flows(b->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* WAIT */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

	/* check for async errors */
	if (test.stun_server.force_error) {
		ASSERT_EQ(EPROTO, test.err);
		goto out;
	}
	else {
		ASSERT_EQ(0, test.err);
	}

	/* verify results after traffic is complete */
	if (send_flowadd) {

		ASSERT_EQ(1, a->n_post_flows);
		ASSERT_EQ(0, b->n_post_flows);
		ASSERT_EQ(0, a->n_put_localsdp);
		ASSERT_EQ(1, b->n_put_localsdp);
	}
	else {
		ASSERT_EQ(1, a->n_post_flows);
		ASSERT_EQ(1, b->n_post_flows);
		ASSERT_EQ(1, a->n_put_localsdp);
		ASSERT_EQ(0, b->n_put_localsdp);
	}

	ASSERT_EQ(0, a->n_put_localcand);
	ASSERT_EQ(0, b->n_put_localcand);

	ASSERT_TRUE(a->estab);
	ASSERT_TRUE(b->estab);

	ASSERT_EQ(1, flowmgr_users_count(a->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b->fm, test.convid));

	/* check if the STUN-server was used or not */
	if (use_stun) {
		ASSERT_GE(test.stun_server.nrecv, 2);
	}
	else {
		ASSERT_EQ(0, test.stun_server.nrecv);
	}

 out:
	flowmgr_release_flows(a->fm, test.convid);
	flowmgr_release_flows(b->fm, test.convid);

	mem_deref(a);
	mem_deref(b);
}


// XXX: this testcase is a bit broken..
void flowmgr_b2b::multi_device_test(bool send_flowadd)
{
	struct device *a, *b1, *b2;

	test.send_flowadd = send_flowadd;

	err |= device_alloc(&a, &test,
			    "aaaaaaaa-0000-0000-0000-000000000000", "A");
	err |= device_alloc(&b1, &test,
			    "bbbbbbbb-1111-0000-0000-000000000000", "B1");
	err |= device_alloc(&b2, &test,
			    "bbbbbbbb-2222-0000-0000-000000000000", "B2");
	ASSERT_EQ(0, err);

	b2->expect_estab = false;
	
	/* connect users A and B */
	flowmgr_user_add(a->fm, test.convid, b1->userid, b1->name);
	flowmgr_user_add(a->fm, test.convid, b2->userid, b2->name);

	flowmgr_user_add(b1->fm, test.convid, a->userid, a->name);

	flowmgr_user_add(b2->fm, test.convid, a->userid, a->name);

	ASSERT_EQ(2, flowmgr_users_count(a->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b1->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b2->fm, test.convid));

	/* Make an outgoing call from Device A */
	err = flowmgr_acquire_flows(a->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* Accept the incoming call from Device B1 */
	err = flowmgr_acquire_flows(b1->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* WAIT */
	err = re_main_wait(5000);
	ASSERT_EQ(0, err);

#if 0
	re_printf("*** FLOWMGR A *****\n");
	re_printf("%H\n", flowmgr_debug, a->fm);
	re_printf("\n");

	re_printf("*** FLOWMGR B1 *****\n");
	re_printf("%H\n", flowmgr_debug, b1->fm);
	re_printf("\n");

	re_printf("*** FLOWMGR B2 *****\n");
	re_printf("%H\n", flowmgr_debug, b2->fm);
	re_printf("\n");
#endif

	/* check for sync/async errors */
	ASSERT_EQ(0, test.err);

	/* verify results after traffic is complete */

	if (send_flowadd) {

		ASSERT_EQ(1, a->n_post_flows);
		ASSERT_EQ(0, a->n_put_localsdp);

		ASSERT_EQ(0, b1->n_post_flows);
		ASSERT_EQ(1, b1->n_put_localsdp);
	}
	else {
		ASSERT_EQ(1, a->n_post_flows);
		ASSERT_EQ(1, a->n_put_localsdp);

		ASSERT_EQ(1, b1->n_post_flows);
		ASSERT_EQ(0, b1->n_put_localsdp);
	}

	ASSERT_EQ(0, b2->n_post_flows);
	ASSERT_EQ(0, b2->n_put_localsdp);

	ASSERT_TRUE(a->estab);
	ASSERT_TRUE(b1->estab);
	ASSERT_FALSE(b2->estab);

	ASSERT_EQ(2, flowmgr_users_count(a->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b1->fm, test.convid));
	ASSERT_EQ(1, flowmgr_users_count(b2->fm, test.convid));

	flowmgr_release_flows(a->fm, test.convid);
	flowmgr_release_flows(b1->fm, test.convid);
	flowmgr_release_flows(b2->fm, test.convid);

	mem_deref(a);
	mem_deref(b1);
	mem_deref(b2);
}


TEST_F(flowmgr_b2b, b2b_send_flowadd)
{
	basic_test(true, false);
}


TEST_F(flowmgr_b2b, b2b_dont_send_flowadd)
{
	basic_test(false, false);
}


TEST_F(flowmgr_b2b, b2b_enable_stun)
{
	basic_test(false, true /* STUN */);
}



TEST_F(flowmgr_b2b, b2b_enable_stun_forced_error)
{
#if 1
	log_enable_stderr(false);
#endif


	test.stun_server.force_error = true;

	basic_test(false, true /* STUN */);
}


/*
 * expected flow from A to B1
 */
#if 0 /* Seems test is broken... */
TEST_F(flowmgr_b2b, b2b_multi_device)
{
	//multi_device_test(false);
	multi_device_test(true);       
}
#endif

void flowmgr_b2b::group_test(bool send_flowadd, bool use_stun)
{
#define NUM_DEVICES 3
#define NUM_FLOWS 2
	struct device *a=0, *b=0, *c=0;

	test.send_flowadd = send_flowadd;
	test.use_stun = use_stun;

	err |= device_alloc(&a, &test,
			    "aaaaaaaa-0000-0000-0000-000000000000", "A");
	err |= device_alloc(&b, &test,
			    "bbbbbbbb-0000-0000-0000-000000000000", "B");
	err |= device_alloc(&c, &test,
			    "cccccccc-0000-0000-0000-000000000000", "C");
	ASSERT_EQ(0, err);

#if 0
	flowmgr_enable_trace(a->fm, 2);
#endif

	if (use_stun) {
		err  = flowmgr_start();
		ASSERT_EQ(0, err);
	}

	/* connect A and B and C */
	flowmgr_user_add(a->fm, test.convid, b->userid, b->name);
	flowmgr_user_add(a->fm, test.convid, c->userid, c->name);

	flowmgr_user_add(b->fm, test.convid, a->userid, a->name);
	flowmgr_user_add(b->fm, test.convid, c->userid, c->name);

	flowmgr_user_add(c->fm, test.convid, a->userid, a->name);
	flowmgr_user_add(c->fm, test.convid, b->userid, b->name);

	/* Make an outgoing call from Device A */
	err = flowmgr_acquire_flows(a->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* Make an outgoing call from Device B */
	err = flowmgr_acquire_flows(b->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* Make an outgoing call from Device C */
	err = flowmgr_acquire_flows(c->fm, test.convid, NULL, NULL, NULL);
	ASSERT_EQ(0, err);

	/* WAIT */
	err = re_main_wait(5000);

#if 0
	re_printf("*** FLOWMGR A *****\n");
	re_printf("%H\n", flowmgr_debug, a->fm);
	re_printf("\n");
#endif

	ASSERT_EQ(0, err);

	/* verify results after traffic is complete */
	ASSERT_EQ(0, test.err);
	ASSERT_TRUE(a->estab);
	ASSERT_TRUE(b->estab);
	ASSERT_TRUE(c->estab);

	/* verify that number of flows */
	ASSERT_EQ(NUM_FLOWS,
		  call_count_flows(flowmgr_call(a->fm, test.convid)));
	ASSERT_EQ(NUM_FLOWS,
		  call_count_flows(flowmgr_call(b->fm, test.convid)));
	ASSERT_EQ(NUM_FLOWS,
		  call_count_flows(flowmgr_call(c->fm, test.convid)));

	/* check if the STUN-server was used or not */
	if (use_stun) {
		ASSERT_GE(test.stun_server.nrecv, 2);
	}
	else {
		ASSERT_EQ(0, test.stun_server.nrecv);
	}

	flowmgr_release_flows(a->fm, test.convid);
	flowmgr_release_flows(b->fm, test.convid);
	flowmgr_release_flows(c->fm, test.convid);

	mem_deref(a);
	mem_deref(b);
	mem_deref(c);
}


TEST_F(flowmgr_b2b, b2b_group)
{
	group_test(true, false);
}


TEST_F(flowmgr_b2b, b2b_group_dont_send_flowadd)
{
	group_test(false, false);
}


// XXX: This testcase is failing
#if 1
TEST_F(flowmgr_b2b, b2b_group_with_stun)
{
	group_test(true, true);
}
#endif
