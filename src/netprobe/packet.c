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

#include "avs_log.h"
#include "avs_netprobe.h"
#include "netprobe.h"


int packet_encode(struct mbuf *mb, uint64_t ts, uint32_t secret,
		  uint32_t seq, uint32_t len)
{
	int err = 0;

	err |= mbuf_write_u64(mb, sys_htonll(ts));
	err |= mbuf_write_u32(mb, htonl(secret));
	err |= mbuf_write_u32(mb, htonl(seq));
	err |= mbuf_write_u32(mb, htonl(len));
	err |= mbuf_fill(mb, 0x42, len);

	return err;
}


int packet_decode(struct packet *pkt, struct mbuf *mb)
{
	if (!pkt || !mb)
		return EINVAL;

	pkt->timestamp_tx  = sys_ntohll(mbuf_read_u64(mb));
	pkt->secret        = ntohl(mbuf_read_u32(mb));
	pkt->seq           = ntohl(mbuf_read_u32(mb));
	pkt->payload_bytes = ntohl(mbuf_read_u32(mb));

	if (mbuf_get_left(mb) < pkt->payload_bytes) {
		warning("netprobe: short packet (%zu bytes)\n",
			pkt->payload_bytes);
		return EBADMSG;
	}

	return 0;
}
