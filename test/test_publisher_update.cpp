#include <re.h>
#include <avs.h>
#include <openssl/sha.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
extern "C" {
#include "../src/ccall/ccall.h"
#include "../src/ecall/ecall.h"
#include "../src/cloudflare_realtime/cloudflare_realtime.h"
}

namespace {
struct FakeFlow {
 struct iflow api;
 enum icall_vstate video;
 unsigned gathers;
 unsigned answers;
};
struct Request { std::string method, path, body; };
static unsigned allocations;
static std::vector<std::string> frames;
static int video_state(struct iflow *flow, enum icall_vstate state) {
 reinterpret_cast<FakeFlow *>(flow)->video = state;
 return 0;
}
static bool gathered(const struct iflow *) { return false; }
static int gather(struct iflow *flow, bool) {
 ++reinterpret_cast<FakeFlow *>(flow)->gathers;
 return 0;
}
static int offer(struct iflow *flow, char *sdp, size_t size) {
 std::string result = "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendonly\r\na=mid:0\r\na=msid:stream mic\r\n";
 if (reinterpret_cast<FakeFlow *>(flow)->video == ICALL_VIDEO_STATE_STARTED)
  result += "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\na=mid:1\r\na=msid:stream camera\r\n";
 else
  result += "m=video 0 UDP/TLS/RTP/SAVPF 96\r\na=inactive\r\na=mid:1\r\n";
 result += "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:2\r\n";
 str_ncpy(sdp, result.c_str(), size);
 return 0;
}
static int answer(struct iflow *flow, const char *) {
 ++reinterpret_cast<FakeFlow *>(flow)->answers;
 return 0; // SDP application completes separately, as in the browser.
}
static int send_frame(struct iflow *, const uint8_t *data, size_t len) {
 frames.emplace_back(reinterpret_cast<const char *>(data), len);
 return 0;
}
static int create_channel(struct iflow *, const char *, int) { return 0; }
static int allocate(struct iflow **flow, const char *, const char *, const char *,
 enum icall_conv_type, enum icall_call_type, enum icall_vstate video, void *) {
 auto *fake = static_cast<FakeFlow *>(mem_zalloc(sizeof(FakeFlow), NULL));
 if (!fake) return ENOMEM;
 ++allocations;
 fake->video = video;
 fake->api.set_video_state = video_state;
 fake->api.is_gathered = gathered;
 fake->api.gather_all_turn = gather;
 fake->api.generate_offer = offer;
 fake->api.handle_answer = answer;
 fake->api.dce_send = send_frame;
 fake->api.dce_create = create_channel;
 *flow = &fake->api;
 return 0;
}
static int request(struct cloudflare_realtime *, enum ccall_ecall_role,
 const char *method, const char *path, const char *, const char *, const char *body,
 cloudflare_realtime_response_h *, void *arg) {
 static_cast<std::vector<Request> *>(arg)->push_back({method, path, body ? body : ""});
 return 0;
}
class PublisherUpdate : public ::testing::Test {
protected:
 void SetUp() override {
  allocations = 0;
  frames.clear();
  iflow_set_alloc(allocate);
  ASSERT_EQ(0, msystem_get(&msys, "audummy", NULL, NULL, NULL));
  ASSERT_EQ(0, ccall_alloc(&call, NULL, "call", "self", "one", false, false));
  ASSERT_EQ(0, ccall_set_enable_publish_subscribe(call, true));
  ASSERT_EQ(0, ccall_prepare_ecalls(call));
  struct cloudflare_realtime_conf conf = {"app", "secret", "https://example.invalid", request};
  ASSERT_EQ(0, cloudflare_realtime_alloc(&adapter, call, &conf, &requests));
  ASSERT_EQ(0, cloudflare_realtime_enable(adapter, true));
  ASSERT_EQ(0, cloudflare_realtime_set_session(adapter, CCALL_ECALL_PUBLISHER, "pub"));
  call->state = CCALL_STATE_CONNSENT;
  call->catalog_epoch_known = true;
  call->catalog_epoch = call->catalog_join_epoch = 1;
  ASSERT_EQ(0, ccall_start_ecall(call, CCALL_ECALL_PUBLISHER));
  publisher = ccall_get_ecall(call, CCALL_ECALL_PUBLISHER);
  fake = reinterpret_cast<FakeFlow *>(publisher->flow);
  complete_offer();
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
   "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},\"tracks\":[{\"mid\":\"0\",\"trackName\":\"mic\"}]}"));
  fake->api.estabh(&fake->api, "DTLS", "opus", fake->api.arg);
  fake->api.dce_estabh(&fake->api, fake->api.arg);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
   "{\"dataChannels\":[{\"id\":2}]}"));
  fake->api.dce_estabh(&fake->api, fake->api.arg);
  ASSERT_EQ(CCALL_STATE_ACTIVE, call->state);
  frames.clear();
  requests.clear();
 }
 void TearDown() override {
  if (adapter) cloudflare_realtime_close(adapter);
  mem_deref(call);
  mem_deref(msys);
  iflow_set_alloc(NULL);
 }
 void complete_offer() { fake->api.gatherh(&fake->api, fake->api.arg); }
 void camera_on() {
  ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STARTED));
  complete_offer();
 }
 struct ccall *call = NULL;
 struct msystem *msys = NULL;
 struct ecall *publisher = NULL;
 struct cloudflare_realtime *adapter = NULL;
 FakeFlow *fake = NULL;
 std::vector<Request> requests;
};

