#include <re.h>
#include <avs.h>
#include <openssl/sha.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
extern "C" {
#include "../src/ccall/ccall.h"
#include "../src/ecall/ecall.h"
#include "../src/catalog/catalog.h"
#include "../src/cloudflare_realtime/cloudflare_realtime.h"
}

namespace {
struct FakeFlow {
 struct iflow api;
 enum icall_vstate video;
 unsigned gathers;
 unsigned answers;
 unsigned video_mid;
 const char *video_name;
};
struct Request { std::string method, path, body; enum ccall_ecall_role role; };
static unsigned allocations;
static std::vector<std::string> frames;
static std::vector<std::string> receive_bindings;
static std::vector<std::string> published_video_states;
static void published_video(struct icall *, const char *uid, const char *cid, enum icall_vstate state, void *) {
 published_video_states.emplace_back(std::string(uid) + "." + cid + "=" + std::to_string(state));
}
static int receive_tracks(struct iflow *, const char *json) { receive_bindings.emplace_back(json); return 0; }
static int video_state(struct iflow *flow, enum icall_vstate state) {
 auto *fake = reinterpret_cast<FakeFlow *>(flow);
 if (state != ICALL_VIDEO_STATE_STOPPED) {
  ++fake->video_mid;
  if (fake->video_mid == 2) ++fake->video_mid;
 }
 fake->video = state;
 return 0;
}
static bool gathered(const struct iflow *) { return false; }
static bool already_gathered(const struct iflow *) { return true; }
static int gather(struct iflow *flow, bool) {
 ++reinterpret_cast<FakeFlow *>(flow)->gathers;
 return 0;
}
static int offer(struct iflow *flow, char *sdp, size_t size) {
 std::string result = "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendonly\r\na=mid:0\r\na=msid:stream mic\r\n";
 if (reinterpret_cast<FakeFlow *>(flow)->video != ICALL_VIDEO_STATE_STOPPED) {
  result += "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\na=mid:" + std::to_string(reinterpret_cast<FakeFlow *>(flow)->video_mid) + "\r\na=msid:stream ";
  auto *fake = reinterpret_cast<FakeFlow *>(flow);
  result += fake->video_name ? std::string(fake->video_name) + "\r\n" :
   (fake->video == ICALL_VIDEO_STATE_SCREENSHARE ? "screen\r\n" : "camera\r\n");
 }
 else
  result += "m=video 0 UDP/TLS/RTP/SAVPF 96\r\na=inactive\r\na=mid:1\r\n";
 result += "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:2\r\n";
 str_ncpy(sdp, result.c_str(), size);
 return 0;
}
static int receive_offer(struct iflow *, const char *) { return 0; }
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
 fake->api.handle_offer = receive_offer;
 fake->api.generate_answer = offer;
 fake->api.dce_send = send_frame;
 fake->api.dce_create = create_channel;
 fake->api.set_receive_tracks = receive_tracks;
 *flow = &fake->api;
 return 0;
}
static int request(struct cloudflare_realtime *, enum ccall_ecall_role role,
 const char *method, const char *path, const char *, const char *, const char *body,
 cloudflare_realtime_response_h *, void *arg) {
 static_cast<std::vector<Request> *>(arg)->push_back({method, path, body ? body : "", role});
 return 0;
}
class PublisherUpdate : public ::testing::Test {
protected:
 void SetUp() override {
  allocations = 0;
  receive_bindings.clear();
  published_video_states.clear();
  frames.clear();
  iflow_set_alloc(allocate);
  ASSERT_EQ(0, msystem_get(&msys, "audummy", NULL, NULL, NULL));
  ASSERT_EQ(0, ccall_alloc(&call, NULL, "call", "self", "one", false, false));
  call->icall.vstate_changedh = published_video;
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
 void remote_sync(uint32_t revision, const std::string &tracks) {
  auto *user = userlist_find_by_real(call->userl, "remote", "two");
  ASSERT_NE(nullptr, user);
  struct econn_message msg;
  ASSERT_EQ(0, econn_message_init(&msg, ECONN_CATALOG_SYNC, "remote-session"));
  str_ncpy(msg.src_userid, user->userid_hash, ECONN_ID_LEN);
  str_ncpy(msg.src_clientid, user->clientid_hash, ECONN_ID_LEN);
  msg.u.catalogsync.revision = revision;
  ASSERT_EQ(0, jzon_decode(&msg.u.catalogsync.tracks, tracks.c_str(), tracks.size()));
  char *wire = NULL;
  ASSERT_EQ(0, econn_message_encode(&wire, &msg));
  auto *flow = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER)->flow;
  flow->dce_recvh(flow, reinterpret_cast<const uint8_t *>(wire), strlen(wire), flow->arg);
  mem_deref(wire); econn_message_reset(&msg);
 }
 void subscribe_answer(const char *name, const char *mid) {
  std::string response = "{\"tracks\":[{\"sessionId\":\"remote-pub\",\"trackName\":\"" + std::string(name) + "\",\"mid\":\"" + mid + "\"}],\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=video 9 UDP/TLS/RTP/SAVPF 96\\r\\na=sendonly\\r\\na=mid:" + mid + "\\r\\n\"}}";
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, response.c_str()));
  auto *flow = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER)->flow;
  flow->gatherh(flow, flow->arg);
  ASSERT_EQ("/sessions/sub-media/renegotiate", requests.back().path);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 }
 void media_changes(bool joining) {
  struct list clients = LIST_INIT;
  auto *client = icall_client_alloc("remote", "two");
  list_append(&clients, &client->le, client);
  ccall_set_clients(&call->icall, &clients, 2); list_flush(&clients);
  struct catalog *remote = NULL;
  ASSERT_EQ(0, catalog_alloc(&remote, "call", "remote.two"));
  ASSERT_EQ(0, catalog_set_publication(remote, "{\"userId\":\"remote\",\"clientId\":\"two\",\"publisherSessionId\":\"remote-pub\",\"tracks\":[]}", 1, "reachable"));
  char *hello = NULL;
  ASSERT_EQ(0, catalog_encode_hello(remote, "remote.two", &hello));
  if (joining) {
   ASSERT_EQ(0, catalog_apply_hello(call->catalog, hello, ccall_catalog_participant_approved, call));
   ASSERT_EQ(0, str_dup(&call->keyholder_session, "remote-pub"));
   ASSERT_EQ(0, ccall_catalog_refresh(call));
  }
  else {
   struct econn_message msg;
   ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "remote-session"));
   msg.u.catalog.json = static_cast<char *>(mem_ref(hello));
   char *wire = NULL;
   ASSERT_EQ(0, econn_message_encode(&wire, &msg));
   fake->api.dce_recvh(&fake->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), fake->api.arg);
   mem_deref(wire); econn_message_reset(&msg);
  }
  mem_deref(hello); catalog_close(remote);
  ASSERT_EQ("/sessions/new", requests.back().path);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 201, "{\"sessionId\":\"sub-media\"}"));
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
   "{\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\na=mid:0\\r\\na=sctp-port:5000\\r\\n\"}}"));
  auto *sub = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER);
  auto *subflow = reinterpret_cast<FakeFlow *>(sub->flow);
  subflow->api.gatherh(&subflow->api, subflow->api.arg);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
  subflow->api.estabh(&subflow->api, "DTLS", "", subflow->api.arg);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{\"dataChannels\":[{\"id\":6}]}"));
  subflow->api.dce_estabh(&subflow->api, subflow->api.arg);
  if (joining) {
   struct econn_message msg;
   ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "remote-session"));
   ASSERT_EQ(0, catalog_encode_snapshot(call->catalog, &msg.u.catalog.json));
   char *wire = NULL;
   ASSERT_EQ(0, econn_message_encode(&wire, &msg));
   subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
   mem_deref(wire); econn_message_reset(&msg);
   ASSERT_TRUE(call->catalog_join_confirmed);
  }
  const unsigned flows = allocations;
  const std::string audio = "{\"sessionId\":\"remote-pub\",\"trackName\":\"mic\",\"kind\":\"audio\",\"muted\":false}";
  ASSERT_NO_FATAL_FAILURE(remote_sync(2, "[" + audio + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/new", requests.back().path);
  ASSERT_NO_FATAL_FAILURE(subscribe_answer("mic", "1"));
  struct list selection = LIST_INIT;
  auto *selected = icall_client_alloc("remote", "two");
  list_append(&selection, &selected->le, selected);
  ASSERT_EQ(0, ccall_request_video_streams(&call->icall, &selection, ICALL_STREAM_MODE_DEFAULT));
  list_flush(&selection);
  const std::string camera = "{\"sessionId\":\"remote-pub\",\"trackName\":\"camera\",\"kind\":\"video\",\"source\":\"camera\"}";
  const std::string screen = "{\"sessionId\":\"remote-pub\",\"trackName\":\"screen\",\"kind\":\"video\",\"source\":\"screen\"}";
  ASSERT_NO_FATAL_FAILURE(remote_sync(3, "[" + audio + "," + camera + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/new", requests.back().path);
  EXPECT_NE(std::string::npos, requests.back().body.find("camera"));
  EXPECT_EQ(std::string::npos, requests.back().body.find("mic"));
  ASSERT_NO_FATAL_FAILURE(subscribe_answer("camera", "2"));
  // Source replacement is close-old, add-new, on the same receiving transport.
  ASSERT_NO_FATAL_FAILURE(remote_sync(4, "[" + audio + "," + screen + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/close", requests.back().path);
  EXPECT_NE(std::string::npos, requests.back().body.find("\"mid\":\"2\""));
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{\"tracks\":[{\"mid\":\"2\"}]}"));
  ASSERT_EQ("/sessions/sub-media/tracks/new", requests.back().path);
  EXPECT_NE(std::string::npos, requests.back().body.find("screen"));
  ASSERT_NO_FATAL_FAILURE(subscribe_answer("screen", "3"));
  ASSERT_NO_FATAL_FAILURE(remote_sync(5, "[" + audio + "," + camera + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/close", requests.back().path);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{\"tracks\":[{\"mid\":\"3\"}]}"));
  ASSERT_NO_FATAL_FAILURE(subscribe_answer("camera", "4"));
  ASSERT_NO_FATAL_FAILURE(remote_sync(6, "[" + audio + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/close", requests.back().path);
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{\"tracks\":[{\"mid\":\"4\"}]}"));
  const size_t settled = requests.size();
  ASSERT_NO_FATAL_FAILURE(remote_sync(6, "[" + audio + "," + screen + "]"));
  EXPECT_EQ(settled, requests.size()); // Duplicate revision cannot resurrect removed media.
  EXPECT_EQ(flows, allocations);
  auto *user = userlist_find_by_real(call->userl, "remote", "two");
  EXPECT_TRUE(user->audio_subscribed);
  EXPECT_FALSE(user->video_subscribed);
  EXPECT_EQ(ICALL_VIDEO_STATE_STOPPED, user->video_state);
  // The chosen participant remains selected: re-enabling camera needs no new client request.
  ASSERT_NO_FATAL_FAILURE(remote_sync(7, "[" + audio + "," + camera + "]"));
  ASSERT_EQ("/sessions/sub-media/tracks/new", requests.back().path);
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

TEST_F(PublisherUpdate, source_change_force_closes_then_publishes_new_locator) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"camera-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 unsigned old_mid = 1;
 for (auto desired : {ICALL_VIDEO_STATE_SCREENSHARE, ICALL_VIDEO_STATE_STARTED}) {
  const uint32_t revision = call->catalog_revision;
  requests.clear(); frames.clear();
  ASSERT_EQ(0, ccall_set_vstate(&call->icall, desired));
  complete_offer();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ("PUT", requests[0].method);
  EXPECT_EQ("/sessions/pub/tracks/close", requests[0].path);
  struct json_object *root = NULL, *tracks = NULL, *description = NULL;
  ASSERT_EQ(0, jzon_decode(&root, requests[0].body.c_str(), requests[0].body.size()));
  ASSERT_EQ(0, jzon_array(&tracks, root, "tracks"));
  EXPECT_STREQ(std::to_string(old_mid).c_str(), jzon_str(json_object_array_get_idx(tracks, 0), "mid"));
  bool force = false;
  ASSERT_EQ(0, jzon_bool(&force, root, "force"));
  EXPECT_TRUE(force);
  EXPECT_EQ(ENOENT, jzon_object(&description, root, "sessionDescription"));
  mem_deref(root);
  EXPECT_TRUE(frames.empty());
  EXPECT_EQ(revision, call->catalog_revision);
  const std::string closed = "{\"tracks\":[{\"mid\":\"" + std::to_string(old_mid) + "\"}]}";
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200, closed.c_str()));
  ASSERT_EQ(2u, requests.size());
  EXPECT_EQ("POST", requests[1].method);
  EXPECT_EQ("/sessions/pub/tracks/new", requests[1].path);
  root = NULL; tracks = NULL;
  ASSERT_EQ(0, jzon_decode(&root, requests[1].body.c_str(), requests[1].body.size()));
  ASSERT_EQ(0, jzon_array(&tracks, root, "tracks"));
  ASSERT_EQ(1, json_object_array_length(tracks));
  const char *name = desired == ICALL_VIDEO_STATE_SCREENSHARE ? "screen" : "camera";
  EXPECT_STREQ(name, jzon_str(json_object_array_get_idx(tracks, 0), "trackName"));
  EXPECT_STREQ(std::to_string(fake->video_mid).c_str(), jzon_str(json_object_array_get_idx(tracks, 0), "mid"));
  mem_deref(root);
  EXPECT_EQ(EBUSY, call->transport_handlers.can_updateh(call, CCALL_ECALL_PUBLISHER, call->transport_arg));
  const std::string answer = "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"source-answer\"},\"tracks\":[{\"mid\":\"" + std::to_string(fake->video_mid) + "\",\"trackName\":\"" + name + "\"}]}";
  ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200, answer.c_str()));
  EXPECT_EQ(revision, call->catalog_revision);
  fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
  ASSERT_EQ(2u, frames.size());
  struct econn_message *sync = NULL;
  ASSERT_EQ(0, econn_message_decode(&sync, 0, 0, frames[1].c_str(), frames[1].size()));
  EXPECT_EQ(revision + 1, sync->u.catalogsync.revision);
  ASSERT_EQ(2, json_object_array_length(sync->u.catalogsync.tracks));
  auto *video = json_object_array_get_idx(sync->u.catalogsync.tracks, 1);
  EXPECT_STREQ(name, jzon_str(video, "trackName"));
  EXPECT_STREQ(name, jzon_str(video, "source"));
  EXPECT_EQ(1u, allocations);
  EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
  old_mid = fake->video_mid;
  mem_deref(sync);
 }
}

TEST_F(PublisherUpdate, screenshare_can_be_added_without_camera_then_closed) {
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_SCREENSHARE));
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"screen-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"screen\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STOPPED));
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/close", requests[0].path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"close-answer\"},\"tracks\":[{\"mid\":\"1\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
 EXPECT_EQ(3u, call->catalog_revision);
 EXPECT_EQ(1u, allocations);
}

