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

/* Basic tests for enconn_messages:
 * build a message
 * serialize and deserialize
 * compare source and dest message values
 */

#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>

#define TIME_MSG 12340
#define TIME_NOW 12345

static struct econn_message *init_message(enum econn_msg type)
{
	struct econn_message *msg = NULL;

	msg = econn_message_alloc();
	econn_message_init(msg, type, "sender");
	str_ncpy(msg->src_userid, "src_userid", ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, "src_clientid", ECONN_ID_LEN);
	str_ncpy(msg->dest_userid, "dest_userid", ECONN_ID_LEN);
	str_ncpy(msg->dest_clientid, "dest_clientid", ECONN_ID_LEN);
	msg->resp = true;

	return msg;
}

static void check_message(const struct econn_message *a,
			  const struct econn_message *b)
{

	ASSERT_EQ(a->msg_type, b->msg_type);
	ASSERT_STREQ(a->sessid_sender, b->sessid_sender);
	ASSERT_STREQ(a->src_userid, b->src_userid);
	ASSERT_STREQ(a->src_clientid, b->src_clientid);
	ASSERT_STREQ(a->dest_userid, b->dest_userid);
	ASSERT_STREQ(a->dest_clientid, b->dest_clientid);
	ASSERT_EQ(a->resp, b->resp);
	ASSERT_EQ(b->time, TIME_MSG);
	ASSERT_EQ(b->age, TIME_NOW - TIME_MSG);
}

static int init_props(struct econn_props **pprops)
{
	int err = 0;

	err = econn_props_alloc(pprops, NULL);
	if (err)
		return err;

	err = econn_props_add(*pprops, "prop1", "value1");
	if (err)
		return err;

	err = econn_props_add(*pprops, "prop2", "value2");
	if (err)
		return err;

	return err;
}

static void check_props(struct econn_props *a,
		       struct econn_props *b)
{
	ASSERT_STREQ(econn_props_get(a, "prop1"),
		     econn_props_get(b, "prop1"));

	ASSERT_STREQ(econn_props_get(a, "prop2"),
		     econn_props_get(b, "prop2"));
}

static void init_stringlist(struct list *l)
{
	stringlist_append(l, "listval1");
	stringlist_append(l, "listval2");
}

static void check_stringlist(struct list *a,
			     struct list *b)
{
	struct le *ale, *ble;

	ASSERT_EQ(list_count(a), list_count(b));

	ale = a->head;
	ble = b->head;

	while (ale && ble) {
		struct stringlist_info *astr = (struct stringlist_info*)ale->data;
		struct stringlist_info *bstr = (struct stringlist_info*)ble->data;
		ASSERT_STREQ(astr->str, bstr->str);
		ale = ale->next;
		ble = ble->next;
	}
}

static void partlist_add(struct list *l,
			 const char *uid,
			 const char *cid,
			 uint32_t ssrca,
			 uint32_t ssrcv)
{
	struct econn_group_part *part; 

	part = econn_part_alloc(uid, cid);
	ASSERT_TRUE(part != NULL);
	part->authorized = true;
	part->muted_state = MUTED_STATE_UNMUTED;
	part->ssrca = ssrca;
	part->ssrcv = ssrcv;
	part->ts = TIME_MSG;
	
	list_append(l, &part->le, part);
}

static void init_partlist(struct list *l)
{
	partlist_add(l ,"user1", "client1", 1001, 2001);
	partlist_add(l ,"user2", "client2", 1002, 2002);
}

static void check_partlist(struct list *a, struct list *b)
{
	struct le *ale, *ble;

	ASSERT_EQ(list_count(a), list_count(b));

	ale = a->head;
	ble = b->head;

	while (ale && ble) {
		struct econn_group_part *apart = (struct econn_group_part*)ale->data;
		struct econn_group_part *bpart = (struct econn_group_part*)ble->data;
		ASSERT_STREQ(apart->userid, bpart->userid);
		ASSERT_STREQ(apart->clientid, bpart->clientid);
		ASSERT_EQ(apart->authorized, bpart->authorized);
		ASSERT_EQ(apart->muted_state, bpart->muted_state);
		ASSERT_EQ(apart->ssrca, bpart->ssrca);
		ASSERT_EQ(apart->ssrcv, bpart->ssrcv);
		ASSERT_EQ(apart->ts, bpart->ts);

		ale = ale->next;
		ble = ble->next;
	}
}

static void keylist_add(struct list *l, uint32_t idx)
{
	struct econn_key_info *kinfo = NULL;
	const char *keydata = "KEYDATA_KEYDATA_KEYDATA_KEYDATA";
	const uint32_t keylen = strlen(keydata) + 1;

	kinfo = econn_key_info_alloc(keylen);
	memcpy(kinfo->data, keydata, keylen);
	kinfo->idx = idx;
	list_append(l, &kinfo->le, kinfo);
}

