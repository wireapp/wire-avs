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