TEST_F(PublisherUpdate, failed_force_close_does_not_publish_new_track) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_SCREENSHARE));
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 EXPECT_NE(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"tracks\":[{\"mid\":\"1\",\"errorCode\":\"close_failed\"}]}"));
 EXPECT_EQ(1u, requests.size());
 EXPECT_TRUE(frames.empty());
}

TEST_F(PublisherUpdate, stop_before_force_close_response_prevents_publish) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_SCREENSHARE));
 complete_offer();
 ccall_end(&call->icall);
 EXPECT_EQ(ECANCELED, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"tracks\":[{\"mid\":\"1\"}]}"));
 EXPECT_EQ(1u, requests.size());
 EXPECT_EQ(CCALL_STATE_IDLE, call->state);
}

TEST_F(PublisherUpdate, stopped_then_started_waits_for_first_update_before_camera_publish) {
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_SCREENSHARE));
 complete_offer();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"screen-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"screen\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STOPPED));
 // The real webapp requests STARTED immediately after stopping screen sharing.
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_STARTED));
 EXPECT_TRUE(call->pending_vstate_valid);
 EXPECT_EQ(ICALL_VIDEO_STATE_STOPPED, call->vstate);
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/close", requests[0].path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"close-answer\"},\"tracks\":[{\"mid\":\"1\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_FALSE(call->pending_vstate_valid);
 EXPECT_EQ(ICALL_VIDEO_STATE_STARTED, call->vstate);
 EXPECT_TRUE(ecall_media_update_pending(publisher));
 complete_offer();
 ASSERT_EQ(2u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/new", requests[1].path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"camera-answer\"},\"tracks\":[{\"mid\":\"3\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_EQ(4u, call->catalog_revision);
 EXPECT_EQ(CCALL_STATE_ACTIVE, call->state);
}

