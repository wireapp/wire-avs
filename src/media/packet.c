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
#include "avs_zapi.h"
#include "avs_media.h"
#include "priv_mediaflow.h"


/*
 * This file contains common binary packet inspection routines
 */


/*
 * See RFC 5764 figure 3:
 *
 *                  +----------------+
 *                  | 127 < B < 192 -+--> forward to RTP
 *                  |                |
 *      packet -->  |  19 < B < 64  -+--> forward to DTLS
 *                  |                |
 *                  |       B < 2   -+--> forward to STUN
 *                  +----------------+
 *
 */
bool packet_is_rtp_or_rtcp(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mbuf_buf(mb)[0];

	return 127 < b && b < 192;
}


bool packet_is_rtcp_packet(const struct mbuf *mb)
{
	uint8_t pt;

	if (mbuf_get_left(mb) < 2)
		return false;

	pt = mbuf_buf(mb)[1] & 0x7f;

	return 64 <= pt && pt <= 95;
}


bool packet_is_dtls_packet(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mb->buf[mb->pos];

	if (b >= 20 && b <= 63) {
		return true;
	}

	return false;
}


enum packet packet_classify_packet_type(const struct mbuf *mb)
{
	uint16_t *p, type;
	uint8_t b;

	b = mb->buf[mb->pos];
	p = (void *)&mb->buf[mb->pos];
	type = ntohs(*p);

	if (packet_is_rtp_or_rtcp(mb)) {
		if (packet_is_rtcp_packet(mb))
			return PACKET_RTCP;
		else
			return PACKET_RTP;
	}
	else if (b >= 20 && b <= 63) {
		return PACKET_DTLS;
	}
	else if ( ! ( type & 0xc000 )) {
		return PACKET_STUN;
	}
	else
		return PACKET_UNKNOWN;
}


const char *packet_classify_name(enum packet pkt)
{
	switch (pkt) {

	case PACKET_RTP:  return "RTP";
	case PACKET_RTCP: return "RTCP";
	case PACKET_DTLS: return "DTLS";
	case PACKET_STUN: return "STUN";
	default: return "???";
	}
}
