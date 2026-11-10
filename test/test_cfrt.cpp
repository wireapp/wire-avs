/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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
#include <string>
#include <vector>
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include "../src/pscall/cfrt.h"


static const char *pub_offer =
	"v=0\r\n"
	"o=- 1 2 IN IP4 127.0.0.1\r\n"
	"s=-\r\n"
	"t=0 0\r\n"
	"m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
	"a=mid:0\r\n"
	"a=sendonly\r\n"
	"a=ssrc:1111 cname:x\r\n"
	"m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
	"a=mid:1\r\n"
	"a=sendonly\r\n"
	"a=ssrc:2222 cname:x\r\n"
	"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
	"a=mid:2\r\n";

/* SFU offer when pulling: mids 3 (audio) and 4 (video) */
static const char *sfu_offer =
	"v=0\r\n"
	"o=- 1 2 IN IP4 127.0.0.1\r\n"
	"s=-\r\n"
	"t=0 0\r\n"
	"m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
	"a=mid:3\r\n"
	"a=sendonly\r\n"
	"a=ssrc:3333 cname:y\r\n"
	"m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
	"a=mid:4\r\n"
	"a=sendonly\r\n"
	"a=ssrc:4444 cname:y\r\n";


struct request {
	enum cfrt_leg leg;
	std::string method;
	std::string path;
	std::string body;
};

struct sdp_event {
	enum cfrt_leg leg;
	bool offer;
	bool initial;
	std::string sdp;
};


class CfrtTest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		static const struct cfrt_handlers h = {
			.httph    = http_handler,
			.sessionh = session_handler,
			.sdph     = sdp_handler,
			.tracksh  = tracks_handler,
			.dch      = dc_handler,
			.errorh   = error_handler,
		};

		ASSERT_EQ(cfrt_alloc(&rt, &h, this), 0);
	}

	virtual void TearDown() override
	{
		mem_deref(rt);
	}

	static int http_handler(enum cfrt_leg leg, const char *method,
				const char *path, const char *body, void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;
		struct request r;

		r.leg = leg;
		r.method = method;
		r.path = path;
		r.body = body ? body : "";
		t->requests.push_back(r);

		return t->http_err;
	}

	static void session_handler(enum cfrt_leg leg, const char *session_id,
				    void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;

		t->sessions.push_back(session_id);
	}

	static void sdp_handler(enum cfrt_leg leg, bool offer, bool initial,
				const char *sdp, void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;
		struct sdp_event e;

		e.leg = leg;
		e.offer = offer;
		e.initial = initial;
		e.sdp = sdp;
		t->sdps.push_back(e);
	}

	static void tracks_handler(enum cfrt_leg leg, const struct list *tracks,
				   void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;
		struct le *le;

		LIST_FOREACH(tracks, le) {
			const struct cfrt_track *tr =
				(const struct cfrt_track *)le->data;

			t->track_ssrcs.push_back(tr->ssrc);
			t->track_mids.push_back(tr->mid);
		}
	}

	static void dc_handler(enum cfrt_leg leg, const char *session_id,
			       const char *name, int id, void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;

		t->dc_ids.push_back(id);
		t->dc_sessions.push_back(session_id ? session_id : "");
	}

	static void error_handler(enum cfrt_leg leg, int err, void *arg)
	{
		CfrtTest *t = (CfrtTest *)arg;

		t->errors.push_back(err);
	}

	/* PUB session + published tracks */
	void publish()
	{
		ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_PUB, pub_offer), 0);
		ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 201,
					"{\"sessionId\":\"pub1\"}"), 0);
		ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 200,
			"{\"requiresImmediateRenegotiation\":false,"
			"\"sessionDescription\":{\"type\":\"answer\","
			"\"sdp\":\"pub-answer\"}}"), 0);
	}

	/* SUB session from our offer */
	void sub_session()
	{
		ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_SUB, "sub-offer"), 0);
		ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 201,
			"{\"sessionId\":\"sub1\","
			"\"sessionDescription\":{\"type\":\"answer\","
			"\"sdp\":\"sub-answer\"}}"), 0);
	}

	std::string sfu_pull_response()
	{
		struct json_object *jobj = jzon_alloc_object();
		struct json_object *jsd = jzon_alloc_object();
		char *s = NULL;
		std::string r;

		jzon_add_str(jsd, "type", "offer");
		jzon_add_str(jsd, "sdp", "%s", sfu_offer);
		jzon_add_bool(jobj, "requiresImmediateRenegotiation", true);
		json_object_object_add(jobj, "sessionDescription", jsd);
		r = "";
		jzon_encode(&s, jobj);
		r = s;
		mem_deref(s);
		mem_deref(jobj);

		/* add the tracks array textually */
		r.insert(r.size() - 1,
			 ",\"tracks\":["
			 "{\"sessionId\":\"pubX\",\"trackName\":\"audio\","
			 "\"mid\":\"3\"},"
			 "{\"sessionId\":\"pubX\",\"trackName\":\"video\","
			 "\"mid\":\"4\"}]");
		return r;
	}