static void init_keylist(struct list *l)
{
	keylist_add(l, 1);
	keylist_add(l, 2);
	keylist_add(l, 3);
}

static void check_keylist(struct list *a, struct list *b)
{
	struct le *ale, *ble;

	ASSERT_EQ(list_count(a), list_count(b));

	ale = a->head;
	ble = b->head;

	while (ale && ble) {
		struct econn_key_info *ainfo = (struct econn_key_info*)ale->data;
		struct econn_key_info *binfo = (struct econn_key_info*)ble->data;
		ASSERT_EQ(ainfo->idx, binfo->idx);
		ASSERT_EQ(ainfo->dlen, binfo->dlen);
		ASSERT_EQ(memcmp(ainfo->data, binfo->data, ainfo->dlen), 0);

		ale = ale->next;
		ble = ble->next;
	}
}

static void init_streamlist(struct list *l)
{
	struct econn_stream_info *sinfo;
	
	sinfo = econn_stream_info_alloc("user1", 0);
	ASSERT_TRUE(sinfo != NULL);
	list_append(l, &sinfo->le, sinfo);

	sinfo = econn_stream_info_alloc("user2", 0);
	ASSERT_TRUE(sinfo != NULL);
	list_append(l, &sinfo->le, sinfo);
}

static void check_streamlist(struct list *a, struct list *b)
{
	struct le *ale, *ble;

	ASSERT_EQ(list_count(a), list_count(b));

	ale = a->head;
	ble = b->head;

	while (ale && ble) {
		struct econn_stream_info *ainfo = (struct econn_stream_info*)ale->data;
		struct econn_stream_info *binfo = (struct econn_stream_info*)ble->data;
		ASSERT_STREQ(ainfo->userid, binfo->userid);
		ASSERT_EQ(ainfo->quality, binfo->quality);

		ale = ale->next;
		ble = ble->next;
	}
}

static void init_ice_serverlist(struct zapi_ice_server **pturnv, size_t *pturnc)
{
	struct zapi_ice_server *srvs = NULL;
	size_t c, count = 4;

	*pturnc = 0;

	srvs = (struct zapi_ice_server*)mem_zalloc(sizeof(struct zapi_ice_server) * count, NULL);
	ASSERT_TRUE(srvs != NULL);

	for (c = 0; c < count; c++) {
		snprintf(srvs[c].url, 255, "server_%zu", c);
		snprintf(srvs[c].username, 127, "user_%zu", c);
		snprintf(srvs[c].credential, 127, "cred_%zu", c);
	}

	*pturnc = count;
	*pturnv = srvs;
}

static void check_ice_serverlist(struct zapi_ice_server *a,
				 struct zapi_ice_server *b,
				 size_t count)
{
	size_t c;

	for (c = 0; c < count; c++) {
		ASSERT_STREQ(a[c].url, b[c].url);
		ASSERT_STREQ(a[c].username, b[c].username);
		ASSERT_STREQ(a[c].credential, b[c].credential);
	}
}

static void encode_decode(struct econn_message *smsg,
			  struct econn_message **pdmsg)
{
	char *mstr = NULL;
	int err = 0;

	err = econn_message_encode(&mstr, smsg);
	ASSERT_EQ(err, 0);
	ASSERT_TRUE(mstr != NULL);

	err = econn_message_decode(pdmsg, TIME_NOW, TIME_MSG, mstr, strlen(mstr));
	ASSERT_EQ(err, 0);
	ASSERT_TRUE(*pdmsg != NULL);

	check_message(smsg, *pdmsg);

	mem_deref(mstr);
}

