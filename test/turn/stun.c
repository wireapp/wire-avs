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
#include "turn.h"


void process_msg(struct turnd *turnd, int proto, void *sock,
			const struct sa *src, const struct sa *dst,
			struct mbuf *mb)
{
	struct msgctx ctx;
	struct stun_msg *msg;
	int err;

	if (!sock || !src || !dst || !mb)
		return;

	err = stun_msg_decode(&msg, mb, &ctx.ua);
	if (err) {
		turn_raw_handler(turnd, proto, src, dst, mb);
		return;
	}

	ctx.key = NULL;
	ctx.keylen = 0;
	ctx.fp = false;
	ctx.turnd = turnd;

#if 0
	stun_msg_dump(msg);
#endif

	switch (stun_msg_class(msg)) {

	case STUN_CLASS_REQUEST:
		turn_request_handler(&ctx, proto, sock, src, dst, msg);
		break;

	case STUN_CLASS_INDICATION:
		turn_indication_handler(&ctx, proto,sock, src, dst, msg);
		break;

	default:
		debug("stun: unhandled msg class (%u) from %J\n",
		      stun_msg_class(msg), src);
		break;
	}

	mem_deref(ctx.key);
	mem_deref(msg);
}