TEST_F(PublisherUpdate, camera_uses_update_and_only_publishes_new_video) {
 auto *connection = publisher->econn;
 camera_on();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("POST", requests[0].method);
 EXPECT_EQ("/sessions/pub/tracks/new", requests[0].path);
 EXPECT_EQ(1u, allocations);
 EXPECT_EQ(&fake->api, publisher->flow);
 EXPECT_EQ(connection, publisher->econn);
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
 struct json_object *root = NULL, *tracks = NULL, *sdp = NULL;
 ASSERT_EQ(0, jzon_decode(&root, requests[0].body.c_str(), requests[0].body.size()));
 ASSERT_EQ(0, jzon_array(&tracks, root, "tracks"));
 ASSERT_EQ(1, json_object_array_length(tracks));
 EXPECT_STREQ("camera", jzon_str(json_object_array_get_idx(tracks, 0), "trackName"));
 EXPECT_STREQ("1", jzon_str(json_object_array_get_idx(tracks, 0), "mid"));
 ASSERT_EQ(0, jzon_object(&sdp, root, "sessionDescription"));
 EXPECT_NE(nullptr, strstr(jzon_str(sdp, "sdp"), "m=audio"));
 EXPECT_NE(nullptr, strstr(jzon_str(sdp, "sdp"), "m=video"));
 EXPECT_NE(nullptr, strstr(jzon_str(sdp, "sdp"), "m=application"));
 EXPECT_EQ(1u, call->catalog_revision);
 EXPECT_TRUE(frames.empty());
 mem_deref(root);
}

TEST_F(PublisherUpdate, catalog_waits_for_successful_async_answer_application) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"video-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 EXPECT_TRUE(publisher->publisher_update);
 EXPECT_EQ(1u, call->catalog_revision);
 EXPECT_TRUE(frames.empty());
 EXPECT_EQ(EBUSY, call->transport_handlers.can_updateh(call, CCALL_ECALL_PUBLISHER, call->transport_arg));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_FALSE(publisher->publisher_update);
 EXPECT_EQ(2u, call->catalog_revision);
 ASSERT_EQ(2u, frames.size());
 struct econn_message *catalog = NULL, *sync = NULL;
 ASSERT_EQ(0, econn_message_decode(&catalog, 0, 0, frames[0].c_str(), frames[0].size()));
 ASSERT_EQ(0, econn_message_decode(&sync, 0, 0, frames[1].c_str(), frames[1].size()));
 EXPECT_EQ(ECONN_CONF_CATALOG, catalog->msg_type);
 EXPECT_EQ(ECONN_CATALOG_SYNC, sync->msg_type);
 EXPECT_EQ(2u, sync->u.catalogsync.revision);
 EXPECT_EQ(2, json_object_array_length(sync->u.catalogsync.tracks));
 EXPECT_EQ(ECONN_DATACHAN_ESTABLISHED, econn_current_state(publisher->econn));
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
 EXPECT_EQ(1u, allocations);
 mem_deref(catalog);
 mem_deref(sync);
}