TEST_F(PublisherUpdate, camera_device_change_refreshes_same_state_and_uses_force_close) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"camera-answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 requests.clear(); frames.clear();
 fake->video_name = "camera-two";
 ASSERT_EQ(0, fake->api.video_updateh(&fake->api, fake->api.arg));
 EXPECT_EQ(ICALL_VIDEO_STATE_STARTED, publisher->vstate);
 EXPECT_EQ(EBUSY, fake->api.video_updateh(&fake->api, fake->api.arg));
 complete_offer();
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/close", requests[0].path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"tracks\":[{\"mid\":\"1\"}]}"));
 ASSERT_EQ(2u, requests.size());
 EXPECT_EQ("/sessions/pub/tracks/new", requests[1].path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"new-camera-answer\"},\"tracks\":[{\"mid\":\"3\",\"trackName\":\"camera-two\"}]}"));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_EQ(3u, call->catalog_revision);
 ASSERT_EQ(2u, frames.size());
 struct econn_message *sync = NULL;
 ASSERT_EQ(0, econn_message_decode(&sync, 0, 0, frames[1].c_str(), frames[1].size()));
 auto *video = json_object_array_get_idx(sync->u.catalogsync.tracks, 1);
 EXPECT_STREQ("camera-two", jzon_str(video, "trackName"));
 EXPECT_STREQ("camera", jzon_str(video, "source"));
 EXPECT_EQ(1u, allocations);
 mem_deref(sync);
}