protected:
	struct cfrt *rt = NULL;
	int http_err = 0;
	std::vector<struct request> requests;
	std::vector<std::string> sessions;
	std::vector<struct sdp_event> sdps;
	std::vector<uint32_t> track_ssrcs;
	std::vector<std::string> track_mids;
	std::vector<int> dc_ids;
	std::vector<std::string> dc_sessions;
	std::vector<int> errors;
};


TEST(cfrt_sdp, media_and_ssrc)
{
	ASSERT_EQ(cfrt_sdp_ssrc(pub_offer, "0"), 1111u);
	ASSERT_EQ(cfrt_sdp_ssrc(pub_offer, "1"), 2222u);
	ASSERT_EQ(cfrt_sdp_ssrc(pub_offer, "2"), 0u);
	ASSERT_EQ(cfrt_sdp_ssrc(pub_offer, "9"), 0u);
	ASSERT_EQ(cfrt_sdp_ssrc(sfu_offer, "4"), 4444u);
}


TEST_F(CfrtTest, publish)
{
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_PUB, pub_offer), 0);

	/* session first, without a body; tracks wait */
	ASSERT_EQ(requests.size(), 1u);
	ASSERT_EQ(requests[0].method, "POST");
	ASSERT_EQ(requests[0].path, "/sessions/new");
	ASSERT_EQ(requests[0].body, "");
	ASSERT_TRUE(cfrt_busy(rt, CFRT_LEG_PUB));

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 201,
				"{\"sessionId\":\"pub1\"}"), 0);
	ASSERT_STREQ(cfrt_session_id(rt, CFRT_LEG_PUB), "pub1");
	ASSERT_EQ(sessions.size(), 1u);

	ASSERT_EQ(requests.size(), 2u);
	ASSERT_EQ(requests[1].path, "/sessions/pub1/tracks/new");
	ASSERT_NE(requests[1].body.find("\"location\":\"local\""),
		  std::string::npos);
	ASSERT_NE(requests[1].body.find("\"mid\":\"0\""), std::string::npos);
	ASSERT_NE(requests[1].body.find("\"trackName\":\"audio\""),
		  std::string::npos);
	ASSERT_NE(requests[1].body.find("\"trackName\":\"video\""),
		  std::string::npos);
	ASSERT_EQ(requests[1].body.find("\"mid\":\"2\""), std::string::npos);
	ASSERT_NE(requests[1].body.find("\"type\":\"offer\""),
		  std::string::npos);

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 200,
		"{\"requiresImmediateRenegotiation\":false,"
		"\"sessionDescription\":{\"type\":\"answer\","
		"\"sdp\":\"pub-answer\"}}"), 0);

	ASSERT_EQ(sdps.size(), 1u);
	ASSERT_EQ(sdps[0].leg, CFRT_LEG_PUB);
	ASSERT_FALSE(sdps[0].offer);
	ASSERT_TRUE(sdps[0].initial);
	ASSERT_EQ(sdps[0].sdp, "pub-answer");
	ASSERT_FALSE(cfrt_busy(rt, CFRT_LEG_PUB));
	ASSERT_EQ(list_count(cfrt_tracks(rt, CFRT_LEG_PUB)), 2u);

	/* re-offer: tracks already published are not published again */
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_PUB, pub_offer), 0);
	ASSERT_EQ(requests.size(), 3u);
	ASSERT_EQ(requests[2].path, "/sessions/pub1/tracks/new");
	ASSERT_EQ(requests[2].body.find("\"location\""), std::string::npos);
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 200,
		"{\"sessionDescription\":{\"type\":\"answer\","
		"\"sdp\":\"pub-answer2\"}}"), 0);
	ASSERT_EQ(sdps.size(), 2u);
	ASSERT_FALSE(sdps[1].initial);

	ASSERT_TRUE(errors.empty());
}


