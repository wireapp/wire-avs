#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include <string>

namespace {
const std::string offer =
 "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0 1 3\r\n"
 "m=audio 9 UDP/TLS/RTP/SAVPF 111 0\r\na=mid:0\r\na=sendonly\r\n"
 "a=rtpmap:111 opus/48000/2\r\na=fmtp:111 minptime=10;useinbandfec=1;cbr=0\r\n"
 "a=ice-ufrag:unchanged\r\na=ice-pwd:unchanged-password\r\n"
 "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:1\r\na=sctp-port:5000\r\n"
 "m=video 0 UDP/TLS/RTP/SAVPF 96 97\r\na=mid:2\r\na=inactive\r\n"
 "a=rtpmap:96 VP8/90000\r\na=rtpmap:97 rtx/90000\r\na=fmtp:97 apt=96\r\n"
 "m=video 9 UDP/TLS/RTP/SAVPF 96 97\r\na=mid:3\r\na=sendonly\r\n"
 "a=rtpmap:96 VP8/90000\r\na=rtpmap:97 rtx/90000\r\na=fmtp:97 apt=96\r\n"
 "a=rid:h send\r\na=simulcast:send h\r\na=msid:screen-stream screen-track\r\n";
std::string modify(const std::string &input, bool cbr) {
 char *result = NULL;
 const int err = sdp_publisher_local(&result, input.c_str(), cbr);
 EXPECT_EQ(0, err);
 std::string output = result ? result : "";
 mem_deref(result);
 return output;
}
}

TEST(PublisherSdp, preserves_rejected_and_new_video_sections_without_cbr) {
 EXPECT_EQ(offer, modify(offer, false));
}
TEST(PublisherSdp, only_updates_opus_cbr_and_is_idempotent_across_both_paths) {
 auto expected = offer;
 expected.replace(expected.find("cbr=0"), 5, "cbr=1");
 const auto local = modify(offer, true);
 EXPECT_EQ(expected, local);
 EXPECT_EQ(expected, modify(local, true));
}
TEST(PublisherSdp, adds_missing_opus_fmtp_without_changing_other_codecs) {
 const std::string input = "v=0\r\nm=audio 9 RTP/AVP 112 0\r\na=mid:0\r\n"
  "a=rtpmap:112 Opus/48000/2\r\na=rtpmap:0 PCMU/8000\r\n"
  "m=video 0 RTP/AVP 96\r\na=mid:2\r\n";
 auto expected = input;
 expected.insert(expected.find("m=video"), "a=fmtp:112 cbr=1\r\n");
 EXPECT_EQ(expected, modify(input, true));
}
TEST(PublisherSdp, preserves_non_opus_fmtp_and_handles_fmtp_before_rtpmap) {
 const std::string input = "m=audio 9 RTP/AVP 111 112\n"
  "a=fmtp:111 minptime=10\n"
  "a=rtpmap:111 opus/48000/2\n"
  "a=rtpmap:112 other/48000/2\n"
  "a=fmtp:112 cbr=0\n"
  "m=video 9 RTP/AVP 96\na=mid:3\n";
 auto expected = input;
 expected.replace(expected.find("minptime=10"), 11, "minptime=10;cbr=1");
 EXPECT_EQ(expected, modify(input, true));
}
TEST(PublisherSdp, preserves_long_offers_including_last_mid) {
 std::string input = offer;
 for (unsigned i = 0; i < 500; ++i) input += "a=x-unknown:" + std::to_string(i) + "\r\n";
 input += "m=video 9 RTP/AVP 96\r\na=mid:4\r\na=sendonly\r\n";
 auto expected = input;
 expected.replace(expected.find("cbr=0"), 5, "cbr=1");
 EXPECT_EQ(expected, modify(input, true));
}
TEST(PublisherSdp, invalid_arguments) {
 char *result = NULL;
 EXPECT_EQ(EINVAL, sdp_publisher_local(NULL, offer.c_str(), true));
 EXPECT_EQ(EINVAL, sdp_publisher_local(&result, NULL, true));
}