TEST_F(PublisherUpdate, pending_video_state_is_cleared_on_stop) {
 camera_on();
 ASSERT_EQ(0, ccall_set_vstate(&call->icall, ICALL_VIDEO_STATE_SCREENSHARE));
 EXPECT_TRUE(call->pending_vstate_valid);
 ccall_end(&call->icall);
 EXPECT_FALSE(call->pending_vstate_valid);
 EXPECT_EQ(CCALL_STATE_IDLE, call->state);
}

namespace {
static void finish_retry_wait(void *) { re_cancel(); }
static void wait_for_retry(unsigned delay) {
 struct tmr timer;
 tmr_init(&timer);
 tmr_start(&timer, delay, finish_retry_wait, NULL);
 re_main(NULL);
 tmr_cancel(&timer);
}
}

TEST_F(PublisherUpdate, http_500_retries_identical_request_and_waits_for_sdp) {
 camera_on();
 const Request original = requests.back();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 500, "{}"));
 EXPECT_EQ(1u, requests.size());
 EXPECT_EQ(EBUSY, call->transport_handlers.can_updateh(call, CCALL_ECALL_PUBLISHER, call->transport_arg));
 wait_for_retry(600);
 ASSERT_EQ(2u, requests.size());
 EXPECT_EQ(original.path, requests.back().path);
 EXPECT_EQ(original.method, requests.back().method);
 EXPECT_EQ(original.body, requests.back().body);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 200,
  "{\"sessionDescription\":{\"type\":\"answer\",\"sdp\":\"answer\"},\"tracks\":[{\"mid\":\"1\",\"trackName\":\"camera\"}]}"));
 EXPECT_TRUE(ecall_media_update_pending(publisher));
 fake->api.sdp_appliedh(&fake->api, false, 0, fake->api.arg);
 EXPECT_FALSE(ecall_media_update_pending(publisher));
 wait_for_retry(600);
 EXPECT_EQ(2u, requests.size());
}

