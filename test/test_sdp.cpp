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


TEST(sdp, strip_cands)
{
	const char *sdp_in =
"v=0\n"
"o=- 2677828015 1838170212 IN IP4 127.0.0.1\n"
"s=-\n"
"c=IN IP4 127.0.0.1\n"
"t=0 0\n"
"a=group:BUNDLE 0 1 2\n"
"a=msid-semantic: WMS c3a06da7-79a6-40fe-9bab-1082533fe0db\n"
"a=tool:avs 8.1.local (x86_64/darwin)\n"
"m=audio 34879 UDP/TLS/RTP/SAVPF 111\n"
"c=IN IP4 127.0.0.1\n"
"a=rtpmap:111 opus/48000/2\n"
"a=fmtp:111 minptime=10;useinbandfec=1\n"
"a=sendrecv\n"
// IPv4, host, id <= 5 -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 5\n"
// IPv4, srvflx, id > 5 -> keep
"a=candidate:1165605395 1 udp 1686052607 12.123.12.123 52723 typ srflx raddr 192.168.1.123 rport 52723 generation 0 network-id 10 network-cost 50\n"
// IPv4, relay, id > 5 -> keep
"a=candidate:4215800428 1 udp 41885439 192.168.1.1 34879 typ relay raddr 11.111.11.111 rport 52723 generation 0 network-id 10 network-cost 50\n"
// IPv4, host, id > 5 -> drop
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 7\n"
// IPv6, host, id > 5 -> keep
"a=candidate:2309545574 1 udp 2118199551 2a00:598:a902:a0a0:a0a0:a0a0:a0a0:a0a0 52884 typ host generation 0 network-id 8 network-cost 10\n"
// IPv4, host, id <= 5 -> keep
"a=candidate:110704459 1 tcp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 1\n"
// IPv4, host, id > 5 -> drop
"a=candidate:110704459 1 tcp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 7\n"
// IPv6, host, id > 5 -> keep
"a=candidate:2309545574 1 tcp 2118199551 2a00:598:a902:a0a0:a0a0:a0a0:a0a0:a0a0 52884 typ host generation 0 network-id 8 network-cost 10\n"
// IPv4, host, missing id -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0\n"
// IPv4, host, non-numeric id -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id a\n";

	const int CSZ = 8;
	const char *cands[CSZ] = {
// IPv4, host, id <= 5 -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 5\r",
// IPv4, srvflx, id > 5 -> keep
"a=candidate:1165605395 1 udp 1686052607 12.123.12.123 52723 typ srflx raddr 192.168.1.123 rport 52723 generation 0 network-id 10 network-cost 50\r",
// IPv4, relay, id > 5 -> keep
"a=candidate:4215800428 1 udp 41885439 192.168.1.1 34879 typ relay raddr 11.111.11.111 rport 52723 generation 0 network-id 10 network-cost 50\r",
// IPv4, host, id > 5 -> drop
//"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 7\r",
// IPv6, host, id > 5 -> keep
"a=candidate:2309545574 1 udp 2118199551 2a00:598:a902:a0a0:a0a0:a0a0:a0a0:a0a0 52884 typ host generation 0 network-id 8 network-cost 10\r",
// IPv4, host, id <= 5 -> keep
"a=candidate:110704459 1 tcp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 1\r",
// IPv4, host, id > 5 -> drop
//"a=candidate:110704459 1 tcp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id 7\r",
// IPv6, host, id > 5 -> keep
"a=candidate:2309545574 1 tcp 2118199551 2a00:598:a902:a0a0:a0a0:a0a0:a0a0:a0a0 52884 typ host generation 0 network-id 8 network-cost 10\r",
// IPv4, host, missing id -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0\r",
// IPv4, host, non-numeric id -> keep
"a=candidate:110704459 1 udp 1518280447 192.168.1.2 9 typ host tcptype active generation 0 network-id a\r"
};
	size_t c = 0;
	char *sdp_out = NULL;
	char *sdp_body = NULL;
	char *tok = NULL;
	struct sdp_session *sess;
	int err = 0;

	err = sdp_dup(&sess, ICALL_CONV_TYPE_ONEONONE, sdp_in, true);
	ASSERT_TRUE(0 == err);

	sdp_out = (char*)sdp_sess2str(sess);	
	ASSERT_TRUE(NULL != sdp_out);

	sdp_body = sdp_out;
	while((tok = strtok_r(sdp_body, "\n", &sdp_body))) {
		if (strstr(tok, "a=candidate")) {
			ASSERT_TRUE(c < CSZ);
			ASSERT_EQ(0, strcmp(tok, cands[c]));
			c++;
		}
	}

	ASSERT_TRUE(c == CSZ);
	mem_deref(sdp_out);
	mem_deref(sess);
}