TEST_F(PublisherUpdate, media_capture_failure_does_not_publish_catalog) {
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STARTED));
 fake->api.sdp_appliedh(&fake->api, true, EACCES, fake->api.arg);
 EXPECT_FALSE(publisher->publisher_update);
 EXPECT_TRUE(requests.empty());
 EXPECT_TRUE(frames.empty());
 EXPECT_EQ(1u, call->catalog_revision);
 EXPECT_EQ(ICALL_VIDEO_STATE_STOPPED, publisher->vstate);
 EXPECT_EQ(ICALL_VIDEO_STATE_STOPPED, call->vstate);
 complete_offer();
 EXPECT_TRUE(requests.empty());
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
}

TEST_F(PublisherUpdate, connected_events_do_not_finish_pending_negotiation) {
 camera_on();
 fake->api.estabh(&fake->api, "DTLS", "opus", fake->api.arg);
 fake->api.dce_estabh(&fake->api, fake->api.arg);
 EXPECT_TRUE(publisher->update);
 EXPECT_TRUE(publisher->publisher_update);
 EXPECT_TRUE(tmr_isrunning(&publisher->connection_tmr));
 EXPECT_EQ(ECONN_UPDATE_SENT, econn_current_state(publisher->econn));
 EXPECT_EQ(1u, requests.size());
 EXPECT_EQ(1u, call->catalog_revision);
}

TEST_F(PublisherUpdate, failed_cloudflare_track_does_not_publish_video) {
 camera_on();
 EXPECT_NE(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},"
  "\"tracks\":[{\"errorCode\":\"track_error\"}]}"));
 EXPECT_TRUE(frames.empty());
 EXPECT_FALSE(call->catalog_complete);
}

TEST_F(PublisherUpdate, stop_before_answer_application_ignores_late_completion) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},"
  "\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 // Keep the retired ECall and flow alive to simulate the asynchronous callback.
 mem_ref(publisher);
 ccall_end(&call->icall);
 frames.clear();
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_EQ(CCALL_STATE_IDLE, call->state);
 EXPECT_TRUE(frames.empty());
 EXPECT_EQ(nullptr, call->catalog);
 mem_deref(publisher);
}

TEST_F(PublisherUpdate, repeated_camera_on_does_not_start_a_second_offer) {
 camera_on();
 complete_offer(); // An extra ICE-complete callback must not send another UPDATE.
 const auto gathers = fake->gathers;
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STARTED));
 EXPECT_EQ(gathers, fake->gathers);
 EXPECT_EQ(1u, requests.size());
 EXPECT_EQ(EBUSY, ecall_set_video_send_state(publisher, ICALL_VIDEO_STATE_SCREENSHARE));
}
} // namespace

TEST_F(PublisherUpdate, camera_off_closes_video_with_offer_then_updates_catalog) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"video-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STOPPED));
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("PUT", requests[0].method);
 EXPECT_EQ("/sessions/pub/tracks/close", requests[0].path);
 struct json_object *root = NULL, *tracks = NULL;
 ASSERT_EQ(0, jzon_decode(&root, requests[0].body.c_str(), requests[0].body.size()));
 ASSERT_EQ(0, jzon_array(&tracks, root, "tracks"));
 ASSERT_EQ(1, json_object_array_length(tracks));
 EXPECT_STREQ("1", jzon_str(json_object_array_get_idx(tracks, 0), "mid"));
 EXPECT_EQ(nullptr, jzon_str(json_object_array_get_idx(tracks, 0), "trackName"));
 EXPECT_EQ(2u, call->catalog_revision);
 EXPECT_TRUE(frames.empty());
 mem_deref(root);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"close-answer\"},\"tracks\":[{\"mid\":\"1\"}]}"));
 EXPECT_EQ(2u, call->catalog_revision);
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_EQ(3u, call->catalog_revision);
 ASSERT_EQ(2u, frames.size());
 struct econn_message *sync = NULL;
 ASSERT_EQ(0, econn_message_decode(&sync, 0, 0, frames[1].c_str(), frames[1].size()));
 EXPECT_EQ(1, json_object_array_length(sync->u.catalogsync.tracks));
 EXPECT_STREQ("audio", jzon_str(json_object_array_get_idx(sync->u.catalogsync.tracks, 0), "kind"));
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
 EXPECT_EQ(1u, allocations);
 mem_deref(sync);
 // Re-enabling the camera still uses tracks/new on the existing session.
 requests.clear();
 camera_on();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/new", requests[0].path);
}