TEST_F(PublisherUpdate, http_500_stops_after_three_attempts) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 500, "{}"));
 wait_for_retry(600);
 ASSERT_EQ(2u, requests.size());
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 500, "{}"));
 wait_for_retry(1100);
 ASSERT_EQ(3u, requests.size());
 EXPECT_EQ(EPROTO, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 500, "{}"));
 wait_for_retry(1100);
 EXPECT_EQ(3u, requests.size());
}

TEST_F(PublisherUpdate, stop_cancels_http_500_backoff) {
 camera_on();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_PUBLISHER, 500, "{}"));
 ccall_end(&call->icall);
 wait_for_retry(600);
 EXPECT_EQ(1u, requests.size());
}

TEST_F(PublisherUpdate, join_negotiates_subscriber_channel_before_hello) {
 ASSERT_EQ(0, str_dup(&call->keyholder_session, "keyholder-session"));
 ASSERT_EQ(0, ccall_catalog_refresh(call));
 ASSERT_EQ(1u, requests.size());
 EXPECT_EQ(CCALL_ECALL_SUBSCRIBER, requests.back().role);
 EXPECT_EQ("/sessions/new", requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 201,
  "{\"sessionId\":\"sub\"}"));
 ASSERT_EQ(2u, requests.size());
 EXPECT_EQ("/sessions/sub/datachannels/establish", requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"requiresImmediateRenegotiation\":true,\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\na=mid:0\\r\\na=sctp-port:5000\\r\\n\"}}"));
 auto *sub = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER);
 ASSERT_NE(nullptr, sub);
 ASSERT_NE(nullptr, sub->flow);
 auto *subflow = reinterpret_cast<FakeFlow *>(sub->flow);
 subflow->api.gatherh(&subflow->api, subflow->api.arg);
 ASSERT_EQ(3u, requests.size());
 EXPECT_EQ("/sessions/sub/renegotiate", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("answer"));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 EXPECT_EQ(3u, requests.size()); // Wait for transport connectivity as well as API acceptance.
 subflow->api.estabh(&subflow->api, "DTLS", "", subflow->api.arg);
 ASSERT_EQ(4u, requests.size());
 EXPECT_EQ("/sessions/sub/datachannels/new", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("keyholder-session"));
 EXPECT_NE(std::string::npos, requests.back().body.find("\"canReply\":true"));
 subflow->api.dce_estabh(&subflow->api, subflow->api.arg); // Reserved transport channel.
 frames.clear();
 const Request subscription = requests.back();
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"dataChannels\":[{\"errorCode\":\"transport_unavailable_error\",\"errorDescription\":\"Try again later\"}]}"));
 EXPECT_EQ(4u, requests.size());
 EXPECT_TRUE(frames.empty());
 wait_for_retry(600);
 ASSERT_EQ(5u, requests.size());
 EXPECT_EQ(subscription.body, requests.back().body);
 EXPECT_EQ(subscription.path, requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"dataChannels\":[{\"id\":4}]}"));
 EXPECT_TRUE(frames.empty());
 subflow->api.dce_estabh(&subflow->api, subflow->api.arg);
 bool found = false;
 for (const auto &frame : frames) if (frame.find("catalog_hello") != std::string::npos) found = true;
 EXPECT_TRUE(found);
 EXPECT_FALSE(call->catalog_join_confirmed);
 const size_t previous = requests.size();
 wait_for_retry(2100);
 ASSERT_EQ(previous + 1, requests.size());
 EXPECT_EQ("/sessions/sub/datachannels/update", requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"dataChannels\":[{\"id\":4}]}"));
 struct econn_message msg;
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "keyholder"));
 ASSERT_EQ(0, catalog_encode_snapshot(call->catalog, &msg.u.catalog.json));
 char *wire = NULL;
 ASSERT_EQ(0, econn_message_encode(&wire, &msg));
 subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
 mem_deref(wire);
 EXPECT_TRUE(call->catalog_join_confirmed);
 econn_message_reset(&msg);
 wait_for_retry(2100);
 EXPECT_EQ(previous + 1, requests.size()); // No further grants or other channel subscriptions.
}

