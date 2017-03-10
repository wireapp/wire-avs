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
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "vie.h"


void stats_rtp_add_packet(struct transp_stats *stats,
			  const uint8_t *data, size_t len)
{
	if (!stats)
		return;

	++stats->rtp.pkt;
	stats->rtp.bytes += len;
}


int stats_rtcp_add_packet(struct transp_stats *stats,
			  const uint8_t *data, size_t len)
{
	struct mbuf *mb;
	int err = 0;

	mb = mbuf_alloc(len);
	if (!mb)
		return ENOMEM;

	mbuf_write_mem(mb, data, len);
	mb->pos = 0;

	while (mbuf_get_left(mb) > 8) {

		struct rtcp_msg *msg = 0;

		err = rtcp_decode(&msg, mb);
		if (err) {
			warning("could not decode RTCP packet "
				"(%zu bytes) (%m)\n",
				mbuf_get_left(mb), err);
			break;
		}

		++stats->rtcp.pkt;

		switch (msg->hdr.pt) {

		case RTCP_SR:
			++stats->rtcp.sr;
			break;

		case RTCP_RR:
			++stats->rtcp.rr;
			break;

		case RTCP_SDES:
			++stats->rtcp.sdes;
			break;

		case RTCP_RTPFB:
#if defined(VIE_DEBUG_RTX)
			info("vie:RTCP_RTPFB ssrc_packet = %u ssrc_media = %u n = %u \n", msg->r.fb.ssrc_packet, msg->r.fb.ssrc_media, msg->r.fb.n);
			for(int i = 0; i < msg->r.fb.n; i++){
				info("    pid = %u bitmask = %u \n", msg->r.fb.fci.gnackv[i].pid,  msg->r.fb.fci.gnackv[i].blp);
			}
#endif
			++stats->rtcp.rtpfb;
			break;

		case RTCP_PSFB:
			++stats->rtcp.psfb;

			if (msg->hdr.count == RTCP_PSFB_PLI) {
				warning("vie: RTCP PLI\n");
			}
			else if (msg->hdr.count == RTCP_PSFB_SLI) {
				warning("vie: RTCP SLI\n");
			}
			else if (msg->hdr.count == RTCP_PSFB_AFB) {
				//info("** RTCP_PSFB_AFB (REMB) **\n");

				// Parse bandwidth allocation from REMB info
				struct mbuf* mb = msg->r.fb.fci.afb; 
				if (mbuf_read_u8(mb) == 'R' && mbuf_read_u8(mb) == 'E' &&
					mbuf_read_u8(mb) == 'M' && mbuf_read_u8(mb) == 'B') {
				
					uint8_t num_ssrcs = mbuf_read_u8(mb);
					uint32_t b = mbuf_read_u8(mb) << 16 |
						mbuf_read_u8(mb) <<  8 |
						mbuf_read_u8(mb);

					uint32_t bitrate = (b & 0x3FFFF) << ((b >> 18) & 3);

					uint8_t s;
					for (s = 0; s < num_ssrcs; s++) {
						uint32_t ssrc = mbuf_read_u8(mb) << 24 |
							mbuf_read_u8(mb) <<  16 |
							mbuf_read_u8(mb) <<  8 |
							mbuf_read_u8(mb);

						if(ssrc == stats->rtcp.ssrc) {
							stats->rtcp.bitrate_limit = bitrate;
						}
					}
				}
			}
			else {
				warning("** ??? (%d) ***\n", msg->hdr.count);
			}

			break;

		case RTCP_BYE:
			++stats->rtcp.bye;
			break;

		default:
			++stats->rtcp.unknown;
			warning("*** RTCP: unknown PT (%s) ***\n",
				rtcp_type_name((enum rtcp_type)msg->hdr.pt));
			break;
		}

		mem_deref(msg);
	}

	mem_deref(mb);

	return err;
}


int stats_print(struct re_printf *pf, const struct transp_stats *stats)
{
	int err;

	if (!stats)
		return 0;

	err = re_hprintf(pf, "RTP={%zu,%zu}",
			 stats->rtp.pkt,
			 stats->rtp.bytes
			 );

	err |= re_hprintf(pf,
			  " RTCP={SR=%-2zu RR=%-2zu SDES=%-2zu"
			  " RTPFB=%-2zu PSFB=%-2zu UNK=%-2zu}",
			  stats->rtcp.sr,
			  stats->rtcp.rr,
			  stats->rtcp.sdes,
			  stats->rtcp.rtpfb,
			  stats->rtcp.psfb,
			  stats->rtcp.unknown
			  );

	return err;
}
