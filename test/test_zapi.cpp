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


TEST(zapi, flows_with_iceservers)
{
	struct json_object *jobj;
	int err;

	static const struct zapi_flow dummy_flows[] = {
		{true,
		 "ff000001",
		 {
			 {"turn:127.0.0.1", "username", "password"},
			 {"turn:127.0.0.1", "username", "password"},
		 },
		 2,
		 "pending",
		},
		{true,
		 "ff000002",
		 {
			 {"turn:127.0.0.1", "username", "password"},
			 {"turn:127.0.0.1", "username", "password"},
		 },
		 2,
		 "pending",
		},

	};

	struct zapi_flow flowv[16];
	size_t flowc = ARRAY_SIZE(flowv);

	memset(flowv, 0, sizeof(flowv));

	jobj = json_object_new_object();

	err = zapi_flows_encode(jobj, dummy_flows, ARRAY_SIZE(dummy_flows));
	ASSERT_EQ(0, err);

#if 0
	jzon_dump(jobj);
#endif

	err = zapi_flows_decode(jobj, flowv, &flowc);
	ASSERT_EQ(0, err);
	ASSERT_EQ(2, flowc);

	ASSERT_TRUE(0 == memcmp(dummy_flows, flowv, sizeof(dummy_flows)));

	mem_deref(jobj);
}


TEST(zapi, flows_no_iceservers)
{
	struct json_object *jobj;
	int err;

	static const struct zapi_flow dummy_flows[] = {
		{true,
		 "ff000001",
		 {
		 },
		 0
		},
		{true,
		 "ff000002",
		 {
		 },
		 0
		},

	};

	struct zapi_flow flowv[16];
	size_t flowc = ARRAY_SIZE(flowv);

	memset(flowv, 0, sizeof(flowv));

	jobj = json_object_new_object();

	err = zapi_flows_encode(jobj, dummy_flows, ARRAY_SIZE(dummy_flows));
	ASSERT_EQ(0, err);

#if 0
	jzon_dump(jobj);
#endif

	err = zapi_flows_decode(jobj, flowv, &flowc);
	ASSERT_EQ(0, err);
	ASSERT_EQ(2, flowc);

	ASSERT_TRUE(0 == memcmp(dummy_flows, flowv, sizeof(dummy_flows)));

	mem_deref(jobj);
}


TEST(zapi, local_sdp)
{
	const char *msg = "v=0\r\no=- 177043771 1811780385 IN IP4 192.168.10.69\r\ns=-\r\nc=IN IP4 192.168.10.69\r\nt=0 0\r\na=ice-options:trickle\r\na=OFFER\r\nm=audio 21960 RTP/SAVPF 96 0 8\r\na=rtpmap:96 opus/48000/2\r\na=fmtp:96 stereo=0;sprop-stereo=0\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\na=rtcp:21961\r\na=sendrecv\r\na=rtcp-mux\r\na=ice-ufrag:w0KwpCImvqEEO6I\r\na=ice-pwd:XyEeYe2AY9ctgOIFCUfMk0lRJ468y7d\r\na=fingerprint:sha-1 5d:d1:13:5d:bb:72:f3:34:6d:db:bd:2f:e1:c5:72:bf:1a:e5:eb:ad\r\na=setup:actpass\r\na=candidate:1 1 udp 2113929471 192.168.10.69 21960 typ host\r\n";
	struct json_object *jobj;
	struct zapi_local_sdp b, a = {"offer", msg};
	int err;


	jobj = json_object_new_object();

	err = zapi_local_sdp_encode(jobj, &a);
	ASSERT_EQ(0, err);

	err = zapi_local_sdp_decode(&b, jobj);
	ASSERT_EQ(0, err);

	ASSERT_STREQ(a.type, b.type);
	ASSERT_STREQ(a.sdp, b.sdp);

	mem_deref(jobj);
}


TEST(zapi, remote_sdp)
{
	const char *msg = "v=0\r\no=- 177043771 1811780385 IN IP4 192.168.10.69\r\ns=-\r\nc=IN IP4 192.168.10.69\r\nt=0 0\r\na=ice-options:trickle\r\na=OFFER\r\nm=audio 21960 RTP/SAVPF 96 0 8\r\na=rtpmap:96 opus/48000/2\r\na=fmtp:96 stereo=0;sprop-stereo=0\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\na=rtcp:21961\r\na=sendrecv\r\na=rtcp-mux\r\na=ice-ufrag:w0KwpCImvqEEO6I\r\na=ice-pwd:XyEeYe2AY9ctgOIFCUfMk0lRJ468y7d\r\na=fingerprint:sha-1 5d:d1:13:5d:bb:72:f3:34:6d:db:bd:2f:e1:c5:72:bf:1a:e5:eb:ad\r\na=setup:actpass\r\na=candidate:1 1 udp 2113929471 192.168.10.69 21960 typ host\r\n";
	struct json_object *jobj;
	struct zapi_remote_sdp b, a = {"answer", msg, "688c7157-e351-4c2c-ae82-2b991172e813", "29951b76-8d4a-4da6-859b-b85eac641412"};
	int err;

	jobj = json_object_new_object();

	err = zapi_remote_sdp_encode(jobj, &a);
	ASSERT_EQ(0, err);

	err = zapi_remote_sdp_decode(&b, jobj);
	ASSERT_EQ(0, err);

	ASSERT_STREQ(a.state, b.state);
	ASSERT_STREQ(a.sdp,   b.sdp);
	ASSERT_STREQ(a.flow,  b.flow);
	ASSERT_STREQ(a.conv,  b.conv);

	mem_deref(jobj);
}