TEST_F(PublisherUpdate, keyholder_subscribes_hello_sender_and_receives_publisher_updates) {
 struct list clients = LIST_INIT;
 auto *client = icall_client_alloc("remote", "two");
 ASSERT_NE(nullptr, client);
 list_append(&clients, &client->le, client);
 ccall_set_clients(&call->icall, &clients, 2);
 list_flush(&clients);
 struct catalog *remote = NULL;
 ASSERT_EQ(0, catalog_alloc(&remote, "call", "remote.two"));
 ASSERT_EQ(0, catalog_set_publication(remote,
  "{\"userId\":\"remote\",\"clientId\":\"two\",\"publisherSessionId\":\"remote-pub\",\"tracks\":[]}", 7, "reachable"));
 struct econn_message msg;
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "remote-session"));
 ASSERT_EQ(0, catalog_encode_hello(remote, "remote.two", &msg.u.catalog.json));
 frames.clear();
 const size_t before = requests.size();
 char *wire = NULL;
 ASSERT_EQ(0, econn_message_encode(&wire, &msg));
 fake->api.dce_recvh(&fake->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), fake->api.arg);
 ASSERT_EQ(1u, frames.size());
 EXPECT_NE(std::string::npos, frames.back().find("conf_catalog"));
 EXPECT_NE(std::string::npos, frames.back().find("remote-pub"));
 EXPECT_NE(std::string::npos, frames.back().find("remote.two"));
 EXPECT_EQ(7u, catalog_participant_revision(call->catalog, "remote.two"));
 fake->api.dce_recvh(&fake->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), fake->api.arg);
 mem_deref(wire);
 EXPECT_EQ(2u, frames.size()); // Repeat welcome, not a second participant.
 ASSERT_EQ(before + 1, requests.size());
 EXPECT_EQ(CCALL_ECALL_SUBSCRIBER, requests.back().role);
 EXPECT_EQ("/sessions/new", requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 201,
  "{\"sessionId\":\"sub-a\"}"));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\na=mid:0\\r\\na=sctp-port:5000\\r\\n\"}}"));
 auto *sub = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER);
 auto *subflow = reinterpret_cast<FakeFlow *>(sub->flow);
 subflow->api.gatherh(&subflow->api, subflow->api.arg);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 subflow->api.estabh(&subflow->api, "DTLS", "", subflow->api.arg);
 ASSERT_EQ(before + 4, requests.size());
 EXPECT_EQ("/sessions/sub-a/datachannels/new", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("remote-pub"));
 EXPECT_NE(std::string::npos, requests.back().body.find("\"canReply\":false"));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"dataChannels\":[{\"id\":6}]}"));
 subflow->api.dce_estabh(&subflow->api, subflow->api.arg);
 auto *user = userlist_find_by_real(call->userl, "remote", "two");
 ASSERT_NE(nullptr, user);
 EXPECT_TRUE(user->incall_now);
 EXPECT_EQ(0u, user->ssrca); // Presence is established without media subscription.
 econn_message_reset(&msg);
 ASSERT_EQ(0, catalog_set_publication(remote,
  "{\"userId\":\"remote\",\"clientId\":\"two\",\"publisherSessionId\":\"remote-pub\",\"tracks\":[{\"sessionId\":\"remote-pub\",\"trackName\":\"mic-b\",\"kind\":\"audio\",\"muted\":true}]}", 8, "reachable"));
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "remote-session"));
 str_ncpy(msg.src_userid, user->userid_hash, ECONN_ID_LEN);
 str_ncpy(msg.src_clientid, user->clientid_hash, ECONN_ID_LEN);
 ASSERT_EQ(0, catalog_encode_snapshot(remote, &msg.u.catalog.json));
 wire = NULL;
 ASSERT_EQ(0, econn_message_encode(&wire, &msg));
 subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
 mem_deref(wire);
 EXPECT_EQ(8u, catalog_participant_revision(call->catalog, "remote.two"));
 EXPECT_TRUE(user->muted);
 // Audio follows confirmed catalog presence, without any video request.
 ASSERT_EQ("/sessions/sub-a/tracks/new", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("mic-b"));
 EXPECT_EQ(std::string::npos, requests.back().body.find("sessionDescription"));
 const unsigned flows = allocations;
 subflow->api.is_gathered = already_gathered;
 const unsigned gathers_before_media = subflow->gathers;
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"requiresImmediateRenegotiation\":true,\"tracks\":[{\"sessionId\":\"remote-pub\",\"trackName\":\"mic-b\",\"mid\":\"1\"}],\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=audio 9 UDP/TLS/RTP/SAVPF 111\\r\\na=sendonly\\r\\na=mid:1\\r\\n\"}}"));
 EXPECT_EQ(flows, allocations); // Existing subscriber PeerConnection is retained.
 EXPECT_EQ(gathers_before_media + 1, subflow->gathers); // Fresh answer even with complete ICE.
 ASSERT_FALSE(receive_bindings.empty());
 EXPECT_NE(std::string::npos, receive_bindings.back().find("remote"));
 EXPECT_NE(std::string::npos, receive_bindings.back().find("\"mid\":\"1\""));
 subflow->api.gatherh(&subflow->api, subflow->api.arg);
 ASSERT_EQ("/sessions/sub-a/renegotiate", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("\"type\":\"answer\""));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 EXPECT_TRUE(user->audio_subscribed);
 EXPECT_FALSE(user->video_subscribed);
 const size_t after_audio = requests.size();
 econn_message_reset(&msg);
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CATALOG_SYNC, "remote-session"));
 str_ncpy(msg.src_userid, user->userid_hash, ECONN_ID_LEN);
 str_ncpy(msg.src_clientid, user->clientid_hash, ECONN_ID_LEN);
 msg.u.catalogsync.revision = 9;
 const char *tracks = "[{\"sessionId\":\"remote-pub\",\"trackName\":\"mic-b\",\"kind\":\"audio\",\"muted\":false}]";
 ASSERT_EQ(0, jzon_decode(&msg.u.catalogsync.tracks, tracks, strlen(tracks)));
 wire = NULL;
 ASSERT_EQ(0, econn_message_encode(&wire, &msg));
 subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
 EXPECT_EQ(9u, catalog_participant_revision(call->catalog, "remote.two"));
 EXPECT_FALSE(user->muted);
 EXPECT_EQ(after_audio, requests.size()); // Mute preserves the subscribed audio publication.
 const size_t updates = frames.size();
 subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
 EXPECT_EQ(updates, frames.size()); // Same revision must not rebroadcast in a loop.
 mem_deref(wire);
 econn_message_reset(&msg);

 // Publishing video alone does not subscribe it until the client selects B.
 ASSERT_EQ(0, catalog_set_publication(remote,
  "{\"userId\":\"remote\",\"clientId\":\"two\",\"publisherSessionId\":\"remote-pub\",\"tracks\":[{\"sessionId\":\"remote-pub\",\"trackName\":\"mic-b\",\"kind\":\"audio\",\"muted\":false},{\"sessionId\":\"remote-pub\",\"trackName\":\"cam-b\",\"kind\":\"video\",\"source\":\"camera\"}]}", 10, "reachable"));
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_CATALOG, "remote-session"));
 str_ncpy(msg.src_userid, user->userid_hash, ECONN_ID_LEN);
 str_ncpy(msg.src_clientid, user->clientid_hash, ECONN_ID_LEN);
 ASSERT_EQ(0, catalog_encode_snapshot(remote, &msg.u.catalog.json));
 wire = NULL;
 ASSERT_EQ(0, econn_message_encode(&wire, &msg));
 subflow->api.dce_recvh(&subflow->api, reinterpret_cast<const uint8_t *>(wire), strlen(wire), subflow->api.arg);
 mem_deref(wire); econn_message_reset(&msg);
 EXPECT_EQ(after_audio, requests.size());
 ASSERT_FALSE(published_video_states.empty());
 EXPECT_EQ("remote.two=" + std::to_string(ICALL_VIDEO_STATE_STARTED), published_video_states.back());
 EXPECT_FALSE(user->video_subscribed); // Published status reaches the client before subscription.
 struct list selection = LIST_INIT;
 auto *selected = icall_client_alloc("remote", "two");
 list_append(&selection, &selected->le, selected);
 ASSERT_EQ(0, ccall_request_video_streams(&call->icall, &selection, ICALL_STREAM_MODE_DEFAULT));
 list_flush(&selection);
 ASSERT_EQ("/sessions/sub-a/tracks/new", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("cam-b"));
 EXPECT_EQ(std::string::npos, requests.back().body.find("mic-b"));
 // Deselecting during the offer exchange must wait for the answer acceptance.
 const size_t during = requests.size();
 ASSERT_EQ(0, ccall_request_video_streams(&call->icall, &selection, ICALL_STREAM_MODE_DEFAULT));
 EXPECT_EQ(during, requests.size());
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"tracks\":[{\"sessionId\":\"remote-pub\",\"trackName\":\"cam-b\",\"mid\":\"2\"}],\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=video 9 UDP/TLS/RTP/SAVPF 96\\r\\na=sendonly\\r\\na=mid:2\\r\\n\"}}"));
 subflow->api.gatherh(&subflow->api, subflow->api.arg);
 ASSERT_EQ("/sessions/sub-a/renegotiate", requests.back().path);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 ASSERT_EQ("/sessions/sub-a/tracks/close", requests.back().path);
 EXPECT_NE(std::string::npos, requests.back().body.find("\"force\":true"));
 EXPECT_NE(std::string::npos, requests.back().body.find("\"mid\":\"2\""));
 EXPECT_EQ(std::string::npos, requests.back().body.find("\"mid\":\"1\""));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{\"tracks\":[{\"mid\":\"2\"}]}"));
 EXPECT_EQ(flows, allocations);
 EXPECT_TRUE(user->audio_subscribed);
 EXPECT_FALSE(user->video_subscribed);
 // Stop while a new subscription is pending; a late offer must not restart it.
 selected = icall_client_alloc("remote", "two");
 list_append(&selection, &selected->le, selected);
 ASSERT_EQ(0, ccall_request_video_streams(&call->icall, &selection, ICALL_STREAM_MODE_DEFAULT));
 list_flush(&selection);
 const size_t before_stop = requests.size();
 ccall_end(&call->icall);
 EXPECT_EQ(ECANCELED, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 EXPECT_EQ(before_stop, requests.size());
 EXPECT_EQ(CCALL_STATE_IDLE, call->state);
 catalog_close(remote);
}

