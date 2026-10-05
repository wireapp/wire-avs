#include <gtest/gtest.h>
#include <string.h>
#include <re.h>
#include <avs.h>
extern "C" {
#include "../src/catalog/catalog.h"
}
struct approved { bool remote; };
static bool match(const char *user, const char *client, void *arg) {
 struct approved *a = static_cast<struct approved *>(arg);
 return (!strcmp(user, "self") && !strcmp(client, "one")) ||
        (a->remote && !strcmp(user, "remote") && !strcmp(client, "two"));
}
TEST(catalog, participants_are_subset_of_approved_clients) {
 struct catalog *c = NULL;
 struct approved a = {false};
 const char *remote = "{\"type\":\"catalog_update\",\"catalogId\":\"call\",\"participant\":{\"participantId\":\"remote.two\",\"revision\":1,\"userId\":\"remote\",\"clientId\":\"two\"}}";
 const char *wrong = "{\"type\":\"catalog_update\",\"catalogId\":\"call\",\"participant\":{\"participantId\":\"remote.other\",\"revision\":1,\"userId\":\"remote\",\"clientId\":\"other\"}}";
 ASSERT_TRUE(!catalog_alloc(&c, "call", "self.one"));
 ASSERT_TRUE(!catalog_participants_match(c, match, &a));
 ASSERT_TRUE(!catalog_set_publication(c, "{\"userId\":\"self\",\"clientId\":\"one\"}", 1, "reachable"));
 ASSERT_TRUE(catalog_participants_match(c, match, &a));
 a.remote = true;
 ASSERT_TRUE(catalog_participants_match(c, match, &a));
 a.remote = false;
 ASSERT_TRUE(!catalog_apply_message(c, remote, strlen(remote)));
 ASSERT_TRUE(!catalog_participants_match(c, match, &a));
 a.remote = true;
 ASSERT_TRUE(catalog_participants_match(c, match, &a));
 ASSERT_TRUE(!catalog_apply_message(c, wrong, strlen(wrong)));
 ASSERT_TRUE(!catalog_participants_match(c, match, &a));
 catalog_close(c);
}

TEST(catalog, publisher_tracks_use_shared_locator_and_media_schema) {
 const char *publisher = "[{\"location\":\"local\",\"mid\":\"0\",\"trackName\":\"mic\",\"kind\":\"audio\"},"
                         "{\"location\":\"local\",\"mid\":\"1\",\"trackName\":\"video\",\"kind\":\"video\"}]";
 for (const char *source : {"camera", "screen"}) {
  char *encoded = NULL;
  ASSERT_EQ(0, catalog_tracks_from_publisher(&encoded, "pub-session", publisher, true, source));
  struct json_object *tracks = NULL, *unused = NULL;
  ASSERT_EQ(0, jzon_decode(&tracks, encoded, strlen(encoded)));
  ASSERT_EQ(0, catalog_tracks_validate(tracks));
  ASSERT_EQ(2, json_object_array_length(tracks));
  auto *audio = json_object_array_get_idx(tracks, 0);
  auto *video = json_object_array_get_idx(tracks, 1);
  EXPECT_STREQ("pub-session", jzon_str(audio, "sessionId"));
  EXPECT_STREQ("mic", jzon_str(audio, "trackName"));
  bool muted = false;
  EXPECT_EQ(0, jzon_bool(&muted, audio, "muted"));
  EXPECT_TRUE(muted);
  EXPECT_STREQ(source, jzon_str(video, "source"));
  EXPECT_FALSE(json_object_object_get_ex(audio, "mid", &unused));
  EXPECT_FALSE(json_object_object_get_ex(audio, "location", &unused));
  mem_deref(tracks);
  mem_deref(encoded);
 }
}

TEST(catalog, snapshot_exports_participant_array_with_shared_tracks) {
 struct catalog *c = NULL;
 ASSERT_EQ(0, catalog_alloc(&c, "call", "self.one"));
 const char *publication = "{\"userId\":\"self\",\"clientId\":\"one\",\"mlsEpoch\":1,\"joinedMlsEpoch\":1,"
  "\"tracks\":[{\"sessionId\":\"pub\",\"trackName\":\"mic\",\"kind\":\"audio\",\"muted\":false}]}";
 ASSERT_EQ(0, catalog_set_publication(c, publication, 2, "reachable"));
 char *encoded = NULL;
 ASSERT_EQ(0, catalog_encode_snapshot(c, &encoded));
 struct json_object *root = NULL, *participants = NULL, *tracks = NULL;
 ASSERT_EQ(0, jzon_decode(&root, encoded, strlen(encoded)));
 EXPECT_STREQ("conf_catalog", jzon_str(root, "type"));
 int epoch = 0;
 EXPECT_EQ(0, jzon_int(&epoch, root, "mlsEpoch"));
 EXPECT_EQ(1, epoch);
 ASSERT_EQ(0, jzon_array(&participants, root, "participants"));
 ASSERT_EQ(1, json_object_array_length(participants));
 auto *self = json_object_array_get_idx(participants, 0);
 EXPECT_STREQ("self.one", jzon_str(self, "participantId"));
 ASSERT_EQ(0, jzon_array(&tracks, self, "tracks"));
 EXPECT_EQ(0, catalog_tracks_validate(tracks));
 bool muted = true;
 EXPECT_EQ(0, jzon_bool(&muted, json_object_array_get_idx(tracks, 0), "muted"));
 EXPECT_FALSE(muted);
 mem_deref(root);
 mem_deref(encoded);
 catalog_close(c);
}
