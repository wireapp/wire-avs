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


/* Protocol stack layers (order is important) */
enum {
	/*LAYER_RTP  =  40,*/
	LAYER_DTLS =  20,       /* must be above zero */
	LAYER_DTLS_TRANSPORT = 15,  /* below DTLS */
	LAYER_SRTP =  10,       /* must be below RTP */
	LAYER_ICE  = -10,
	LAYER_TURN = -20,       /* must be below ICE */
	LAYER_STUN = -30,       /* must be below TURN */
};


extern const char *avs_software;


/*
 * DTLS
 */

int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls);


/*
 * Packet
 */

enum packet {
	PACKET_UNKNOWN = 0,
	PACKET_RTP,
	PACKET_RTCP,
	PACKET_DTLS,
	PACKET_STUN,
};

bool packet_is_rtp_or_rtcp(const struct mbuf *mb);
bool packet_is_rtcp_packet(const struct mbuf *mb);
bool packet_is_dtls_packet(const struct mbuf *mb);
enum packet packet_classify_packet_type(const struct mbuf *mb);
const char *packet_classify_name(enum packet pkt);


/*
 * SDP
 */

int sdp_fingerprint_decode(const char *attr, struct pl *hash,
			   uint8_t *md, size_t *sz);