TEST_F(PublisherUpdate, join_stop_cancels_hello_grant_retries) {
 ASSERT_EQ(0, str_dup(&call->keyholder_session, "keyholder-session"));
 ASSERT_EQ(0, ccall_catalog_refresh(call));
 ccall_end(&call->icall);
 EXPECT_EQ(ECANCELED, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 201,
  "{\"sessionId\":\"late-sub\"}"));
 wait_for_retry(2100);
 EXPECT_EQ(1u, requests.size());
 EXPECT_FALSE(call->catalog_join_confirmed);
}

static int capture_call_signaling(struct icall *, const char *, struct econn_message *msg,
 struct list *, bool, void *) {
 char *json = NULL;
 int err = econn_message_encode(&json, msg);
 if (!err) frames.emplace_back(json);
 mem_deref(json);
 return err;
}

TEST_F(PublisherUpdate, first_publisher_confstart_contains_ready_catalog) {
 call->icall.sendh = capture_call_signaling;
 call->is_caller = true;
 call->sft_timestamp = 100;
 call->sft_seqno = 3;
 call->state = CCALL_STATE_CONNECTED;
 call->publisher_media_notified = false;
 frames.clear();
 ccall_set_transport_active(call, CCALL_ECALL_PUBLISHER);
 ASSERT_EQ(CCALL_STATE_ACTIVE, call->state);
 ASSERT_EQ(1u, frames.size());
 struct econn_message *msg = NULL;
 ASSERT_EQ(0, econn_message_decode(&msg, 0, 0, frames.back().c_str(), frames.back().size()));
 EXPECT_EQ(ECONN_CONF_START, msg->msg_type);
 ASSERT_NE(nullptr, msg->u.confstart.catalog);
 EXPECT_NE(std::string::npos, std::string(msg->u.confstart.catalog).find("publisherSessionId"));
 EXPECT_EQ(100u, msg->u.confstart.timestamp);
 mem_deref(msg);
}