TEST(zapi, one_candidate)
{
	struct json_object *jobj;
	struct zapi_candidate b, a = {"audio", 0, "a=candidate:1 1 udp 1677721855 62.96.148.44 21960 typ srflx raddr 0.0.0.0 rport 21960\r\n"};
	int err;

	jobj = json_object_new_object();

	err = zapi_candidate_encode(jobj, &a);
	ASSERT_EQ(0, err);

	err = zapi_candidate_decode(&b, jobj);
	ASSERT_EQ(0, err);

	ASSERT_STREQ(a.mid,      b.mid);
	ASSERT_EQ(a.mline_index, b.mline_index);
	ASSERT_STREQ(a.sdp,      b.sdp);

	mem_deref(jobj);
}


TEST(zapi, call_state)
{
	struct json_object *jobj;
	struct zapi_call_state b, a = {"joined", .5};
	int err;

	jobj = json_object_new_object();

	err = zapi_call_state_encode(jobj, &a);
	ASSERT_EQ(0, err);

	err = zapi_call_state_decode(&b, jobj);
	ASSERT_EQ(0, err);

	ASSERT_STREQ(a.state, b.state);
	ASSERT_EQ(a.quality,  b.quality);

	mem_deref(jobj);
}


TEST(zapi, call_state_event)
{
	const char *msg =
"{"
"  \"conversation\":\"193ac375-d7f0-4a0e-a237-e409297c7c9f\","
"  \"cause\":\"requested\","
"  \"participants\":{"
"    \"7e8d7b27-7fc9-4e96-b94c-db5277b96620\":{"
"      \"state\":\"idle\""
"    },"
"    \"9aad484e-5827-4b78-a8eb-08b7b1c3167f\":{"
"      \"state\":\"joined\""
"    }"
"  },"
"  \"self\":null,"
"  \"sequence\":157,"
"  \"type\":\"call.state\","
"  \"session\":\"6510eff0-7cc9-4cb4-a203-7a1b1b6db1f6\""
"}";
	struct zapi_call_state_event cs;
	struct json_object *jobj = NULL;
	int err;

	err = jzon_decode(&jobj, msg, str_len(msg));
	ASSERT_EQ(0, err);

	err = zapi_call_state_event_decode(&cs, jobj);
	ASSERT_EQ(0, err);

	ASSERT_STREQ("193ac375-d7f0-4a0e-a237-e409297c7c9f", cs.convid);
	ASSERT_STREQ("requested", cs.cause);
	ASSERT_EQ(2, cs.participantc);
	ASSERT_STREQ("7e8d7b27-7fc9-4e96-b94c-db5277b96620",
		     cs.participantv[0].userid);
	ASSERT_STREQ("idle", cs.participantv[0].state);
	ASSERT_STREQ("9aad484e-5827-4b78-a8eb-08b7b1c3167f",
		     cs.participantv[1].userid);
	ASSERT_STREQ("joined", cs.participantv[1].state);
	
	ASSERT_STREQ("", cs.self.reason);
	ASSERT_STREQ("", cs.self.state);
	ASSERT_EQ(157, cs.sequence);
	ASSERT_STREQ("call.state", cs.type);
	ASSERT_STREQ("6510eff0-7cc9-4cb4-a203-7a1b1b6db1f6", cs.session);

	mem_deref(jobj);
}


TEST(zapi, prekeys)
{
	struct zapi_prekey out, in = {
		{1,2,3,4},
		4,
		42
	};
	struct json_object *jobj;
	int err;

	jobj = json_object_new_object();

	err = zapi_prekey_encode(jobj, &in);
	ASSERT_EQ(0, err);

	err = zapi_prekey_decode(&out, jobj);
	ASSERT_EQ(0, err);

	ASSERT_EQ(in.key_len, out.key_len);
	ASSERT_TRUE(0 == memcmp(in.key, out.key, in.key_len));
	ASSERT_EQ(in.id, out.id);

	mem_deref(jobj);
}