TEST_F(CfrtTest, sub_session_with_offer)
{
	sub_session();

	ASSERT_EQ(requests.size(), 1u);
	ASSERT_EQ(requests[0].path, "/sessions/new");
	ASSERT_NE(requests[0].body.find("sub-offer"), std::string::npos);
	ASSERT_STREQ(cfrt_session_id(rt, CFRT_LEG_SUB), "sub1");

	ASSERT_EQ(sdps.size(), 1u);
	ASSERT_EQ(sdps[0].leg, CFRT_LEG_SUB);
	ASSERT_TRUE(sdps[0].initial);
	ASSERT_FALSE(sdps[0].offer);

	/* a second session is refused */
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_SUB, "again"), EALREADY);
}


TEST_F(CfrtTest, pull_blocks_until_answer)
{
	sub_session();
	requests.clear();
	sdps.clear();

	ASSERT_EQ(cfrt_subscribe(rt, "pubX", "audio", "video"), 0);
	ASSERT_EQ(requests.size(), 1u);
	ASSERT_EQ(requests[0].path, "/sessions/sub1/tracks/new");
	ASSERT_NE(requests[0].body.find("\"location\":\"remote\""),
		  std::string::npos);
	ASSERT_NE(requests[0].body.find("\"sessionId\":\"pubX\""),
		  std::string::npos);
	ASSERT_NE(requests[0].body.find("\"kind\":\"video\""),
		  std::string::npos);

	/* queue a data channel pull behind it */
	ASSERT_EQ(cfrt_dc_subscribe(rt, "pubX", "catalog"), 0);
	ASSERT_EQ(requests.size(), 1u);

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200,
				sfu_pull_response().c_str()), 0);

	/* tracks with ssrcs from the SFU offer */
	ASSERT_EQ(track_mids.size(), 2u);
	ASSERT_EQ(track_mids[0], "3");
	ASSERT_EQ(track_ssrcs[0], 3333u);
	ASSERT_EQ(track_ssrcs[1], 4444u);
	ASSERT_EQ(list_count(cfrt_tracks(rt, CFRT_LEG_SUB)), 2u);

	ASSERT_EQ(sdps.size(), 1u);
	ASSERT_TRUE(sdps[0].offer);
	ASSERT_FALSE(sdps[0].initial);

	/* blocked: the dc pull waits for our answer */
	ASSERT_EQ(requests.size(), 1u);

	ASSERT_EQ(cfrt_answer(rt, CFRT_LEG_SUB, "sub-answer2"), 0);
	ASSERT_EQ(requests.size(), 2u);
	ASSERT_EQ(requests[1].method, "PUT");
	ASSERT_EQ(requests[1].path, "/sessions/sub1/renegotiate");
	ASSERT_NE(requests[1].body.find("\"type\":\"answer\""),
		  std::string::npos);

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200, "{}"), 0);
	ASSERT_EQ(requests.size(), 3u);
	ASSERT_EQ(requests[2].path, "/sessions/sub1/datachannels/new");
	ASSERT_NE(requests[2].body.find("\"dataChannelName\":\"catalog\""),
		  std::string::npos);

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200,
		"{\"dataChannels\":[{\"location\":\"remote\","
		"\"sessionId\":\"pubX\",\"dataChannelName\":\"catalog\","
		"\"id\":4}]}"), 0);
	ASSERT_EQ(dc_ids.size(), 1u);
	ASSERT_EQ(dc_ids[0], 4);
	ASSERT_EQ(dc_sessions[0], "pubX");

	/* unsubscribe closes the pulled mids */
	ASSERT_EQ(cfrt_unsubscribe(rt, "pubX"), 0);
	ASSERT_EQ(requests.size(), 4u);
	ASSERT_EQ(requests[3].path, "/sessions/sub1/tracks/close");
	ASSERT_NE(requests[3].body.find("\"mid\":\"3\""), std::string::npos);
	ASSERT_NE(requests[3].body.find("\"mid\":\"4\""), std::string::npos);
	ASSERT_NE(requests[3].body.find("\"force\":true"), std::string::npos);
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200, "{}"), 0);
	ASSERT_EQ(list_count(cfrt_tracks(rt, CFRT_LEG_SUB)), 0u);

	/* nothing left to close: no request */
	ASSERT_EQ(cfrt_unsubscribe(rt, "pubX"), 0);
	ASSERT_EQ(requests.size(), 4u);

	ASSERT_TRUE(errors.empty());
}


