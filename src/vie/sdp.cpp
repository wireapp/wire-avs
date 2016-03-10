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
#include <pthread.h>
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "vie_render_view.h"
#include "vie.h"


int vie_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *data)
{
	const struct vidcodec *vc = (const struct vidcodec *)data;
	int err = 0;
	(void)vc;

	err |= mbuf_printf(mb, "a=rtcp-fb:%s ccm fir\r\n", fmt->id);
	err |= mbuf_printf(mb, "a=rtcp-fb:%s nack\r\n", fmt->id);
	err |= mbuf_printf(mb, "a=rtcp-fb:%s nack pli\r\n", fmt->id);

#if 0
	/* NOTE: REMB is not necessary */
	err |= mbuf_printf(mb, "a=rtcp-fb:%s goog-remb\r\n", fmt->id);
#endif

	return err;
}

int vie_rtx_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		     bool offer, void *data)
{
	const struct vidcodec *vc = (const struct vidcodec *)data;
	int err = 0;
	(void)vc;

	err |= mbuf_printf(mb, "a=fmtp:%s apt=100\r\n", fmt->id);

	return err;
}