TEST(econn_fmt, econn_setup)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_SETUP);
	ASSERT_TRUE(smsg != NULL);

	err = str_dup(&smsg->u.setup.sdp_msg, "sdp contents");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.setup.url, "url");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.setup.sft_tuple, "sft tuple");
	ASSERT_EQ(err, 0);
	err = init_props(&smsg->u.setup.props);
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	ASSERT_STREQ(smsg->u.setup.sdp_msg, dmsg->u.setup.sdp_msg);
	ASSERT_STREQ(smsg->u.setup.url, dmsg->u.setup.url);
	ASSERT_STREQ(smsg->u.setup.sft_tuple, dmsg->u.setup.sft_tuple);
	check_props(smsg->u.setup.props, dmsg->u.setup.props);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_update)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_UPDATE);
	ASSERT_TRUE(smsg != NULL);

	err = str_dup(&smsg->u.setup.sdp_msg, "sdp contents");
	ASSERT_EQ(err, 0);

	err = init_props(&smsg->u.setup.props);
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	ASSERT_STREQ(smsg->u.setup.sdp_msg, dmsg->u.setup.sdp_msg);
	ASSERT_STREQ(smsg->u.setup.url, dmsg->u.setup.url);
	ASSERT_STREQ(smsg->u.setup.sft_tuple, dmsg->u.setup.sft_tuple);
	check_props(smsg->u.setup.props, dmsg->u.setup.props);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_cancel)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CANCEL);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_hangup)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CANCEL);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_reject)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_REJECT);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_ping)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_PING);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_propsync)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_PROPSYNC);
	ASSERT_TRUE(smsg != NULL);

	err = init_props(&smsg->u.propsync.props);
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	check_props(smsg->u.propsync.props, dmsg->u.propsync.props);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_groupstart)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_GROUP_START);
	ASSERT_TRUE(smsg != NULL);

	err = init_props(&smsg->u.groupstart.props);
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	check_props(smsg->u.groupstart.props, dmsg->u.groupstart.props);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_groupleave)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_GROUP_LEAVE);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_groupcheck)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_GROUP_CHECK);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_groupsetup)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_GROUP_SETUP);
	ASSERT_TRUE(smsg != NULL);

	err = str_dup(&smsg->u.setup.sdp_msg, "sdp contents");
	ASSERT_EQ(err, 0);

	err = init_props(&smsg->u.setup.props);
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	ASSERT_STREQ(smsg->u.setup.sdp_msg, dmsg->u.setup.sdp_msg);
	check_props(smsg->u.setup.props, dmsg->u.setup.props);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confconn)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CONF_CONN);
	ASSERT_TRUE(smsg != NULL);

	init_ice_serverlist(&smsg->u.confconn.turnv, &smsg->u.confconn.turnc);
	smsg->u.confconn.update = true;
	err = str_dup(&smsg->u.confconn.tool, "tool_str");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.confconn.toolver, "tool_ver");
	ASSERT_EQ(err, 0);
	smsg->u.confconn.status = ECONN_CONFCONN_REJECTED_BLACKLIST;;
	smsg->u.confconn.selective_audio = true;
	smsg->u.confconn.selective_video = true;
	smsg->u.confconn.vstreams = 12;
	err = str_dup(&smsg->u.confconn.sft_url, "sft_url");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.confconn.sft_tuple, "sft_tuple");
	ASSERT_EQ(err, 0);

	encode_decode(smsg, &dmsg);

	ASSERT_EQ(smsg->u.confconn.turnc, dmsg->u.confconn.turnc);
	check_ice_serverlist(smsg->u.confconn.turnv,
			     dmsg->u.confconn.turnv,
			     smsg->u.confconn.turnc);
	ASSERT_EQ(smsg->u.confconn.update, dmsg->u.confconn.update);
	ASSERT_STREQ(smsg->u.confconn.tool, dmsg->u.confconn.tool);
	ASSERT_STREQ(smsg->u.confconn.toolver, dmsg->u.confconn.toolver);
	ASSERT_EQ(smsg->u.confconn.status, dmsg->u.confconn.status);
	ASSERT_EQ(smsg->u.confconn.selective_audio, dmsg->u.confconn.selective_audio);
	ASSERT_EQ(smsg->u.confconn.selective_video, dmsg->u.confconn.selective_video);
	ASSERT_EQ(smsg->u.confconn.vstreams, dmsg->u.confconn.vstreams);
	ASSERT_STREQ(smsg->u.confconn.sft_url, dmsg->u.confconn.sft_url);
	ASSERT_STREQ(smsg->u.confconn.sft_tuple, dmsg->u.confconn.sft_tuple);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confstart)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CONF_START);
	ASSERT_TRUE(smsg != NULL);

	err = init_props(&smsg->u.confstart.props);
	ASSERT_EQ(err, 0);

	err = str_dup(&smsg->u.confstart.sft_url, "sft_url");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.confstart.sft_tuple, "sft_tuple");
	ASSERT_EQ(err, 0);
	err = str_dup((char**)(&smsg->u.confstart.secret), "secret");
	ASSERT_EQ(err, 0);
	smsg->u.confstart.secretlen = 7;
	smsg->u.confstart.timestamp = 12345;
	smsg->u.confstart.seqno = 24680;
	init_stringlist(&smsg->u.confstart.sftl);

	encode_decode(smsg, &dmsg);

	check_props(smsg->u.confstart.props, dmsg->u.confstart.props);
	ASSERT_STREQ(smsg->u.confstart.sft_url, dmsg->u.confstart.sft_url);
	ASSERT_STREQ(smsg->u.confstart.sft_tuple, dmsg->u.confstart.sft_tuple);
	ASSERT_EQ(smsg->u.confstart.secretlen, dmsg->u.confstart.secretlen);
	ASSERT_EQ(memcmp(smsg->u.confstart.secret,
			 dmsg->u.confstart.secret,
			 smsg->u.confstart.secretlen), 0);
	ASSERT_EQ(smsg->u.confstart.timestamp, dmsg->u.confstart.timestamp);
	ASSERT_EQ(smsg->u.confstart.seqno, dmsg->u.confstart.seqno);
	check_stringlist(&smsg->u.confstart.sftl, &dmsg->u.confstart.sftl);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confcheck)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;
	const char *secret = "SECRETsecret";

	smsg = init_message(ECONN_CONF_CHECK);
	ASSERT_TRUE(smsg != NULL);

	err = str_dup(&smsg->u.confcheck.sft_url, "sft_url");
	ASSERT_EQ(err, 0);
	err = str_dup(&smsg->u.confcheck.sft_tuple, "sft_tuple");
	ASSERT_EQ(err, 0);
	err = str_dup((char**)(&smsg->u.confcheck.secret), secret);
	ASSERT_EQ(err, 0);
	smsg->u.confcheck.secretlen = strlen(secret)+1;
	smsg->u.confcheck.timestamp = 12345;
	smsg->u.confcheck.seqno = 24680;
	init_stringlist(&smsg->u.confcheck.sftl);

	encode_decode(smsg, &dmsg);

	ASSERT_STREQ(smsg->u.confcheck.sft_url, dmsg->u.confcheck.sft_url);
	ASSERT_STREQ(smsg->u.confcheck.sft_tuple, dmsg->u.confcheck.sft_tuple);
	ASSERT_EQ(smsg->u.confcheck.secretlen, dmsg->u.confcheck.secretlen);
	ASSERT_EQ(memcmp(smsg->u.confcheck.secret,
			 dmsg->u.confcheck.secret,
			 smsg->u.confcheck.secretlen), 0);
	ASSERT_EQ(smsg->u.confcheck.timestamp, dmsg->u.confcheck.timestamp);
	ASSERT_EQ(smsg->u.confcheck.seqno, dmsg->u.confcheck.seqno);
	check_stringlist(&smsg->u.confcheck.sftl, &dmsg->u.confcheck.sftl);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confend)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CONF_END);
	ASSERT_TRUE(smsg != NULL);

	encode_decode(smsg, &dmsg);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confpart)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;
	const char *entropy = "ENTROPY";

	smsg = init_message(ECONN_CONF_PART);
	ASSERT_TRUE(smsg != NULL);

	smsg->u.confpart.timestamp = 12345;
	smsg->u.confpart.seqno = 24680;
	smsg->u.confpart.should_start = true;
	err = str_dup((char**)(&smsg->u.confpart.entropy), entropy);
	ASSERT_EQ(err, 0);
	smsg->u.confpart.entropylen = strlen(entropy)+1;
	init_stringlist(&smsg->u.confpart.sftl);
	init_partlist(&smsg->u.confpart.partl);

	encode_decode(smsg, &dmsg);

	ASSERT_EQ(smsg->u.confpart.timestamp, dmsg->u.confpart.timestamp);
	ASSERT_EQ(smsg->u.confpart.seqno, dmsg->u.confpart.seqno);
	ASSERT_EQ(smsg->u.confpart.entropylen, dmsg->u.confpart.entropylen);
	ASSERT_EQ(memcmp(smsg->u.confpart.entropy,
			 dmsg->u.confpart.entropy,
			 smsg->u.confpart.entropylen), 0);
	check_stringlist(&smsg->u.confpart.sftl, &dmsg->u.confpart.sftl);
	check_partlist(&smsg->u.confpart.partl, &dmsg->u.confpart.partl);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confkey)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;
	const char *keydata = "KEYDATA_KEYDATA_KEYDATA_KEYDATA";

	smsg = init_message(ECONN_CONF_KEY);
	ASSERT_TRUE(smsg != NULL);

	init_keylist(&smsg->u.confkey.keyl);

	encode_decode(smsg, &dmsg);

	check_keylist(&smsg->u.confkey.keyl, &dmsg->u.confkey.keyl);

	mem_deref(smsg);
	mem_deref(dmsg);
}

TEST(econn_fmt, econn_confstreams)
{
	struct econn_message *smsg = NULL;
	struct econn_message *dmsg = NULL;
	int err = 0;

	smsg = init_message(ECONN_CONF_STREAMS);
	ASSERT_TRUE(smsg != NULL);

	str_dup(&smsg->u.confstreams.mode, "list");
	init_streamlist(&smsg->u.confstreams.streaml);

	encode_decode(smsg, &dmsg);

	check_streamlist(&smsg->u.confstreams.streaml, &dmsg->u.confstreams.streaml);

	mem_deref(smsg);
	mem_deref(dmsg);
}