TEST_F(CfrtTest, publish_dc)
{
	publish();
	requests.clear();

	ASSERT_EQ(cfrt_dc_publish(rt, "catalog"), 0);
	ASSERT_EQ(requests.size(), 1u);
	ASSERT_EQ(requests[0].path, "/sessions/pub1/datachannels/new");
	ASSERT_NE(requests[0].body.find("\"location\":\"local\""),
		  std::string::npos);

	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 200,
		"{\"dataChannels\":[{\"location\":\"local\","
		"\"dataChannelName\":\"catalog\",\"id\":2}]}"), 0);
	ASSERT_EQ(dc_ids.size(), 1u);
	ASSERT_EQ(dc_ids[0], 2);
	ASSERT_EQ(dc_sessions[0], "");
}


TEST_F(CfrtTest, errors)
{
	/* requests before the session fail */
	ASSERT_EQ(cfrt_subscribe(rt, "pubX", "audio", NULL), 0);
	ASSERT_EQ(errors.size(), 1u);
	ASSERT_EQ(errors[0], ENOTCONN);
	ASSERT_TRUE(requests.empty());

	/* HTTP auth failure */
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_PUB, pub_offer), 0);
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 401, "unauthorized"),
		  EACCES);
	ASSERT_EQ(errors.size(), 2u);
	ASSERT_EQ(errors[1], EACCES);
	/* the queued tracks request was dropped */
	ASSERT_EQ(requests.size(), 1u);
	ASSERT_FALSE(cfrt_busy(rt, CFRT_LEG_PUB));

	/* error in body */
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_SUB, "sub-offer"), 0);
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200,
		"{\"errorCode\":\"x\",\"errorDescription\":\"bad\"}"),
		  EPROTO);
	ASSERT_EQ(errors.size(), 3u);

	/* response without request */
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_SUB, 200, "{}"), EPROTO);

	/* HTTP request could not be started */
	http_err = EIO;
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_SUB, "sub-offer"), 0);
	ASSERT_EQ(errors.size(), 4u);
	ASSERT_EQ(errors[3], EIO);
	ASSERT_FALSE(cfrt_busy(rt, CFRT_LEG_SUB));
}


TEST_F(CfrtTest, reset)
{
	publish();
	ASSERT_STREQ(cfrt_session_id(rt, CFRT_LEG_PUB), "pub1");

	cfrt_reset(rt, CFRT_LEG_PUB);
	ASSERT_TRUE(cfrt_session_id(rt, CFRT_LEG_PUB) == NULL);

	/* a new offer creates a new session and republishes */
	requests.clear();
	ASSERT_EQ(cfrt_offer(rt, CFRT_LEG_PUB, pub_offer), 0);
	ASSERT_EQ(requests[0].path, "/sessions/new");
	ASSERT_EQ(cfrt_response(rt, CFRT_LEG_PUB, 201,
				"{\"sessionId\":\"pub2\"}"), 0);
	ASSERT_NE(requests[1].body.find("\"mid\":\"0\""), std::string::npos);
}