TEST_F(PublisherUpdate, incoming_legacy_call_retains_bootstrap_for_provider_selection_before_answer) {
 struct ccall *incoming = NULL;
 ASSERT_EQ(0, ccall_alloc(&incoming, NULL, "late-call", "self", "one", true, false));
 EXPECT_FALSE(incoming->enable_publish_subscribe);
 struct econn_message msg;
 ASSERT_EQ(0, econn_message_init(&msg, ECONN_CONF_START, "call-session"));
 msg.u.confstart.timestamp = 1;
 msg.u.confstart.seqno = 1;
 uint8_t secret[16] = {1};
 msg.u.confstart.secret = static_cast<uint8_t *>(mem_alloc(sizeof(secret), NULL));
 memcpy(msg.u.confstart.secret, secret, sizeof(secret));
 msg.u.confstart.secretlen = sizeof(secret);
 ASSERT_EQ(0, str_dup(&msg.u.confstart.sft_url, "https://example.invalid"));
 ASSERT_EQ(0, str_dup(&msg.u.confstart.catalog,
  "{\"type\":\"conf_catalog\",\"catalogId\":\"late-call\",\"participants\":[{\"userId\":\"remote\",\"clientId\":\"two\",\"publisherSessionId\":\"keyholder-session\"}]}"));
 ASSERT_EQ(0, ccall_msg_recv(&incoming->icall, 0, 0, "remote", "two", &msg));
 ASSERT_EQ(CCALL_STATE_INCOMING, incoming->state);
 EXPECT_STREQ("keyholder-session", incoming->keyholder_session);
 EXPECT_TRUE(ccall_can_configure_incoming_transport(incoming));
 ASSERT_EQ(0, ccall_set_enable_publish_subscribe(incoming, true));
 ASSERT_EQ(0, ccall_prepare_ecalls(incoming));
 EXPECT_FALSE(ccall_can_configure_incoming_transport(incoming));
 EXPECT_STREQ("keyholder-session", incoming->keyholder_session);
 EXPECT_EQ(EBUSY, ccall_set_enable_publish_subscribe(incoming, false));
 econn_message_reset(&msg);
 mem_deref(incoming);
}

TEST_F(PublisherUpdate, subscriber_transport_unavailable_stops_after_three_attempts) {
 ASSERT_EQ(0, str_dup(&call->keyholder_session, "keyholder-session"));
 ASSERT_EQ(0, ccall_catalog_refresh(call));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 201,
  "{\"sessionId\":\"sub\"}"));
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200,
  "{\"sessionDescription\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\na=mid:0\\r\\na=sctp-port:5000\\r\\n\"}}"));
 auto *sub = ccall_get_ecall(call, CCALL_ECALL_SUBSCRIBER);
 auto *subflow = reinterpret_cast<FakeFlow *>(sub->flow);
 subflow->api.gatherh(&subflow->api, subflow->api.arg);
 ASSERT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, "{}"));
 subflow->api.estabh(&subflow->api, "DTLS", "", subflow->api.arg);
 ASSERT_EQ(4u, requests.size());
 const char *unavailable = "{\"dataChannels\":[{\"errorCode\":\"transport_unavailable_error\"}]}";
 EXPECT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, unavailable));
 wait_for_retry(600);
 ASSERT_EQ(5u, requests.size());
 EXPECT_EQ(0, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, unavailable));
 wait_for_retry(1100);
 ASSERT_EQ(6u, requests.size());
 EXPECT_EQ(EPROTO, cloudflare_realtime_handle_response(adapter, CCALL_ECALL_SUBSCRIBER, 200, unavailable));
 EXPECT_EQ(CCALL_STATE_IDLE, call->state);
 wait_for_retry(1100);
 EXPECT_EQ(6u, requests.size());
 EXPECT_FALSE(call->catalog_join_confirmed);
}

TEST_F(PublisherUpdate, keyholder_updates_subscriptions_when_peer_changes_media) {
 ASSERT_NO_FATAL_FAILURE(media_changes(false));
}
TEST_F(PublisherUpdate, joiner_updates_subscriptions_when_keyholder_changes_media) {
 ASSERT_NO_FATAL_FAILURE(media_changes(true));
}
